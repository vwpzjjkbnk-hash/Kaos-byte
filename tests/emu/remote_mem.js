// How heavy the remote page is on a phone (run_remote.py with REMOTE_MODE=load
// REMOTE_SCRIPT=remote_mem.js): a phone-sized Chromium slowed down 4x like a phone's CPU scrolls the
// whole list to the bottom, opens and closes 20 figures, then sits for 30 s. Records the page's
// memory (JS heap), how many DOM nodes it holds, slow frames while scrolling, and errors.
//   node remote_mem.js <url> <dir> <result.json>
const { chromium, devices } = require("playwright");
const fs = require("fs");
const path = require("path");

(async () => {
  const [url, dir, out] = process.argv.slice(2);
  const r = { errors: [] };
  const browser = await chromium.launch();
  const phone = devices["iPhone 13"];
  const ctx = await browser.newContext({ viewport: phone.viewport, deviceScaleFactor: phone.deviceScaleFactor,
    isMobile: true, hasTouch: true, userAgent: phone.userAgent });
  const page = await ctx.newPage();
  page.on("pageerror", e => r.errors.push(String(e)));
  const cdp = await ctx.newCDPSession(page);
  await cdp.send("Performance.enable");
  await cdp.send("Emulation.setCPUThrottlingRate", { rate: 4 });
  // Resident memory of the browser's processes, from /proc: the page's own process (renderer), the
  // GPU process, and all of them together (the browser process holds blob: pictures).
  const procMB = () => {
    const kb = { renderer: 0, gpu: 0, total: 0 };
    for (const d of fs.readdirSync("/proc")) {
      if (!/^\d+$/.test(d)) continue;
      try {
        const cmd = fs.readFileSync(`/proc/${d}/cmdline`, "utf8");
        if (!/chrom(e|ium)|headless_shell/i.test(cmd)) continue;
        const rss = +(fs.readFileSync(`/proc/${d}/status`, "utf8").match(/VmRSS:\s+(\d+)/) || [0, 0])[1];
        if (/--type=renderer/.test(cmd)) kb.renderer += rss;
        else if (/--type=gpu-process/.test(cmd)) kb.gpu += rss;
        kb.total += rss;
      } catch (e) { /* gone */ }
    }
    return { rendererMB: Math.round(kb.renderer / 1024), gpuMB: Math.round(kb.gpu / 1024), totalMB: Math.round(kb.total / 1024) };
  };
  const metrics = async () => {
    const m = (await cdp.send("Performance.getMetrics")).metrics;
    const get = n => (m.find(x => x.name === n) || {}).value || 0;
    const blobs = await page.evaluate(() => [...document.querySelectorAll("*")]
      .filter(e => /blob:/.test((e.style && e.style.backgroundImage) || "") || /^blob:/.test(e.src || "")).length);
    const held = await page.evaluate(() => [...document.querySelectorAll("img")].filter(i => i.getAttribute("src")).length +
      [...document.querySelectorAll(".pic")].filter(p => /url\(/.test(p.style.backgroundImage || "")).length);
    return { heapMB: +(get("JSHeapUsedSize") / 1048576).toFixed(1), nodes: get("Nodes"),
             layouts: get("LayoutCount"), blobs, held, ...procMB() };
  };
  try {
    const t0 = Date.now();
    await page.goto(url, { waitUntil: "domcontentloaded", timeout: 60000 });
    await page.waitForFunction(() => document.querySelectorAll(".card").length > 400, null, { timeout: 180000 });
    r.listMs = Date.now() - t0;
    r.atStart = await metrics();
    // Scroll to the bottom like a thumb would, timing frames.
    await page.evaluate(() => {
      window.__frames = [];
      let last = performance.now();
      const tick = t => { window.__frames.push(t - last); last = t; window.__raf = requestAnimationFrame(tick); };
      window.__raf = requestAnimationFrame(tick);
    });
    const height = await page.evaluate(() => document.body.scrollHeight);
    for (let y = 0; y < height; y += 700) {
      await page.evaluate(v => window.scrollTo(0, v), y);
      await page.waitForTimeout(120);
    }
    await page.waitForTimeout(3000);
    const frames = await page.evaluate(() => { cancelAnimationFrame(window.__raf); return window.__frames; });
    r.scroll = { frames: frames.length, slow: frames.filter(f => f > 50).length,
                 worstMs: Math.round(Math.max(...frames)) };
    r.afterScroll = await metrics();
    // Open and close 20 figures.
    await page.evaluate(() => window.scrollTo(0, 0));
    await page.waitForTimeout(500);
    for (let i = 0; i < 20; i++) {
      await page.evaluate(k => {
        const cards = document.querySelectorAll(".card");
        cards[(k * 23) % cards.length].click();
      }, i);
      await page.waitForTimeout(250);
      await page.evaluate(() => { const s = document.getElementById("shade"); if (s) s.click(); });
      await page.waitForTimeout(150);
    }
    r.afterSheets = await metrics();
    await page.waitForTimeout(30000);
    r.afterIdle = await metrics();
    r.totalMs = Date.now() - t0;
  } catch (e) {
    r.errors.push("test: " + String(e));
    await page.screenshot({ path: path.join(dir, "mem_error.png") }).catch(() => {});
  }
  fs.writeFileSync(out, JSON.stringify(r, null, 1));
  console.log(JSON.stringify(r));
  await browser.close();
})();
