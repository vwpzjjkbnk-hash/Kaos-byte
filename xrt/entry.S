// SkyPortal360 open toolchain: the boundary between the console's code and the plugin's.
//
// The plugin is stock 32-bit SysV code (clang). Stock SysV code keeps only the low halves of
// r14-r31, while the console's MSVC code keeps 64-bit values there across calls (xsdk
// docs/HARDWARE.md measured MSVC titles losing them). So every place the console calls into the
// plugin goes through GUEST_ENTRY, which saves r14-r31 whole and CR2-CR4 around the C function:
//   - the DLL entry point (the loader calls it like DllMain, also for every thread start/exit),
//   - the thread startup routine ExCreateThread runs,
//   - the five import hooks the game calls through its patched import stubs.
//
// The trampoline writes the return address at 4(caller's r1), as SysV does: the console's callers
// keep nothing at 4-15(r1) across a call and its callees don't write there (xsdk measured).
// Arguments pass through untouched in r3-r10 and the result comes back in r3.

	.machine ppc64

#define GUEST_FRAME 176   /* 16 header + 18*8 GPRs (16..159) + CR (160) + pad, 16-aligned */

.macro GUEST_ENTRY name, target
	.section .text.\name, "ax"
	.balign 4
	.globl	\name
	.type	\name, @function
\name:
	mflr	0
	stw	0, 4(1)
	stwu	1, -GUEST_FRAME(1)
	std	14, 16(1)
	std	15, 24(1)
	std	16, 32(1)
	std	17, 40(1)
	std	18, 48(1)
	std	19, 56(1)
	std	20, 64(1)
	std	21, 72(1)
	std	22, 80(1)
	std	23, 88(1)
	std	24, 96(1)
	std	25, 104(1)
	std	26, 112(1)
	std	27, 120(1)
	std	28, 128(1)
	std	29, 136(1)
	std	30, 144(1)
	std	31, 152(1)
	mfcr	0
	stw	0, 160(1)
	bl	\target
	lwz	0, 160(1)
	mtcrf	0x38, 0          /* CR2, CR3, CR4: the non-volatile fields */
	ld	14, 16(1)
	ld	15, 24(1)
	ld	16, 32(1)
	ld	17, 40(1)
	ld	18, 48(1)
	ld	19, 56(1)
	ld	20, 64(1)
	ld	21, 72(1)
	ld	22, 80(1)
	ld	23, 88(1)
	ld	24, 96(1)
	ld	25, 104(1)
	ld	26, 112(1)
	ld	27, 120(1)
	ld	28, 128(1)
	ld	29, 136(1)
	ld	30, 144(1)
	ld	31, 152(1)
	addi	1, 1, GUEST_FRAME
	lwz	0, 4(1)
	mtlr	0
	blr
	.size	\name, . - \name
.endm

// The XEX entry point: xrt_dll_entry(handle, reason, reserved) runs static constructors on the
// first DLL_PROCESS_ATTACH, then calls the plugin's DllMain.
GUEST_ENTRY _xrt_dll_entry, xrt_dll_entry

// ExCreateThread's startup routine: xrt_thread_startup(start, context).
GUEST_ENTRY xrt_thread_startup_guest, xrt_thread_startup

// The import hooks (src/xbox/hooks.cpp).
GUEST_ENTRY sp_hook_get_raw_guest, sp_hook_get_raw
GUEST_ENTRY sp_hook_set_raw_guest, sp_hook_set_raw
GUEST_ENTRY sp_hook_get_raw_ex_guest, sp_hook_get_raw_ex
GUEST_ENTRY sp_hook_set_raw_ex_guest, sp_hook_set_raw_ex
GUEST_ENTRY sp_hook_get_state_guest, sp_hook_get_state
GUEST_ENTRY sp_hook_get_proc_guest, sp_hook_get_proc
GUEST_ENTRY sp_vdswap_record_guest, sp_vdswap_record

// sp_vdswap_hook: what the game's VdSwap import points at in the skydrawn build. VdSwap takes ten
// arguments, the last two on the caller's stack (0x50/0x58 of its frame), so this stub never calls
// VdSwap itself: it records the frame (r4 = fetch constant, r8 = &front buffer address, r9 =
// &texture format) through a GUEST_ENTRY trampoline, puts r3-r10, LR and r1 back exactly as the
// game left them, and jumps to the real VdSwap, which then returns straight to the game. While the
// menu is open, r4 and r8 are pointed at the menu frame's copies so the menu is what's shown.
	.section .text.sp_vdswap_hook, "ax"
	.balign 4
	.globl	sp_vdswap_hook
	.type	sp_vdswap_hook, @function
sp_vdswap_hook:
	stwu	1, -0x70(1)
	mflr	0
	stw	0, 0x6C(1)
	std	3, 0x10(1)
	std	4, 0x18(1)
	std	5, 0x20(1)
	std	6, 0x28(1)
	std	7, 0x30(1)
	std	8, 0x38(1)
	std	9, 0x40(1)
	std	10, 0x48(1)
	mr	3, 4
	mr	4, 8
	mr	5, 9
	bl	sp_vdswap_record_guest
	ld	3, 0x10(1)
	ld	4, 0x18(1)
	ld	5, 0x20(1)
	ld	6, 0x28(1)
	ld	7, 0x30(1)
	ld	8, 0x38(1)
	ld	9, 0x40(1)
	ld	10, 0x48(1)
	lwz	0, 0x6C(1)
	mtlr	0
	addi	1, 1, 0x70
	/* Menu open: present the plugin's frame instead (src/xbox/overlay.cpp MenuFrame: fetch
	   constant at +0, front buffer address word at +24). CR0 is volatile across the call. */
	lis	12, sp_menu_current@ha
	lwz	12, sp_menu_current@l(12)
	cmpwi	12, 0
	beq	1f
	mr	4, 12
	addi	8, 12, 24
1:
	lis	12, sp_real_vdswap@ha
	lwz	12, sp_real_vdswap@l(12)
	mtctr	12
	bctr
	.size	sp_vdswap_hook, . - sp_vdswap_hook

// sp_set_gfx_cb_hook: what the game's VdSetGraphicsInterruptCallback import points at in the
// skydrawn build. Direct3D registers its interrupt handler (callback, context) with the kernel; the
// handler runs at every vertical blank and GPU interrupt and is where the scan-out address is
// programmed. This keeps Direct3D's handler and registers sp_gfx_irq_hook in its place (the
// context stays Direct3D's), then jumps to the real export.
	.section .text.sp_set_gfx_cb_hook, "ax"
	.balign 4
	.globl	sp_set_gfx_cb_hook
	.type	sp_set_gfx_cb_hook, @function
sp_set_gfx_cb_hook:
	lis	12, sp_game_gfx_cb@ha
	stw	3, sp_game_gfx_cb@l(12)
	cmpwi	3, 0
	beq	1f
	lis	3, sp_gfx_irq_hook@ha
	addi	3, 3, sp_gfx_irq_hook@l
	clrldi	3, 3, 32             /* a 32-bit pointer, zero-extended as the kernel expects */
1:
	lis	12, sp_real_set_gfx_cb@ha
	lwz	12, sp_real_set_gfx_cb@l(12)
	mtctr	12
	bctr
	.size	sp_set_gfx_cb_hook, . - sp_set_gfx_cb_hook

// sp_gfx_irq_hook(source, context): runs in the kernel's graphics interrupt in place of Direct3D's
// handler. Calls Direct3D's handler first (which moves scan-out to the game's newest frame at
// vertical blank), then, while the menu is up, points scan-out straight back at the menu surface,
// microseconds later and still inside the blank, so no game frame is ever scanned out. Only
// volatile registers are used; the frame keeps 0x10-0x57 free for the callee (console convention).
	.section .text.sp_gfx_irq_hook, "ax"
	.balign 4
	.globl	sp_gfx_irq_hook
	.type	sp_gfx_irq_hook, @function
sp_gfx_irq_hook:
	stwu	1, -0x70(1)
	mflr	0
	stw	0, 0x68(1)
	/* Menu up: lock the display's surface registers while Direct3D's handler runs
	   (D1GRPH_UPDATE bit 16, as Linux's radeon page flip does), so the game's frame address it
	   writes is never taken by the display before it's replaced with the menu's below; unlocked
	   again at the end. Only done when the register holds nothing but the bits known (lock,
	   update pending). r3/r4 (the handler's arguments) are left alone. */
	lis	12, sp_pin_phys@ha
	lwz	12, sp_pin_phys@l(12)
	cmpwi	12, 0
	beq	6f
	lis	11, 0x7FC8
	ori	11, 11, 0x6144          /* D1GRPH_UPDATE */
	lwz	10, 0(11)
	lis	12, 0xFFFE
	ori	12, 12, 0xFFFB
	and.	12, 10, 12
	bne	6f
	oris	12, 10, 0x0001          /* | D1GRPH_UPDATE_LOCK */
	stw	12, 0(11)
	sync
	lis	11, sp_lock_saved@ha
	stw	10, sp_lock_saved@l(11)
	lis	11, sp_lock_held@ha
	li	12, 1
	stw	12, sp_lock_held@l(11)
	lis	11, sp_lock_uses@ha
	lwz	12, sp_lock_uses@l(11)
	addi	12, 12, 1
	stw	12, sp_lock_uses@l(11)
6:
	lis	12, sp_game_gfx_cb@ha
	lwz	12, sp_game_gfx_cb@l(12)
	cmpwi	12, 0
	beq	2f
	mtctr	12
	bctrl
2:
	lis	11, sp_irq_count@ha
	lwz	12, sp_irq_count@l(11)
	addi	12, 12, 1
	stw	12, sp_irq_count@l(11)
	lis	12, sp_pin_phys@ha
	lwz	12, sp_pin_phys@l(12)
	cmpwi	12, 0
	beq	3f
	/* Menu up: new scan-out addresses wait for the vertical blank, not the next line
	   (D1GRPH_FLIP_CONTROL bit 0; only touched when it reads exactly 1). See sp_flipctl_saved. */
	lis	11, 0x7FC8
	ori	11, 11, 0x6148          /* D1GRPH_FLIP_CONTROL (0x7FC80000 + 0x1852 * 4) */
	lwz	10, 0(11)
	cmpwi	10, 1
	bne	5f
	li	10, 0
	stw	10, 0(11)
	sync
	lis	11, sp_flipctl_saved@ha
	li	10, 1
	stw	10, sp_flipctl_saved@l(11)
	lis	11, sp_flipctl_clears@ha
	lwz	10, sp_flipctl_clears@l(11)
	addi	10, 10, 1
	stw	10, sp_flipctl_clears@l(11)
5:
	lis	11, 0x7FC8
	ori	11, 11, 0x6110          /* D1GRPH_PRIMARY_SURFACE_ADDRESS (0x7FC80000 + 0x1844 * 4) */
	lwz	10, 0(11)
	cmpw	10, 12
	beq	3f
	stw	12, 0(11)
	sync
	/* Remember the game's frame (to put back when the menu closes), not a menu surface. */
	lis	11, sp_surf_phys@ha
	addi	11, 11, sp_surf_phys@l
	lwz	12, 0(11)
	cmpw	10, 12
	beq	4f
	lwz	12, 4(11)
	cmpw	10, 12
	beq	4f
	lwz	12, 8(11)
	cmpw	10, 12
	beq	4f
	lis	11, sp_pin_game@ha
	stw	10, sp_pin_game@l(11)
	lis	11, sp_irq_game_fixes@ha
	lwz	10, sp_irq_game_fixes@l(11)
	addi	10, 10, 1
	stw	10, sp_irq_game_fixes@l(11)
4:
	lis	11, sp_irq_fixes@ha
	lwz	10, sp_irq_fixes@l(11)
	addi	10, 10, 1
	stw	10, sp_irq_fixes@l(11)
3:
	/* Unlock (see the top): the display takes the menu's address at the next vertical blank. */
	lis	11, sp_lock_held@ha
	lwz	12, sp_lock_held@l(11)
	cmpwi	12, 0
	beq	7f
	li	12, 0
	stw	12, sp_lock_held@l(11)
	lis	11, sp_lock_saved@ha
	lwz	10, sp_lock_saved@l(11)
	lis	11, 0x7FC8
	ori	11, 11, 0x6144
	stw	10, 0(11)
	sync
7:
	lwz	0, 0x68(1)
	mtlr	0
	addi	1, 1, 0x70
	blr
	.size	sp_gfx_irq_hook, . - sp_gfx_irq_hook

// xrt_call_guest(fn, a, b, c, d): calls console code at `fn` (a real XAM function the hooks pass
// through to) by the console's convention: arguments zero-extended in r3-r6, inside a 0x60-byte
// frame of our own.
	.section .text.xrt_call_guest, "ax"
	.balign 4
	.globl	xrt_call_guest
	.type	xrt_call_guest, @function
xrt_call_guest:
	mflr	0
	stw	0, 4(1)
	stwu	1, -0x60(1)
	mtctr	3
	clrldi	3, 4, 32
	clrldi	4, 5, 32
	clrldi	5, 6, 32
	clrldi	6, 7, 32
	li	7, 0
	li	8, 0
	li	9, 0
	li	10, 0
	bctrl
	addi	1, 1, 0x60
	lwz	0, 4(1)
	mtlr	0
	blr
	.size	xrt_call_guest, . - xrt_call_guest
