// SPDX-License-Identifier: MIT
//
// Copyright (c) 2026 DSHA contributors
//
// Drives the DS Dashboard panel in Chromium with a mouse, like a user would.
// Usage: node tests/panel/test_panel.js [screenshot.png]

const path = require("path");
const http = require("http");
const fs = require("fs");
const { chromium } = require("playwright");

const ROOT = path.resolve(__dirname, "../..");
let failed = 0;
function check(cond, msg) {
  if (cond) {
    console.log(`  ok: ${msg}`);
  } else {
    failed++;
    console.log(`  FAIL: ${msg}`);
  }
}

// ES modules need to be served over HTTP
const server = http.createServer((req, res) => {
  const file = path.join(ROOT, decodeURIComponent(req.url.split("?")[0]));
  if (!file.startsWith(ROOT) || !fs.existsSync(file)) {
    res.writeHead(404);
    res.end();
    return;
  }
  const type = file.endsWith(".js") ? "text/javascript" : "text/html";
  res.writeHead(200, { "Content-Type": type });
  fs.createReadStream(file).pipe(res);
});

(async () => {
  await new Promise((r) => server.listen(0, "127.0.0.1", r));
  const port = server.address().port;
  const browser = await chromium.launch();
  const page = await browser.newPage({ viewport: { width: 1100, height: 760 } });
  const errors = [];
  page.on("pageerror", (e) => errors.push(e.message));
  page.on("console", (m) => { if (m.type() === "error") errors.push(m.text()); });

  await page.goto(`http://127.0.0.1:${port}/tests/panel/index.html`);
  const panel = page.locator("dsha-panel");
  await panel.locator(".slot").first().waitFor();

  const slot = (i) => panel.locator(`.slot[data-slot="${i}"]`);
  const entity = (id) => panel.locator(`.entity[data-value="${id}"]`);
  const layout = () => page.evaluate(() => window.panel._layout);

  async function drag(from, to) {
    await from.scrollIntoViewIfNeeded();
    await to.scrollIntoViewIfNeeded();
    const a = await from.boundingBox();
    const b = await to.boundingBox();
    await page.mouse.move(a.x + 15, a.y + a.height / 2);
    await page.mouse.down();
    await page.mouse.move(a.x + 40, a.y + a.height / 2, { steps: 4 });
    await page.mouse.move(b.x + b.width / 2, b.y + b.height / 2, { steps: 8 });
    await page.mouse.up();
  }

  check((await panel.locator(".entity").count()) === 12,
        "unsupported domains aren't listed");
  check(await panel.locator('[data-action="save"]').isDisabled(),
        "nothing to save at start");

  // Drag entities into slots
  await drag(entity("light.living_room"), slot(0));
  await drag(entity("automation.lights_at_sunset"), slot(3));
  let l = await layout();
  check(l.pages[0].tiles[0]?.entity_id === "light.living_room", "drag to slot 0");
  check(l.pages[0].tiles[3]?.entity_id === "automation.lights_at_sunset", "drag to slot 3");
  check(!(await panel.locator('[data-action="save"]').isDisabled()), "save enabled");

  // Swap two tiles
  await drag(slot(0), slot(3));
  l = await layout();
  check(l.pages[0].tiles[3]?.entity_id === "light.living_room"
        && l.pages[0].tiles[0]?.entity_id === "automation.lights_at_sunset",
        "drag a tile onto another swaps them");

  // Tap "+" fills the first empty slot
  await entity("switch.coffee_machine").locator("button.add").click();
  l = await layout();
  check(l.pages[0].tiles[1]?.entity_id === "switch.coffee_machine", "+ adds to first empty slot");

  // Edit a tile: rename and trigger action
  await slot(0).click();
  await panel.locator("#tile-name").fill("Sunset");
  await panel.locator("#tile-action").selectOption("trigger");
  l = await layout();
  check(l.pages[0].tiles[0].name === "Sunset", "rename tile");
  check(l.pages[0].tiles[0].action === "trigger", "trigger action for automations");
  check((await slot(0).locator(".tile-name").textContent()) === "Sunset", "new name shown");
  await panel.locator('[data-action="close-editor"]').click();

  // Drag a tile back to the list removes it
  await drag(slot(1), panel.locator(".entity-list"));
  l = await layout();
  check(l.pages[0].tiles[1] === null, "drag to the list removes the tile");

  // Pages
  await panel.locator('[data-action="add-page"]').click();
  await panel.locator("#page-name").fill("Climate");
  await panel.locator("#page-name").press("Enter");
  await drag(entity("climate.thermostat"), slot(0));
  l = await layout();
  check(l.pages.length === 2 && l.pages[1].name === "Climate", "add and rename a page");
  check(l.pages[1].tiles[0]?.entity_id === "climate.thermostat", "drag onto the new page");

  // Drag a tile onto a page tab moves it there
  await panel.locator('.tab[data-page-tab="0"]').click();
  await drag(slot(3), panel.locator('.tab[data-page-tab="1"]'));
  l = await layout();
  check(l.pages[1].tiles[1]?.entity_id === "light.living_room"
        && l.pages[0].tiles[3] === null, "drag onto a page tab moves the tile");

  // Search
  await panel.locator("#search").fill("good");
  check((await panel.locator(".entity").count()) === 1, "search filters the list");
  await panel.locator("#search").fill("");

  // Filter chips
  await panel.locator('.chip[data-value="light"]').click();
  check((await panel.locator(".entity").count()) === 3, "light filter");
  await panel.locator('.chip[data-value="all"]').click();

  // Page from an area (diagnostic entities skipped, device area used)
  await panel.locator("#area").selectOption("kitchen");
  l = await layout();
  const kitchen = l.pages.find((p) => p.name === "Kitchen");
  check(kitchen && kitchen.tiles.filter((t) => t).map((t) => t.entity_id).join()
        === "light.kitchen,media_player.kitchen_speaker", "page from an area");

  // Move a page
  const before = l.pages.map((p) => p.name).join();
  await panel.locator('[data-action="page-left"]').click();
  l = await layout();
  check(l.pages.map((p) => p.name).join() !== before, "move a page");

  // Title and save
  await panel.locator("#title").fill("My DS");
  await panel.locator('[data-action="save"]').click();
  await page.waitForFunction(() => window.saved.length === 1);
  const saved = await page.evaluate(() => window.saved[0]);
  check(saved.title === "My DS", "title saved");
  check(saved.pages.length === 3 && saved.pages.every((p) => p.tiles.length === 8),
        "saved layout has 3 pages of 8 slots");
  check(!("revision" in saved), "revision isn't sent back");
  await page.waitForTimeout(100);
  check(await panel.locator('[data-action="save"]').isDisabled(), "saved state");
  check((await panel.locator(".status").textContent()).includes("START"), "save message");

  // State updates don't break the editor
  await page.evaluate(() => {
    window.hass.states["light.kitchen"].state = "off";
    window.panel.hass = { ...window.hass };
  });
  await page.waitForTimeout(600);

  // Screenshot for the docs: a nice layout
  if (process.argv[2]) {
    await panel.locator('.tab[data-page-tab="0"]').click();
    await slot(5).click().catch(() => {});
    await drag(entity("scene.movie_night"), slot(4));
    await drag(entity("lock.front_door"), slot(5));
    await drag(entity("cover.garage"), slot(6));
    await drag(entity("sensor.outside_temperature"), slot(7));
    await drag(entity("light.hallway"), slot(2));
    await slot(0).click();
    await page.screenshot({ path: process.argv[2] });
  }

  check(errors.length === 0, `no errors in the console ${errors.join(" | ")}`);

  // Phone size: everything stacks in one column and still works
  await page.setViewportSize({ width: 390, height: 800 });
  await page.reload();
  await panel.locator(".slot").first().waitFor();
  await entity("light.hallway").locator("button.add").click();
  l = await layout();
  check(l.pages[0].tiles[0]?.entity_id === "light.hallway", "works at phone size");

  await browser.close();
  server.close();
  console.log(`test_panel: ${failed} failed`);
  process.exit(failed ? 1 : 0);
})().catch((e) => {
  console.error(e);
  process.exit(1);
});
