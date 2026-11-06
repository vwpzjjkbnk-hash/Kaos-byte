// The phone side of run_remote.py: a phone-sized headless Chromium opens the page the emulated
// Xbox serves and uses it like a person would:
//   - browses (the first screen of pictures),
//   - Quick tap: one tap swaps a figure in (shown at once, then confirmed), holding opens it instead,
//   - opens Chop Chop from the search and puts him on the rift,
//   - live updates: a change made elsewhere (another phone, the TV menu) shows up by itself,
//   - a Wi-Fi blip: every connection is cut for 4 s, and the page carries on afterwards on its own,
//   - My Picks.
// Ends with only Chop Chop on the rift (run_remote.py then has the game read him).
// Writes what it saw as JSON plus screenshots.
//   node remote_browser.js <url> <screenshot dir> <result.json>
const { chromium, devices } = require("playwright");
const fs = require("fs");
const path = require("path");

(async () => {
  const [url, dir, out] = process.argv.slice(2);
  const result = { errors: [], timings: {} };
  const browser = await chromium.launch();
  const phone = devices["iPhone 13"];
  const ctx = await browser.newContext({
    viewport: phone.viewport, deviceScaleFactor: phone.deviceScaleFactor, isMobile: true, hasTouch: true,
    userAgent: phone.userAgent,
  });
  const page = await ctx.newPage();
  const requests = { state: 0, events: 0, list: 0, do: 0, files: 0 };
  page.on("request", r => {
    const u = new URL(r.url());
    if (u.pathname === "/api/state") requests.state++;
    else if (u.pathname === "/api/events") requests.events++;
    else if (u.pathname === "/api/list") requests.list++;
    else if (u.pathname === "/api/do") requests.do++;
    else requests.files++;
  });
  page.on("pageerror", e => result.errors.push(String(e)));
  page.on("console", m => {
    if (m.type() === "error" && !/Failed to load resource|ERR_|EventSource|net::/.test(m.text())) result.errors.push(m.text());
  });
  const shot = async name => page.screenshot({ path: path.join(dir, name) });
  const cdp = await ctx.newCDPSession(page);
  // Another phone (or the TV menu) changing the rift: straight to the Xbox, not through this page.
  const elsewhere = async (a, id) => (await fetch(`${url}api/do?a=${a}&i=${id}`, { method: "POST" })).json();
  const chips = () => page.$$eval("#on .chip .n", n => n.map(x => x.textContent));
  const idOf = name => page.evaluate(n => {
    const c = [...document.querySelectorAll(".card")].find(x => x._f && x._f.name === n);
    return c ? c._f.id : -1;
  }, name);
  const waitChip = (name, present, ms) => page.waitForFunction(([n, p]) =>
    [...document.querySelectorAll("#on .chip:not(.pending) .n")].some(x => x.textContent === n) === p,
    [name, present], { timeout: ms || 60000 });
  const toastSays = text => page.waitForFunction(t => document.querySelector("#toast").classList.contains("show") &&
    document.querySelector("#toast").textContent.includes(t), text, { timeout: 60000 });
  const shown = sel => `[...document.querySelectorAll(${JSON.stringify(sel)})]`;
  try {
    const t0 = Date.now();
    await page.goto(url, { waitUntil: "domcontentloaded", timeout: 120000 });
    await page.waitForFunction(() => document.querySelectorAll(".card").length > 50, null, { timeout: 240000 });
    // The first screen of pictures.
    await page.waitForFunction(
      `${shown(".card .pic img")}.slice(0, 6).every(i => i.getAttribute("src") && i.complete && i.naturalWidth > 0)`,
      null, { timeout: 240000 });
    await page.waitForFunction(() => /url\(/.test(document.querySelector("#bg").style.backgroundImage), null, { timeout: 120000 })
      .catch(() => {});
    result.timings.firstScreenMs = Date.now() - t0;
    console.log(`page with pictures after ${(result.timings.firstScreenMs / 1000).toFixed(1)} s`);
    await page.waitForTimeout(800);
    await shot("remote_1_browse.png");
    result.cards = await page.$$eval(".card", c => c.length);
    result.tabs = await page.$$eval(".tab", t => t.map(x => x.textContent));
    result.sections = await page.$$eval(".sec .t", t => t.slice(0, 5).map(x => x.textContent));
    result.live = await page.evaluate(() => document.querySelector("#pill").className);

    // ---- Quick tap ----
    await page.tap("#quick");
    result.quickLit = await page.$eval("#quick", e => e.className === "lit");
    const firstName = await page.$eval(".card .n", e => e.textContent);
    // Holding a card opens it and puts nothing on.
    const box = await page.locator(".card").nth(1).boundingBox();
    await cdp.send("Input.dispatchTouchEvent", { type: "touchStart", touchPoints: [{ x: box.x + box.width / 2, y: box.y + box.height / 2 }] });
    await page.waitForTimeout(650);
    await cdp.send("Input.dispatchTouchEvent", { type: "touchEnd", touchPoints: [] });
    await page.waitForTimeout(300);
    result.holdOpens = await page.evaluate(() => document.body.classList.contains("sheet"));
    result.holdAddsNothing = (await page.$$("#on .chip")).length === 0;
    await page.waitForTimeout(400);
    await page.click("#shade", { position: { x: 30, y: 30 } });  // above the sheet
    await page.waitForFunction(() => !document.body.classList.contains("sheet"), null, { timeout: 10000 }).catch(() => {});
    // One tap: on the phone at once, then the Xbox confirms.
    const tapped = await page.evaluate(() => {
      window.__t0 = performance.now();
      const c = document.querySelector(".card");
      c.dispatchEvent(new MouseEvent("click", { bubbles: true }));
      const chip = document.querySelector("#on .chip");  // straight after the tap, before anything comes back
      return { chip: chip ? chip.querySelector(".n").textContent : null, pending: chip ? chip.classList.contains("pending") : null,
               cardOn: c.classList.contains("on") };
    });
    result.quickTap = tapped;
    await waitChip(firstName, true);
    await toastSays("on the rift");
    result.timings.quickConfirmMs = Math.round(await page.evaluate(() => performance.now() - window.__t0));
    await page.tap("#quick");  // off again
    // Take it off with the chip's x: gone at once.
    result.removeInstant = await page.evaluate(() => {
      document.querySelector("#on .chip .x").click();
      return document.querySelectorAll("#on .chip").length === 0;
    });
    await toastSays("off the rift").catch(() => {});
    await page.waitForTimeout(500);

    // ---- Chop Chop from the search ----
    await page.fill("#q", "chop chop");
    await page.waitForTimeout(400);
    const chop = page.locator(".card", { hasText: "Chop Chop" }).first();
    await chop.scrollIntoViewIfNeeded();
    await page.waitForFunction(() => {
      const c = [...document.querySelectorAll(".card")].find(x => x.textContent.includes("Chop Chop"));
      const i = c && c.querySelector(".pic img");
      return i && i.complete && i.naturalWidth > 0;
    }, null, { timeout: 120000 }).catch(() => {});
    await shot("remote_4_search.png");
    await chop.tap();
    await page.waitForFunction(() => document.querySelector("#art")._state === "big" && document.querySelector("#art").complete,
      null, { timeout: 120000 }).catch(() => {});
    await page.waitForTimeout(600);
    await shot("remote_2_sheet.png");
    result.sheet = await page.$eval("#sname", e => e.textContent);
    result.bigArt = await page.$eval("#art", e => e.naturalHeight);

    await page.tap("#add");
    await toastSays("Chop Chop");
    result.toast = await page.$eval("#toast", e => e.textContent);
    await page.fill("#q", "");
    await waitChip("Chop Chop", true);
    await page.evaluate(() => window.scrollTo(0, 0));
    await page.waitForTimeout(1500);
    await shot("remote_3_on.png");
    result.rift = await chips();
    result.pictures = await page.$$eval(".pic img", p => p.filter(i => i.getAttribute("src") && i.naturalWidth > 0).length);

    // ---- Live updates: Hex put on and taken off from elsewhere ----
    const hex = await idOf("Hex");
    const before = { ...requests };
    let t = Date.now();
    result.elsewhereAdd = await elsewhere("add", hex);
    await waitChip("Hex", true, 20000);
    result.timings.pushAddMs = Date.now() - t;
    t = Date.now();
    await elsewhere("remove", hex);
    await waitChip("Hex", false, 20000);
    result.timings.pushRemoveMs = Date.now() - t;
    result.pollsWhileLive = requests.state - before.state;

    // ---- Nicknames, Favorites and lists of your own ----
    const dlgShown = id => page.waitForFunction(i => document.getElementById(i).classList.contains("shown"), id, { timeout: 20000 });
    await page.evaluate(() => window.scrollTo(0, 0));
    await page.fill("#q", "chop chop");
    await page.waitForTimeout(400);
    await page.locator(".card", { hasText: "Chop Chop" }).first().tap();
    await page.waitForFunction(() => document.body.classList.contains("sheet"), null, { timeout: 20000 });
    result.nickBefore = await page.$eval("#snick", e => e.textContent);
    await page.tap("#snick");
    await dlgShown("dlg");
    await page.fill("#dinput", "Sir Chops");
    await page.tap("#dok");
    await toastSays("Sir Chops");
    result.nickToast = await page.$eval("#toast", e => e.textContent);
    result.nickSheet = await page.$eval("#snick", e => e.textContent);
    await page.tap("#fav");
    await toastSays("Favorites");
    result.favLit = await page.$eval("#fav", e => e.classList.contains("lit"));
    await page.tap("#lists");
    await dlgShown("pick");
    await page.locator("#plist .prow.add").tap();
    await dlgShown("dlg");
    await page.fill("#dinput", "Speedrun");
    await page.tap("#dok");
    await toastSays("Made Speedrun");
    await page.waitForFunction(() => [...document.querySelectorAll("#plist .prow.in .pn")].some(x => x.textContent === "Speedrun"),
      null, { timeout: 20000 });
    result.picker = await page.$$eval("#plist .prow", r => r.map(x => (x.classList.contains("in") ? "[x] " : "[ ] ") + x.textContent));
    await shot("remote_7_lists.png");
    await page.tap("#pdone");
    await page.click("#shade", { position: { x: 30, y: 30 } });
    await page.waitForFunction(() => !document.body.classList.contains("sheet"), null, { timeout: 10000 });
    // The card: its name, the nickname, then its series / game; a star for a favorite.
    await page.waitForFunction(() => document.querySelector(".card .nk"), null, { timeout: 10000 });
    result.cardLines = await page.evaluate(() => {
      const c = [...document.querySelectorAll(".card")].find(x => x.querySelector(".nk"));
      return c ? { lines: [...c.querySelector(".meta").children].map(e => e.className + ": " + e.textContent),
                   star: c.classList.contains("fav") } : null;
    });
    await shot("remote_9_nickname.png");
    await page.fill("#q", "sir chop");  // the search finds nicknames too
    await page.waitForTimeout(500);
    result.nickSearch = await page.$$eval(".card .n", n => n.map(x => x.textContent));
    await page.fill("#q", "");
    await page.waitForTimeout(400);
    // The Favorites tab.
    await page.locator(".tab", { hasText: "Favorites" }).tap();
    await page.waitForFunction(() => document.querySelector("#listbar").classList.contains("shown") &&
      document.querySelector("#status").style.display === "none", null, { timeout: 30000 });
    result.favTab = await page.$$eval(".card .n", n => n.map(x => x.textContent));
    result.favBar = await page.$eval("#listtitle", e => e.textContent);
    await shot("remote_8_favorites.png");
    // Your list: rename it, then delete it.
    await page.locator(".tab", { hasText: "Speedrun" }).tap();
    await page.waitForFunction(() => /Speedrun/.test(document.querySelector("#listtitle").textContent) &&
      document.querySelectorAll(".card").length === 1, null, { timeout: 30000 });
    await page.tap("#lrename");
    await dlgShown("dlg");
    await page.fill("#dinput", "Any% Run");
    await page.tap("#dok");
    await toastSays("now called");
    await page.waitForFunction(() => [...document.querySelectorAll(".tab")].some(t => t.textContent === "Any% Run") &&
      document.querySelector(".tab.active").textContent === "Any% Run", null, { timeout: 20000 });
    result.renamed = true;
    await page.tap("#ldelete");
    await dlgShown("dlg");
    await page.tap("#dok");
    await toastSays("Deleted");
    await page.waitForFunction(() => ![...document.querySelectorAll(".tab")].some(t => t.textContent === "Any% Run") &&
      document.querySelector(".tab.active").textContent === "All", null, { timeout: 20000 });
    result.deleted = true;
    // "+ New list": made, and shown straight away (empty).
    await page.tap(".tab.new");
    await dlgShown("dlg");
    await page.fill("#dinput", "Raid Team");
    await page.tap("#dok");
    await toastSays("Made the list Raid Team");
    await page.waitForFunction(() => document.querySelector(".tab.active").textContent === "Raid Team" &&
      /Nothing in this list yet/.test(document.querySelector("#status").textContent), null, { timeout: 20000 });
    result.newTab = await page.$eval("#status", e => e.textContent);
    await page.tap("#ldelete");
    await dlgShown("dlg");
    await page.tap("#dok");
    await toastSays("Deleted");
    await page.waitForFunction(() => document.querySelector(".tab.active").textContent === "All" &&
      document.querySelectorAll(".card").length > 50, null, { timeout: 30000 });

    // Scrolled down: the search and tabs stay, with how many are on the rift.
    await page.evaluate(() => window.scrollTo(0, 1600));
    await page.waitForTimeout(1500);
    await shot("remote_6_scrolled.png");
    result.mini = await page.$eval("#mini", e => getComputedStyle(e).display !== "none" ? e.textContent : "");

    // ---- A Wi-Fi blip: every connection cut, nothing gets through for 4 s ----
    const eventsBefore = requests.events;
    fs.writeFileSync(process.env.XBOX_BLIP_FILE || path.join(dir, "blip"), "4");
    await page.waitForTimeout(2500);
    result.duringBlip = await page.$eval("#pilltext", e => e.textContent);
    await page.waitForTimeout(1700);  // the Wi-Fi is back
    t = Date.now();
    await elsewhere("add", hex);
    await waitChip("Hex", true, 30000);
    result.timings.afterBlipMs = Date.now() - t;
    result.streamReopened = requests.events > eventsBefore;
    await elsewhere("remove", hex);
    await waitChip("Hex", false, 20000);
    result.afterBlip = await page.$eval("#pilltext", e => e.textContent);
    result.rift = await chips();

    // My Picks: the Trap Team roster first.
    await page.locator(".tab", { hasText: "My Picks" }).tap();
    await page.waitForFunction(() => document.querySelector(".sec .t") &&
      /Roster/.test(document.querySelector(".sec .t").textContent), null, { timeout: 120000 });
    await page.evaluate(() => window.scrollTo(0, 0));
    await page.waitForTimeout(2500);
    await shot("remote_5_picks.png");
    result.picksSections = await page.$$eval(".sec .t", t => t.slice(0, 3).map(x => x.textContent));
  } catch (e) {
    result.errors.push("test: " + String(e));
    await shot("remote_error.png").catch(() => {});
  }
  result.requests = requests;
  fs.writeFileSync(out, JSON.stringify(result, null, 1));
  console.log(JSON.stringify(result));
  await browser.close();
})();
