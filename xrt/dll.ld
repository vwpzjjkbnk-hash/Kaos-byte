/* SkyPortal360 open toolchain: lays the title DLL out for elf2xex.py. Read-only data, code and
   writable data each start on their own 64 KB page (XEX page descriptors are per page). The base
   must match --base given to elf2xex.py and must not overlap the game (title images start at
   0x82000000). Adapted from openpak/x360-sdk src/xsdk.ld (MIT). */
OUTPUT_FORMAT("elf32-powerpc")
OUTPUT_ARCH(powerpc:common)
ENTRY(_xrt_dll_entry)

SECTIONS
{
	/* elf2xex puts the PE headers in the first 4 KB. */
	. = 0x8A000000 + 0x1000;
	.rdata : {
		/* Import slots are referenced only from .xsdk.imports, which --gc-sections doesn't count. */
		KEEP(*(.rodata.xrt_imp_*))
		*(.rodata .rodata.*)
		*(.sdata2 .sdata2.* .sbss2 .sbss2.*)
	}

	. = ALIGN(0x10000);
	.text : {
		KEEP(*(.text.xrt_thunk_*))
		*(.text .text.*)
	}

	. = ALIGN(0x10000);
	.init_array : {
		__init_array_start = .;
		KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))
		KEEP(*(.init_array))
		__init_array_end = .;
	}
	.data : {
		*(.data .data.*)
		*(.sdata .sdata.*)
	}
	.bss : {
		*(.sbss .sbss.* .bss .bss.* COMMON)
	}

	.xsdk.imports 0 : {
		KEEP(*(.xsdk.imports))
	}

	/DISCARD/ : {
		*(.comment .note.* .gnu.attributes .eh_frame .eh_frame_hdr .fini_array .fini_array.* .ctors .dtors)
	}
}
