#!/usr/bin/env node
// Smoke test for the web build (brief 09). Needs the Chromium that comes with
// Playwright (`require('playwright')`; do not run `playwright install`) and a built
// build-wasm/ (see README.md, "Web build").
//
//   node tests/web/smoke.js [build-wasm-dir]        (default: build-wasm)
//   BITRIOT_SMOKE_SHOTS=dir node tests/web/smoke.js  (screenshots, default <build>/smoke)
//
// It serves the build directory itself, so nothing else needs to be running. Five
// scenarios, each in a fresh browser:
//   1. the menus with the mouse, a round with a keyboard player (keys, ESC), then a
//      CPU-only round (GPU-less WebGL through SwiftShader), with sound;
//   2. the ?autostart=ai4 address (the same round, started from the address);
//   3. no WebGL at all, so the game falls back to SDL's software renderer;
//   4. a gamepad that appears after the page loaded (a fake one: the Gamepad API
//      hides pads until a button is pressed), driving the menu and pressing START
//      in a round, which asks the pad to rumble and must fail quietly;
//   5. Exit, which tears the game down and stops the main loop.
// What it checks: the page screenshot (not canvas read-back, which a WebGL canvas
// answers with blank unless preserveDrawingBuffer is set) is not blank and changes
// from the menu to the round, the simulation's tick count advances, nothing logs an
// error, and the audio context is running with sounds reaching the mixer.
// Exit status 0 when every check passed.
'use strict';

const http = require('http');
const fs = require('fs');
const path = require('path');
const { chromium } = require('playwright');

const buildDir = path.resolve(process.argv[2] || 'build-wasm');
const shotDir = path.resolve(process.env.BITRIOT_SMOKE_SHOTS || path.join(buildDir, 'smoke'));
fs.mkdirSync(shotDir, { recursive: true });

const MIME = {
  '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm',
  '.data': 'application/octet-stream', '.png': 'image/png', '.css': 'text/css',
};

function serve(dir) {
  const server = http.createServer((req, res) => {
    const rel = decodeURIComponent(new URL(req.url, 'http://x').pathname).replace(/^\/+/, '') || 'index.html';
    const file = path.join(dir, rel);
    if (!file.startsWith(dir + path.sep) || !fs.existsSync(file) || fs.statSync(file).isDirectory()) {
      res.writeHead(404); res.end('not found'); return;
    }
    res.writeHead(200, { 'Content-Type': MIME[path.extname(file)] || 'application/octet-stream' });
    fs.createReadStream(file).pipe(res);
  });
  return new Promise((resolve) => server.listen(0, '127.0.0.1', () => resolve({
    url: 'http://127.0.0.1:' + server.address().port + '/index.html',
    close: () => server.close(),
  })));
}

let failures = 0;
let checks = 0;
function check(ok, what, detail) {
  checks++;
  if (!ok) { failures++; }
  console.log((ok ? '  ok   ' : '  FAIL ') + what + (detail !== undefined ? '  (' + detail + ')' : ''));
}

const GL_ARGS = ['--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist'];
const NO_GL_ARGS = ['--disable-3d-apis', '--disable-gpu'];

// A page with its console watched. Anything the game or the browser reports as an
// error fails the scenario: a page error, console.error, a line the game logged at
// ERROR level (they arrive as console warnings, from stderr), a wasm trap.
async function openPage(browser, name, url, initScript) {
  const page = await browser.newPage({ viewport: { width: 1100, height: 760 }, colorScheme: 'dark' });
  const problems = [];
  const lines = [];
  page.on('pageerror', (e) => problems.push('pageerror: ' + e.message));
  page.on('console', (m) => {
    const text = m.text();
    lines.push('[' + m.type() + '] ' + text);
    if (m.type() === 'error') { problems.push('console.error: ' + text); }
    else if (/^ERROR:|Aborted\(|RuntimeError|unreachable|Uncaught/.test(text)) { problems.push('log: ' + text); }
  });
  // Count the audio contexts the game opens, so the test can look at their state.
  await page.addInitScript(() => {
    const Original = window.AudioContext || window.webkitAudioContext;
    window.__audioContexts = [];
    if (Original) {
      window.AudioContext = class extends Original {
        constructor(...args) { super(...args); window.__audioContexts.push(this); }
      };
    }
  });
  if (initScript) { await page.addInitScript(initScript); }
  await page.goto(url);
  page.__problems = problems;
  page.__lines = lines;
  page.__name = name;
  return page;
}

const call = (page, fn, ...args) => page.evaluate(([f, a]) => window.Module['_' + f](...a), [fn, args]);

async function waitReady(page) {
  await page.waitForFunction(() => window.bitriotReady === true, null, { timeout: 90000 });
}

async function clickStart(page) {
  await page.click('#start');
  await page.waitForFunction(() => window.bitriotStarted === true && window.Module._bitriot_frames() > 5, null, { timeout: 60000 });
}

async function waitTicks(page, n, timeoutMs) {
  const start = await call(page, 'bitriot_tick');
  await page.waitForFunction(([s, k]) => window.Module._bitriot_tick() >= s + k, [start, n], { timeout: timeoutMs });
  return (await call(page, 'bitriot_tick')) - start;
}

async function shot(page, file) {
  const png = await page.screenshot({ path: path.join(shotDir, file) });
  return png.toString('base64');
}

// Decodes a screenshot in the page and answers: how many distinct colours (at 4 bits
// a channel) it holds and what fraction of pixels is the most common one.
const inspect = (page, b64) => page.evaluate(async (data) => {
  const bitmap = await createImageBitmap(await (await fetch('data:image/png;base64,' + data)).blob());
  const c = document.createElement('canvas'); c.width = bitmap.width; c.height = bitmap.height;
  const g = c.getContext('2d'); g.drawImage(bitmap, 0, 0);
  const px = g.getImageData(0, 0, c.width, c.height).data;
  const counts = new Map();
  for (let i = 0; i < px.length; i += 4) {
    const key = (px[i] >> 4) << 8 | (px[i + 1] >> 4) << 4 | (px[i + 2] >> 4);
    counts.set(key, (counts.get(key) || 0) + 1);
  }
  let top = 0; counts.forEach((v) => { if (v > top) top = v; });
  return { colours: counts.size, dominant: top / (px.length / 4) };
}, b64);

// What fraction of pixels differ noticeably between two screenshots of one size.
const differ = (page, a, b) => page.evaluate(async ([x, y]) => {
  const load = async (d) => {
    const bm = await createImageBitmap(await (await fetch('data:image/png;base64,' + d)).blob());
    const c = document.createElement('canvas'); c.width = bm.width; c.height = bm.height;
    const g = c.getContext('2d'); g.drawImage(bm, 0, 0); return g.getImageData(0, 0, c.width, c.height).data;
  };
  const p = await load(x), q = await load(y);
  if (p.length !== q.length) { return 1; }
  let n = 0;
  for (let i = 0; i < p.length; i += 4) {
    if (Math.abs(p[i] - q[i]) + Math.abs(p[i + 1] - q[i + 1]) + Math.abs(p[i + 2] - q[i + 2]) > 48) { n++; }
  }
  return n / (p.length / 4);
}, [a, b]);

async function notBlank(page, b64, what) {
  const r = await inspect(page, b64);
  check(r.colours >= 24 && r.dominant < 0.9, what + ' is not blank', r.colours + ' colours, ' + Math.round(r.dominant * 100) + '% the commonest');
}

async function finish(page, browser) {
  const fatal = await page.evaluate(() => window.bitriotFatalMessage || null);
  check(fatal === null, page.__name + ': the game reported no fatal error', fatal);
  check(page.__problems.length === 0, page.__name + ': no page errors or logged errors', page.__problems.slice(0, 3).join(' | '));
  fs.writeFileSync(path.join(shotDir, page.__name + '.log'), page.__lines.join('\n') + '\n');
  await page.close();
  await browser.close();
}

async function audioState(page) {
  return page.evaluate(async () => {
    const ctx = window.__audioContexts[0];
    if (!ctx) { return null; }
    const t0 = ctx.currentTime;
    await new Promise((r) => setTimeout(r, 400));
    return { state: ctx.state, advanced: ctx.currentTime > t0 };
  });
}

// ---------------------------------------------------------------------------
async function scenarioMenuAndRound(server) {
  console.log('1. menus and a CPU-only round');
  const browser = await chromium.launch({ args: GL_ARGS });
  const page = await openPage(browser, 'menu-and-round', server.url);
  await waitReady(page);
  const overlay = await page.evaluate(() => !document.getElementById('start').hidden);
  check(overlay, 'the page offers "Click to start" once loaded');
  await clickStart(page);
  await page.waitForTimeout(800);
  const menu = await shot(page, 'menu.png');
  await notBlank(page, menu, 'the main menu');
  check((await call(page, 'bitriot_game_state')) === 1, 'the game is in the menu (MENU_RUNNING)');

  // Mouse: click "Play" (the first entry, 36% down the canvas) and the player-select page appears.
  const box = await page.locator('#canvas').boundingBox();
  await page.mouse.click(box.x + box.width * 0.5, box.y + box.height * 0.36);
  await page.waitForTimeout(800);
  const select = await shot(page, 'player-select.png');
  check((await differ(page, menu, select)) > 0.02, 'a mouse click on Play opens the player-select page');

  // Start Game (the top entry of the page) with the defaults: Player 1 on the
  // keyboard, three CPU players.
  await page.mouse.click(box.x + box.width * 0.5, box.y + box.height * 0.065);
  await page.waitForFunction(() => window.Module._bitriot_game_state() === 2, null, { timeout: 15000 })
    .then(() => check(true, 'Start Game starts a round with a keyboard player'), () => check(false, 'Start Game starts a round with a keyboard player'));
  await waitTicks(page, 30, 30000);
  // Player 1's keys: W A S D, left Ctrl (weapon), left Shift (egg). The page must not
  // see them as shortcuts, and the round must go on.
  for (const key of ['KeyD', 'KeyS', 'KeyA', 'KeyW']) {
    await page.keyboard.down(key); await page.waitForTimeout(250); await page.keyboard.up(key);
  }
  await page.keyboard.down('ShiftLeft'); await page.waitForTimeout(100); await page.keyboard.up('ShiftLeft');
  const keyed = await waitTicks(page, 30, 30000);
  check(keyed >= 30, 'the round carries on while keys are pressed', keyed);
  // ESC leaves the round for the scoreboard, which is a menu again.
  await page.keyboard.press('Escape');
  await page.waitForFunction(() => window.Module._bitriot_game_state() === 1, null, { timeout: 15000 })
    .then(() => check(true, 'ESC ends the round (the keyboard reaches the game)'), () => check(false, 'ESC ends the round (the keyboard reaches the game)'));

  check((await call(page, 'bitriot_start_ai_match', 4)) === 1, 'a four-CPU round starts through the test hook');
  const advanced = await waitTicks(page, 90, 30000);
  check(advanced >= 90, 'the simulation advanced 90 ticks', advanced);
  const round = await shot(page, 'round.png');
  await notBlank(page, round, 'the round');
  check((await differ(page, menu, round)) > 0.2, 'the round looks different from the menu');

  // Sound. The click that started the game is what lets the page make noise.
  const audio = await audioState(page);
  check(audio && audio.state === 'running' && audio.advanced, 'the audio context is running after the click', JSON.stringify(audio));
  await page.waitForFunction(() => window.Module._bitriot_sounds_played() > 0, null, { timeout: 30000 })
    .then(() => check(true, 'sounds reach the mixer'), () => check(false, 'sounds reach the mixer'));

  // The tick rate: 30 a second, whatever the display's refresh rate.
  const t0 = await call(page, 'bitriot_tick');
  await page.waitForTimeout(2000);
  const rate = ((await call(page, 'bitriot_tick')) - t0) / 2;
  check(rate > 20 && rate < 40, 'about 30 ticks a second', rate.toFixed(1));
  await finish(page, browser);
}

async function scenarioAutostart(server) {
  console.log('2. ?autostart=ai4');
  const browser = await chromium.launch({ args: GL_ARGS });
  const page = await openPage(browser, 'autostart', server.url + '?autostart=ai4');
  await waitReady(page);
  await clickStart(page);
  const advanced = await waitTicks(page, 60, 30000);
  check(advanced >= 60, 'a round started from the address and advanced 60 ticks', advanced);
  check((await call(page, 'bitriot_game_state')) === 2, 'the game is running a round (GAME_RUNNING)');
  await notBlank(page, await shot(page, 'autostart.png'), 'the autostarted round');
  await finish(page, browser);
}

async function scenarioSoftwareRenderer(server) {
  console.log('3. no WebGL: the software renderer');
  const browser = await chromium.launch({ args: NO_GL_ARGS });
  const page = await openPage(browser, 'software-renderer', server.url + '?autostart=ai2');
  const gl = await page.evaluate(() => !!document.createElement('canvas').getContext('webgl'));
  check(!gl, 'this browser really has no WebGL');
  await waitReady(page);
  await clickStart(page);
  const advanced = await waitTicks(page, 60, 60000);
  check(advanced >= 60, 'a round advanced 60 ticks without WebGL', advanced);
  await notBlank(page, await shot(page, 'software.png'), 'the round drawn by the software renderer');
  await finish(page, browser);
}

async function scenarioQuit(server) {
  console.log('5. Exit quits cleanly');
  const browser = await chromium.launch({ args: GL_ARGS });
  const page = await openPage(browser, 'quit', server.url);
  await waitReady(page);
  await clickStart(page);
  await page.waitForTimeout(500);
  const box = await page.locator('#canvas').boundingBox();
  await page.mouse.click(box.x + box.width * 0.5, box.y + box.height * 0.61); // Exit
  await page.waitForFunction(() => window.bitriotQuit === true, null, { timeout: 15000 })
    .then(() => check(true, 'Exit quits and tells the page'), () => check(false, 'Exit quits and tells the page'));
  const frames = await call(page, 'bitriot_frames');
  await page.waitForTimeout(500);
  check((await call(page, 'bitriot_frames')) === frames, 'the main loop has stopped');
  const shown = await page.evaluate(() => !document.getElementById('overlay').hidden);
  check(shown, 'the page says the game has quit');
  await finish(page, browser);
}

// A pad that looks like an ordinary standard-layout one to the Gamepad API.
const FAKE_PAD = () => {
  const pad = {
    id: 'Fake Pad (STANDARD GAMEPAD Vendor: 045e Product: 028e)', index: 0, connected: true,
    mapping: 'standard', timestamp: 0, axes: [0, 0, 0, 0], vibrationActuator: null,
    buttons: Array.from({ length: 17 }, () => ({ pressed: false, touched: false, value: 0 })),
  };
  let present = false;
  navigator.getGamepads = () => [present ? pad : null, null, null, null];
  window.__connectPad = () => {
    present = true;
    const e = new Event('gamepadconnected'); e.gamepad = pad; window.dispatchEvent(e);
  };
  window.__setButton = (i, down) => {
    pad.buttons[i].pressed = down; pad.buttons[i].value = down ? 1 : 0; pad.timestamp = performance.now();
  };
};

async function tap(page, button) {
  await page.evaluate((b) => window.__setButton(b, true), button);
  await page.waitForTimeout(150);
  await page.evaluate((b) => window.__setButton(b, false), button);
  await page.waitForTimeout(150);
}

async function scenarioGamepad(server) {
  console.log('4. a gamepad that appears later');
  const browser = await chromium.launch({ args: GL_ARGS });
  const page = await openPage(browser, 'gamepad', server.url, FAKE_PAD);
  await waitReady(page);
  await clickStart(page);
  await page.waitForTimeout(500);
  check((await call(page, 'bitriot_joystick_count')) === 0, 'no pad is visible before one is connected');
  await page.evaluate(() => window.__connectPad());
  await page.waitForFunction(() => window.Module._bitriot_joystick_count() === 1, null, { timeout: 10000 })
    .then(() => check(true, 'the pad shows up once connected'), () => check(false, 'the pad shows up once connected'));

  // A on the main menu presses Play: the player-select page opens.
  const before = await shot(page, 'pad-menu.png');
  await tap(page, 0);
  await page.waitForTimeout(600);
  const after = await shot(page, 'pad-after-a.png');
  check((await differ(page, before, after)) > 0.02, 'the A button on the pad opens the player-select page');

  // START in a round: an unbound pad opens the join selector and is buzzed (rumble),
  // which this pad cannot do; that must be silent, and the round must carry on.
  check((await call(page, 'bitriot_start_ai_match', 4)) === 1, 'a four-CPU round starts');
  await waitTicks(page, 20, 30000);
  await tap(page, 9); // START
  await tap(page, 0); // A: confirm
  const advanced = await waitTicks(page, 30, 30000);
  check(advanced >= 30, 'the round carries on after START and A on the pad', advanced);
  await shot(page, 'pad-round.png');
  await finish(page, browser);
}

(async () => {
  for (const f of ['index.html', 'BitRiot.js', 'BitRiot.wasm', 'BitRiot.data']) {
    if (!fs.existsSync(path.join(buildDir, f))) {
      console.error('Missing ' + path.join(buildDir, f) + ': build the web version first (see README.md).');
      process.exit(2);
    }
  }
  const server = await serve(buildDir);
  console.log('serving ' + buildDir + ' at ' + server.url + '; screenshots in ' + shotDir);
  try {
    await scenarioMenuAndRound(server);
    await scenarioAutostart(server);
    await scenarioSoftwareRenderer(server);
    await scenarioGamepad(server);
    await scenarioQuit(server);
  } catch (e) {
    failures++;
    console.log('  FAIL the test stopped: ' + (e && e.stack || e));
  } finally {
    server.close();
  }
  console.log((failures === 0 ? 'PASS' : 'FAIL') + ': ' + (checks - failures) + ' of ' + checks + ' checks passed');
  process.exit(failures === 0 ? 0 : 1);
})();
