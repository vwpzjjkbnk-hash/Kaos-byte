// A phone using the remote page for a while (run_remote.py with REMOTE_MODE=load): times the first
// screen of pictures, six screens of scrolling, opening My Picks, then counts "Not connected"
// moments over 20 s of leaving the page open.  node remote_load.js <url> <dir> <result.json>
const { chromium, devices } = require("playwright");
const fs = require("fs");
const path = require("path");

(async () => {
  const [url, dir, out] = process.argv.slice(2);
  const r = { errors: [], steps: [] };
  const browser = await chromium.launch();
  const phone = devices["iPhone 13"];
  const ctx = await browser.newContext({ viewport: phone.viewport, deviceScaleFactor: phone.deviceScaleFactor,
    isMobile: true, hasTouch: true, userAgent: phone.userAgent });
  const page = await ctx.newPage();
  page.on("pageerror", e => r.errors.push(String(e)));
  let failed = 0;
  page.on("requestfailed", () => failed++);
  // Pictures in view that have something drawn (any picture format).
  const visibleLoaded = () => page.evaluate(() => {
    const pics = [...document.querySelectorAll(".card .pic")].filter(p => {
      const b = p.getBoundingClientRect(); return b.bottom > 0 && b.top < innerHeight; });
    const drawn = p => {  // 3.2.0v pictures are <img> tags; older pages painted backgrounds
      const i = p.querySelector("img");
      return i ? !!(i.getAttribute("src") && i.complete && i.naturalWidth > 0)
               : !!(p.style.backgroundImage && p.style.backgroundImage !== "none");
    };
    const done = pics.filter(drawn).length;
    return { done, total: pics.length };
  });
  const waitPictures = async (limit) => {
    const t = Date.now();
    for (;;) {
      const v = await visibleLoaded();
      if (v.total > 0 && v.done >= v.total) return { ms: Date.now() - t, ...v, ok: true };
      if (Date.now() - t > limit) return { ms: Date.now() - t, ...v, ok: false };
      await page.waitForTimeout(100);
    }
  };
  try {
    const t0 = Date.now();
    await page.goto(url, { waitUntil: "domcontentloaded", timeout: 60000 });
    await page.waitForFunction(() => document.querySelectorAll(".card").length > 20, null, { timeout: 120000 });
    r.listMs = Date.now() - t0;
    const first = await waitPictures(60000);
    r.firstScreenMs = Date.now() - t0;
    r.firstScreen = first;
    await page.screenshot({ path: path.join(dir, "load_first.png") });
    for (let i = 1; i <= 6; i++) {
      await page.evaluate(y => window.scrollTo(0, y), i * 800);
      r.steps.push(await waitPictures(30000));
    }
    const tp = Date.now();
    await page.locator(".tab", { hasText: "My Picks" }).tap();
    await page.waitForFunction(() => document.querySelector(".sec .t") && /Roster/.test(document.querySelector(".sec .t").textContent),
      null, { timeout: 60000 }).catch(() => r.errors.push("my picks didn't load"));
    await page.evaluate(() => window.scrollTo(0, 0));
    r.picks = await waitPictures(30000);
    r.picksMs = Date.now() - tp;
    // Leave it open: how often does it say "Not connected" (red), or "Reconnecting" (yellow, 3.2.0v)?
    let off = 0, wait = 0, samples = 0;
    const ti = Date.now();
    while (Date.now() - ti < 20000) {
      const pill = await page.$eval("#pill", e => e.className);
      if (/\boff\b/.test(pill)) off++;
      else if (/\bwait\b/.test(pill)) wait++;
      samples++;
      await page.waitForTimeout(250);
    }
    r.offlineSamples = off;
    r.reconnectingSamples = wait;
    r.samples = samples;
    r.totalMs = Date.now() - t0;
    await page.screenshot({ path: path.join(dir, "load_end.png") });
  } catch (e) {
    r.errors.push("test: " + String(e));
  }
  r.failedRequests = failed;
  fs.writeFileSync(out, JSON.stringify(r, null, 1));
  console.log(JSON.stringify({ listMs: r.listMs, firstScreenMs: r.firstScreenMs, picksMs: r.picksMs,
    offline: r.offlineSamples + "/" + r.samples, reconnecting: r.reconnectingSamples, failed, errors: r.errors }));
  await browser.close();
})();
