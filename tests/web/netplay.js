#!/usr/bin/env node
// Browser netplay tests (brief 12): browsers and a native player in one match through a vnet relay.
// Needs the Chromium that comes with Playwright (`require('playwright')`; do not run `playwright install`), the web build
// (build-wasm/), the native build's bitriot_headless, and Go (the relay is built from ext/vnet/relay unless --relay-bin is given).
//
//   node tests/web/netplay.js [build-wasm-dir] [--headless PATH] [--relay-bin PATH] [--only NAME[,NAME...]]
//
//   defaults: build-wasm, build/bitriot_headless (or build-gcc/...), the relay built into <build-wasm-dir>/vnet-relay.
//   BITRIOT_NETPLAY_SHOTS=dir   screenshots and logs (default <build-wasm-dir>/netplay)
//
// Everything runs on loopback: a relay (WebSocket and WebTransport with its development certificate), a native
// `bitriot_headless host --relay` that opens a room, and Chromium pages that join it. What each scenario checks:
//
//   golden       the golden match record, made natively, replayed by the game's own wasm build through a page hook: every
//                checkpoint hash (one per tick, 2768 ticks) agrees with native, and the final hash is the one native prints
//   ws           the native host and two browsers play a whole round over the WebSocket, with scripted key presses in the
//                browsers: every machine reports the same checkpoint hash at every checkpoint and the same winner and final hash
//   wt           the same over WebTransport datagrams
//   ws-impaired  the same over the WebSocket with simulated latency, jitter, loss and duplication on every machine
//   wt-impaired  the same over WebTransport
//   hidden       two browser tabs that are hidden (document.hidden): they are stepped from network events and finish an AI-heavy
//                round with the same hashes; the test says how many ticks a second that gave
//   background   a hidden tab with the browser's throttling imposed (no animation frames, timers held to 1 Hz): measures what each way
//                of driving it gives; see doco/netplay/12-browser-netplay.md
//   frozen       a tab that does not run for 6 s (a blocked main thread, as a freeze or a laptop lid looks to the game): its peers say the machine is away, and it is back
//                in the match when it thaws
//   host-away    a browser HOSTING a room whose tab is frozen: the peers see "the host is away", then play on
//   ui           a browser joins by room code through the real menus with the keyboard (Online game, Join by room code, typing
//                the code), the settings come back after a reload, and the room panel under the canvas shows the code
//   errors       a room that does not exist, and a relay that is not there: the error pages (the session's own words)
//
// Exit status 0 when every check passed.
'use strict';

const http = require('http');
const fs = require('fs');
const os = require('os');
const path = require('path');
const readline = require('readline');
const { spawn, spawnSync } = require('child_process');
const { chromium } = require('playwright');

// ---- command line -----------------------------------------------------------------------------------------------

const argv = process.argv.slice(2);
function option(name, def) {
  const i = argv.indexOf('--' + name);
  return i >= 0 ? argv[i + 1] : def;
}
const positional = argv.filter((a, i) => !a.startsWith('--') && !(i > 0 && argv[i - 1].startsWith('--')));
const buildDir = path.resolve(positional[0] || 'build-wasm');
const repoDir = path.resolve(__dirname, '..', '..');
const shotDir = path.resolve(process.env.BITRIOT_NETPLAY_SHOTS || path.join(buildDir, 'netplay'));
fs.mkdirSync(shotDir, { recursive: true });
const only = option('only', '') ? option('only', '').split(',') : null;

function firstExisting(list) {
  for (const f of list) if (f && fs.existsSync(f)) return path.resolve(f);
  return null;
}
const headlessBin = option('headless') ? path.resolve(option('headless')) :
  firstExisting([path.join(repoDir, 'build', 'bitriot_headless'), path.join(repoDir, 'build-gcc', 'bitriot_headless'),
                 path.join(repoDir, 'build-clang', 'bitriot_headless')]);
if (!headlessBin) { console.error('No bitriot_headless found: build the native game first, or pass --headless PATH.'); process.exit(2); }

// ---- results ------------------------------------------------------------------------------------------------------

let failures = 0;
let checks = 0;
function check(ok, what, detail) {
  checks++;
  if (!ok) failures++;
  console.log((ok ? '  ok   ' : '  FAIL ') + what + (detail !== undefined ? '  (' + detail + ')' : ''));
}
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const withTimeout = (p, ms, what) => Promise.race([p, new Promise((_, rej) => setTimeout(() => rej(new Error('timeout waiting for ' + what)), ms))]);

// ---- the relay -------------------------------------------------------------------------------------------------------

function buildRelay() {
  const given = option('relay-bin');
  if (given) return path.resolve(given);
  const out = path.join(buildDir, 'vnet-relay');
  const src = path.join(repoDir, 'ext', 'vnet', 'relay');
  const r = spawnSync('go', ['build', '-o', out, './cmd/vnet-relay'], { cwd: src, stdio: 'inherit' });
  if (r.status !== 0) { console.error('go build of the relay failed'); process.exit(2); }
  return out;
}

async function startRelay(bin, extra = []) {
  const child = spawn(bin, ['-udp', '127.0.0.1:0', '-http', '127.0.0.1:0', '-wt', '127.0.0.1:0', '-dev-cert', ...extra],
    { stdio: ['ignore', 'pipe', 'inherit'], env: { PATH: process.env.PATH } });
  const rl = readline.createInterface({ input: child.stdout });
  const ready = new Promise((resolve, reject) => {
    rl.on('line', (line) => {
      const m = /^LISTEN udp=(\S+) http=(\S+) stats=(\S*) wt=(\S*) wt_cert_sha256=(\S*)/.exec(line);
      if (m) resolve({ child, udp: m[1], http: m[2], wt: m[4], certHash: m[5] });
    });
    child.on('exit', (c) => reject(new Error('the relay exited early: ' + c)));
  });
  const r = await withTimeout(ready, 15000, 'the relay to start');
  r.udpPort = r.udp.split(':').pop();
  r.httpPort = r.http.split(':').pop();
  r.wtPort = r.wt.split(':').pop();
  r.stop = () => { child.kill('SIGTERM'); };
  return r;
}

// ---- a native bitriot_headless -------------------------------------------------------------------------------------------

const activeNatives = [];
function startNative(args, env = {}) {
  const child = spawn(headlessBin, args, { stdio: ['ignore', 'pipe', 'pipe'], env: { PATH: process.env.PATH, ...env } });
  const n = { child, out: [], err: [], code: null, args };
  activeNatives.push(n);
  readline.createInterface({ input: child.stdout }).on('line', (l) => n.out.push(l));
  readline.createInterface({ input: child.stderr }).on('line', (l) => n.err.push(l));
  n.exited = new Promise((resolve) => child.on('exit', (c) => { n.code = c; resolve(c); }));
  return n;
}

async function waitFile(file, ms) {
  const t0 = Date.now();
  while (Date.now() - t0 < ms) {
    if (fs.existsSync(file)) { const t = fs.readFileSync(file, 'utf8').trim(); if (t) return t; }
    await sleep(100);
  }
  throw new Error('timeout waiting for ' + file);
}

// ---- serving the web build ---------------------------------------------------------------------------------------------------

const MIME = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm', '.data': 'application/octet-stream',
               '.png': 'image/png', '.css': 'text/css' };
function serve(dir) {
  const server = http.createServer((req, res) => {
    const rel = decodeURIComponent(new URL(req.url, 'http://x').pathname).replace(/^\/+/, '') || 'index.html';
    const file = path.join(dir, rel);
    if (!file.startsWith(dir + path.sep) || !fs.existsSync(file) || fs.statSync(file).isDirectory()) { res.writeHead(404); res.end('not found'); return; }
    res.writeHead(200, { 'Content-Type': MIME[path.extname(file)] || 'application/octet-stream' });
    fs.createReadStream(file).pipe(res);
  });
  return new Promise((resolve) => server.listen(0, '127.0.0.1', () => resolve({
    port: server.address().port, close: () => server.close(),
  })));
}

// ---- pages ---------------------------------------------------------------------------------------------------------------------

const GL_ARGS = ['--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist'];
const openPages = [];

// A page of the game with its console watched. `query` is added to ?test=1. A page error, console.error or ERROR: line fails the
// scenario (page.__problems is checked by finish()).
async function openGame(context, name, server, query = {}) {
  const page = await context.newPage();
  const problems = [];
  const lines = [];
  page.on('pageerror', (e) => problems.push('pageerror: ' + e.message));
  page.on('console', (m) => {
    const text = m.text();
    lines.push('[' + m.type() + '] ' + text);
    if (m.type() === 'error' && !/status of 404/.test(text)) problems.push('console.error: ' + text);   // (no favicon)
    else if (/^ERROR:|Aborted\(|RuntimeError|unreachable|Uncaught/.test(text)) problems.push('log: ' + text);
  });
  const params = new URLSearchParams({ test: '1', ...query });
  await page.goto('http://127.0.0.1:' + server.port + '/index.html?' + params.toString());
  page.__problems = problems; page.__lines = lines; page.__name = name;
  openPages.push(page);
  return page;
}

const call = (page, fn, ...args) => page.evaluate(([f, a]) => window.Module['_' + f](...a), [fn, args]);
// A hook that answers with a C string.
const callString = (page, fn) => page.evaluate((f) => window.Module.UTF8ToString(window.Module['_' + f]()), fn);
const info = async (page) => JSON.parse(await callString(page, 'bitriot_net_info'));

async function startGame(page) {
  await page.waitForFunction(() => window.bitriotReady === true, null, { timeout: 120000 });
  await page.click('#start');
  await page.waitForFunction(() => window.bitriotStarted === true && window.Module._bitriot_frames() > 5, null, { timeout: 60000 });
}

async function netStart(page, host, name, room) {
  await page.evaluate(([n, r]) => { window.Module.bitriotTestArgs = { name: n, room: r }; }, [name, room || '']);
  const ok = await call(page, 'bitriot_net_start', host ? 1 : 0, 1);
  check(ok === 1, name + ': the game started a ' + (host ? 'room' : 'join'));
}

const netState = (page) => call(page, 'bitriot_net_state');   // 0 none 1 connecting 2 lobby 3 round 4 between 5 ended
async function waitNetState(page, states, ms, what) {
  await page.waitForFunction((s) => s.includes(window.Module._bitriot_net_state()), states, { timeout: ms })
    .catch(() => { throw new Error(page.__name + ': never reached net state ' + states + ' (' + what + '); in state ' + 'unknown'); });
}

// A page that was not made by openGame (a popup): watched the same way.
function adopt(page, name) {
  page.__problems = []; page.__lines = []; page.__name = name;
  page.on('pageerror', (e) => page.__problems.push('pageerror: ' + e.message));
  page.on('console', (m) => {
    const text = m.text();
    page.__lines.push('[' + m.type() + '] ' + text);
    if (m.type() === 'error' && !/status of 404/.test(text)) page.__problems.push('console.error: ' + text);
    else if (/^ERROR:|Aborted\(|RuntimeError|unreachable|Uncaught/.test(text)) page.__problems.push('log: ' + text);
  });
  openPages.push(page);
}

async function finish(page, { allowProblems = false } = {}) {
  const fatal = await page.evaluate(() => window.bitriotFatalMessage || null).catch(() => null);
  if (!allowProblems) {
    check(fatal === null, page.__name + ': no fatal error reported', fatal);
    check(page.__problems.length === 0, page.__name + ': no page errors or logged errors', page.__problems.slice(0, 3).join(' | '));
  }
  fs.writeFileSync(path.join(shotDir, page.__name + '.log'), page.__lines.join('\n') + '\n');
  const i = openPages.indexOf(page);
  if (i >= 0) openPages.splice(i, 1);
  await page.close().catch(() => {});
}

async function dumpOpenPages() {
  for (const n of activeNatives.splice(0)) {
    console.log('  (native ' + n.args[0] + ' exit ' + n.code + '; stderr: ' + n.err.slice(-6).join(' | ') + '; stdout tail: ' + n.out.slice(-3).join(' | ') + ')');
    try { n.child.kill('SIGTERM'); } catch (e) { /* gone */ }
  }
  for (const page of openPages.splice(0)) {
    try {
      fs.writeFileSync(path.join(shotDir, page.__name + '.log'), page.__lines.join('\n') + '\n');
      console.log('  (' + page.__name + ': ' + page.__problems.length + ' problem(s)' +
        (page.__problems.length ? ': ' + page.__problems.slice(0, 3).join(' | ') : '') + '; log in ' + path.join(shotDir, page.__name + '.log') + ')');
    } catch (e) { /* gone */ }
  }
}

// Scripted inputs: a deterministic walk of key presses (movement, bombs, an egg now and then), so the browsers are not idle.
function startKeyScript(page, seed) {
  let stop = false;
  const keys = ['KeyW', 'KeyA', 'KeyS', 'KeyD'];
  let x = seed * 2654435761 >>> 0;
  const rnd = () => { x = (x * 1664525 + 1013904223) >>> 0; return x / 4294967296; };
  const done = (async () => {
    let held = null;
    while (!stop) {
      try {
        if (held) { await page.keyboard.up(held); held = null; }
        const r = rnd();
        if (r < 0.7) { held = keys[Math.floor(rnd() * 4)]; await page.keyboard.down(held); }
        else if (r < 0.85) { await page.keyboard.down('ControlLeft'); await page.keyboard.press('KeyS'); await page.keyboard.up('ControlLeft'); }
        else { await page.keyboard.press('ShiftLeft'); }
        await sleep(150 + Math.floor(rnd() * 350));
      } catch (e) { return; }
    }
    if (held) await page.keyboard.up(held).catch(() => {});
  })();
  return { stop: async () => { stop = true; await done; } };
}

// What a machine says it played: the "checkpoint R T HASH" lines and the "round R: over ..." line of each round.
function parseLog(lines) {
  const cps = new Map();
  let over = null;
  for (const l of lines) {
    let m;
    if ((m = /^checkpoint (\d+) (\d+) (0x[0-9a-f]+)$/.exec(l))) cps.set(m[1] + ':' + m[2], m[3]);
    else if (/^round \d+: over at tick/.test(l)) over = l;
  }
  return { cps, over };
}
const parseBrowserLog = async (page) => parseLog((await callString(page, 'bitriot_net_log')).split('\n').filter(Boolean));

// Every machine played the same round: the same checkpoint at every tick (the checkpoints one machine has, another has too, with
// the same hash), the same last line. `machines` is [{name, log: {cps, over}}].
function compareMachines(machines, what, minCheckpoints) {
  const ref = machines[0];
  check(ref.log.cps.size >= minCheckpoints, what + ': ' + ref.name + ' reported at least ' + minCheckpoints + ' checkpoints', ref.log.cps.size);
  for (const m of machines.slice(1)) {
    let missing = 0, differing = 0, firstDiff = null;
    for (const [k, h] of ref.log.cps) {
      const other = m.log.cps.get(k);
      if (other === undefined) missing++;
      else if (other !== h) { differing++; if (!firstDiff) firstDiff = k + ' ' + h + ' vs ' + other; }
    }
    check(differing === 0, what + ': ' + m.name + ' agrees with ' + ref.name + ' on every checkpoint hash', differing ? 'first difference at ' + firstDiff : m.log.cps.size + ' checkpoints');
    check(missing === 0 && m.log.cps.size === ref.log.cps.size, what + ': ' + m.name + ' has the same checkpoints as ' + ref.name, 'missing ' + missing + ', ' + m.log.cps.size + ' vs ' + ref.log.cps.size);
    check(ref.log.over !== null && m.log.over === ref.log.over, what + ': ' + m.name + ' reports the same winner and final hash', ref.log.over + ' | ' + m.log.over);
  }
}

// ---- scenarios ------------------------------------------------------------------------------------------------------------------

// The golden record, replayed by the game's wasm build.
async function scenarioGolden(ctx) {
  console.log('golden: the golden match record replayed by the full game build (wasm) against native');
  const native = spawnSync(headlessBin, ['replay', path.join(repoDir, 'tests', 'data', 'golden-round.brmr')], { encoding: 'utf8' });
  const m = /replay: OK, (\d+) ticks, (\d+) checkpoints verified, final hash (0x[0-9a-f]+)/.exec(native.stdout);
  check(native.status === 0 && m !== null, 'native bitriot_headless replays the record', m ? m[0] : native.stdout);
  const page = await openGame(ctx.context(), 'golden', ctx.server);
  await startGame(page);
  const r = JSON.parse(await callString(page, 'bitriot_replay_golden'));
  console.log('  wasm: ' + JSON.stringify(r));
  check(r.status === 0, 'the game build replays the record with every checkpoint matching native', r.message || ('status ' + r.status + ', first mismatch at tick ' + r.mismatchTick + ': expected ' + r.expectedHash + ', got ' + r.actualHash));
  check(m !== null && r.ticks === +m[1] && r.checkpoints === +m[2], 'it played as many ticks and verified as many checkpoints as native', r.ticks + '/' + r.checkpoints);
  check(m !== null && r.finalHash === m[3], 'its final hash is native\'s', r.finalHash + ' vs ' + (m && m[3]));
  check(r.roundOver === true, 'the replayed round is over, as in the record');
  await finish(page);
}

// A native host and two browsers in one round through the relay. `opts`: transport 'ws' | 'wt', impair spec for browsers,
// nativeImpair (VNET_IMPAIR of the host), config, seed, nativeTickMs, hidden (the browsers' tabs are hidden), keys.
async function playRound(ctx, name, opts) {
  const relay = ctx.relay;
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'bitriot-np-'));
  const roomFile = path.join(dir, 'room');
  const config = opts.config || 'players=4,map=13x11,blocks=30';
  const host = startNative(['host', '--relay', relay.udp, '--room-file', roomFile, '--peers', '2', '--rounds', '1', '--seed', String(opts.seed || 3),
                            '--config', config, '--name', 'native', '--tick-ms', String(opts.nativeTickMs || 33), '--wait-secs', '150',
                            '--timeout-secs', '900', '--out', dir], opts.nativeImpair ? { VNET_IMPAIR: opts.nativeImpair } : {});
  const room = await waitFile(roomFile, 20000);
  check(/^[A-HJ-NP-Z2-9]{6}$/.test(room), name + ': the native host opened a room', room);

  const query = { relay: 'ws://127.0.0.1:' + relay.httpPort + '/vnet' };
  if (opts.transport === 'wt') { query.relay_wt = 'https://127.0.0.1:' + relay.wtPort + '/vnet'; query.relay_cert = relay.certHash; }
  if (opts.impair) query.impair = opts.impair;
  const pages = [];
  for (let i = 1; i <= 2; i++) {
    const p = await openGame(ctx.context(), name + '-browser' + i, ctx.server, { ...query, ...(opts.impair ? { impair: opts.impair + ',seed=' + (10 + i) } : {}) });
    p.__tag = 'browser' + i;
    pages.push(p);
  }
  for (const p of pages) await startGame(p);
  if (opts.hidden) for (const p of pages) await hidePage(p);
  for (let i = 0; i < 2; i++) await netStart(pages[i], false, 'web' + (i + 1), room);
  for (const p of pages) {
    await waitNetState(p, [2, 3], 40000, 'joined the lobby');
    const t = await callString(p, 'bitriot_net_transport');
    check(t === (opts.transport === 'wt' ? 'relay-wt' : 'relay-ws'), p.__name + ': the datagrams travel over ' + (opts.transport === 'wt' ? 'WebTransport' : 'the WebSocket'), t);
    // The host does not wait for "ready" (headless host); pressing it is what the button does, harmless.
  }
  for (const p of pages) await waitNetState(p, [3], 40000, 'the round starts');
  const scripts = opts.keys === false ? [] : pages.map((p, i) => startKeyScript(p, 5 + i));
  const t0 = Date.now();
  for (const p of pages) await waitNetState(p, [4, 5], opts.roundMs || 400000, 'the round ends');
  const seconds = (Date.now() - t0) / 1000;
  for (const s of scripts) await s.stop();
  const exitCode = await withTimeout(host.exited, 60000, 'the native host to finish');
  // The native host leaves when its rounds are done: a peer is on the scoreboard, or has been told the host left (HOST_CLOSED = 4).
  for (const p of pages) {
    const st = await netState(p), inf = await info(p);
    check(st === 4 || (st === 5 && inf.endReason === 4), p.__name + ': the round ended cleanly (scoreboard, or the host said it left)', 'state ' + st + ', end reason ' + inf.endReason);
  }
  check(exitCode === 0, name + ': the native host played the round and exited 0', exitCode + ' ' + host.err.slice(-3).join(' | '));
  const machines = [{ name: 'native', log: parseLog(host.out) }];
  for (const p of pages) machines.push({ name: p.__tag, log: await parseBrowserLog(p) });
  compareMachines(machines, name, 10);
  const winner = machines[0].log.over;
  console.log('  ' + name + ': ' + winner + ' (' + machines[0].log.cps.size + ' checkpoints, ' + seconds.toFixed(1) + ' s)');
  const infos = [];
  for (const p of pages) infos.push(await info(p));
  const result = { machines, seconds, infos, host };
  for (const p of pages) await finish(p);
  fs.rmSync(dir, { recursive: true, force: true });
  return result;
}

// Makes a page look hidden to the game, as a background tab does: document.hidden and visibilityState, and a visibilitychange event.
async function hidePage(page) {
  await page.evaluate(() => {
    Object.defineProperty(document, 'hidden', { configurable: true, get: () => true });
    Object.defineProperty(document, 'visibilityState', { configurable: true, get: () => 'hidden' });
    document.dispatchEvent(new Event('visibilitychange'));
  });
  await page.waitForFunction(() => window.Module._bitriot_is_hidden() === 1, null, { timeout: 5000 });
}

const scenarios = {};
scenarios.golden = scenarioGolden;
scenarios.ws = async (ctx) => { console.log('ws: native host + two browsers, a whole round over the WebSocket'); await playRound(ctx, 'ws', { transport: 'ws' }); };
scenarios.wt = async (ctx) => { console.log('wt: the same over WebTransport'); await playRound(ctx, 'wt', { transport: 'wt', seed: 4 }); };
scenarios['ws-impaired'] = async (ctx) => {
  console.log('ws-impaired: the same over the WebSocket with latency, jitter, loss and duplication on every machine');
  await playRound(ctx, 'ws-impaired', { transport: 'ws', seed: 5, impair: 'lat=40,jit=15,loss=4,dup=1,reorder=2', nativeImpair: 'lat=30,jit=10,loss=4,dup=1,seed=3' });
};
scenarios['wt-impaired'] = async (ctx) => {
  console.log('wt-impaired: the same over WebTransport with the same impairment');
  await playRound(ctx, 'wt-impaired', { transport: 'wt', seed: 6, impair: 'lat=40,jit=15,loss=4,dup=1,reorder=2', nativeImpair: 'lat=30,jit=10,loss=4,dup=1,seed=3' });
};

// ---- more scenarios: tabs that are hidden or frozen, the menus, the error pages ------------------------------------------------------

const LONG_CONFIG = 'players=6,map=17x19,blocks=55';   // an AI-heavy round (3000 to 4500 ticks)

// A native host that opens a room and waits for `peers` machines; resolves {host, room, dir}.
async function nativeHost(ctx, { peers, config, seed, tickMs, rounds, impair, delay }) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'bitriot-np-'));
  const roomFile = path.join(dir, 'room');
  const host = startNative(['host', '--relay', ctx.relay.udp, '--room-file', roomFile, '--peers', String(peers), '--rounds', String(rounds || 1),
                            '--seed', String(seed || 3), '--config', config || LONG_CONFIG, '--name', 'native', '--tick-ms', String(tickMs || 33),
                            '--wait-secs', '150', '--timeout-secs', '900', '--out', dir, ...(delay ? ['--delay', String(delay)] : [])], impair ? { VNET_IMPAIR: impair } : {});
  const room = await waitFile(roomFile, 20000);
  return { host, room, dir };
}
const wsQuery = (ctx, extra = {}) => ({ relay: 'ws://127.0.0.1:' + ctx.relay.httpPort + '/vnet', ...extra });
const tickOf = async (page) => (await info(page)).tick;
const stopNative = (n) => { try { n.child.kill('SIGTERM'); } catch (e) { /* gone */ } };

// Hidden by override (document.hidden and a visibilitychange): the game steps from network events and runs the whole AI-heavy
// round fast. The tabs are NOT really throttled here (the browser still delivers animation frames); "background" is the real thing.
scenarios.hidden = async (ctx) => {
  console.log('hidden: two tabs that report themselves hidden finish an AI-heavy round, stepped from network events');
  const r = await playRound(ctx, 'hidden', { transport: 'ws', seed: 2, config: LONG_CONFIG, hidden: true, keys: false, nativeTickMs: 4, roundMs: 300000 });
  const ticks = +/over at tick (\d+)/.exec(r.machines[0].log.over)[1];
  console.log('  hidden: ' + ticks + ' ticks in ' + r.seconds.toFixed(1) + ' s = ' + (ticks / r.seconds).toFixed(0) + ' ticks a second (nominal is 30)');
  check(ticks / r.seconds > 100, 'hidden tabs are not paced by a clock: they step whatever the host confirms (the native host at 4 ms a tick set the pace)', (ticks / r.seconds).toFixed(0));
};

// A background tab as Chromium treats one: no animation frames, and timers held to one run a second. Playwright cannot make a tab really
// hidden (it makes every page believe it is focused and visible, and a second tab hides nothing; tried in headed Chromium under Xvfb,
// both through Playwright and through a bare CDP connection), so the tab is made to look hidden to the game (document.hidden and
// visibilityState, as in "hidden") AND the two things the browser does to a hidden tab are imposed on it: requestAnimationFrame never calls
// back, and setTimeout/setInterval/emscripten_async_call (which is a setTimeout) wait at least a second. WebSocket and
// MessageChannel events are left alone, as in Chromium. The native host runs at the nominal pace; browser B is the hidden one and browser A
// is visible. Three ways of driving B: the game's default (network wake-ups plus a one-second timer), the one-second timer alone, and
// nothing (what the browser does by itself: no animation frames, so no stepping).
async function throttleLikeABackgroundTab(page) {
  await page.evaluate(() => {
    window.requestAnimationFrame = () => 0;
    const st = window.setTimeout.bind(window), si = window.setInterval.bind(window);
    window.setTimeout = (f, ms, ...a) => st(f, Math.max(+ms || 0, 1000), ...a);
    window.setInterval = (f, ms, ...a) => si(f, Math.max(+ms || 0, 1000), ...a);
  });
}

scenarios.background = async (ctx) => {
  console.log('background: a hidden tab with the browser\'s throttling imposed (what each way of driving it gives)');
  const results = {};
  for (const mode of ['wake', 'timer', 'off']) {
    const context = await ctx.browser.newContext({ viewport: { width: 900, height: 620 } });
    // D is forced to 2 and the browsers send with 25 ms of latency: at D=2 every millisecond a hidden tab adds to a round trip shows (the
    // review of the first version found that inputs were sent one network wake late, which this guards).
    const { host, room } = await nativeHost(ctx, { peers: 2, seed: 2, delay: 2 });
    const q = wsQuery(ctx, { hiddendriver: mode, impair: 'lat=25,seed=' + (mode === 'wake' ? 1 : mode === 'timer' ? 2 : 3) });
    const b = await openGame(context, 'bg-' + mode + '-hidden', ctx.server, q);
    const a = await openGame(context, 'bg-' + mode + '-visible', ctx.server, q);
    for (const p of [a, b]) await startGame(p);
    await netStart(b, false, 'hidden', room);
    await netStart(a, false, 'visible', room);
    for (const p of [a, b]) await waitNetState(p, [3], 60000, 'the round starts');
    await hidePage(b);
    await throttleLikeABackgroundTab(b);
    await sleep(3000);   // let the last animation frame go by
    const t0 = { a: await tickOf(a), b: await tickOf(b) }, polls0 = await call(b, 'bitriot_hidden_polls'), time0 = Date.now();
    await sleep(20000);
    const t1 = { a: await tickOf(a), b: await tickOf(b) }, polls1 = await call(b, 'bitriot_hidden_polls'), secs = (Date.now() - time0) / 1000;
    const inf = await info(a);
    results[mode] = { hiddenRate: (t1.b - t0.b) / secs, visibleRate: (t1.a - t0.a) / secs, polls: (polls1 - polls0) / secs };
    console.log('  ' + mode + ': hidden tab ' + results[mode].hiddenRate.toFixed(1) + ' ticks/s, visible tab ' + results[mode].visibleRate.toFixed(1) +
                ' ticks/s, hidden-tab loop runs ' + results[mode].polls.toFixed(1) + '/s, the visible tab sees: ' + (inf.stall || 'no stall'));
    await finish(a); await finish(b);
    stopNative(host);
    await context.close();
  }
  check(results.wake.hiddenRate >= 27, 'the default (network wake-ups) keeps a throttled hidden tab at nearly the nominal 30 ticks a second', results.wake.hiddenRate.toFixed(1));
  check(results.wake.visibleRate >= 27, 'and the visible tab and the host are not held back by it', results.wake.visibleRate.toFixed(1));
  check(results.timer.hiddenRate < results.wake.hiddenRate / 2, 'a one-second timer alone does not keep up: that is why the wake-ups are used', results.timer.hiddenRate.toFixed(1) + ' vs ' + results.wake.hiddenRate.toFixed(1));
  check(results.off.hiddenRate < 5, 'with nothing driving it a hidden tab stalls the match', results.off.hiddenRate.toFixed(1));
};

// A tab that does not run for `ms` (a frozen tab, a laptop lid, a debugger): its main thread is blocked, so nothing is polled, stepped or
// sent, and what arrives meanwhile waits in the browser. (CDP Page.setWebLifecycleState frozen did not freeze headless Chromium here:
// the page kept running. A blocked thread is what the game sees either way.) Returns a function that waits for the block to end.
function freeze(page, ms) {
  const done = page.evaluate((n) => { const t = Date.now(); while (Date.now() - t < n) { /* blocked */ } }, ms);
  return async () => { await done; };
}

scenarios.frozen = async (ctx) => {
  console.log('frozen: a guest tab does not run for six seconds (its thread is blocked); the others say who they wait for, and it rejoins the match');
  const { host, room } = await nativeHost(ctx, { peers: 2, seed: 3, config: 'players=4,map=13x11,blocks=30' });
  const q = wsQuery(ctx);
  const pa = await openGame(ctx.context(), 'frozen-a', ctx.server, q), pb = await openGame(ctx.context(), 'frozen-b', ctx.server, q);
  for (const p of [pa, pb]) await startGame(p);
  await netStart(pa, false, 'webA', room); await netStart(pb, false, 'webB', room);
  for (const p of [pa, pb]) await waitNetState(p, [3], 60000, 'the round starts');
  await sleep(2000);
  const thaw = freeze(pa, 6000);
  const t0 = await tickOf(pb);
  let sawWaiting = false;
  for (let i = 0; i < 12; i++) { await sleep(500); const st = (await info(pb)).stall; if (/webA/.test(st)) sawWaiting = true; }
  check(sawWaiting, 'while a tab is frozen the other guest is told who the match waits for', (await info(pb)).stall);
  check((await tickOf(pb)) - t0 < 60, 'the match is held while it is frozen (lockstep stalls rather than guessing)', (await tickOf(pb)) - t0);
  await thaw();
  await sleep(3000);
  const inf = await info(pa);
  check(inf.sparseGaps >= 1, 'the thawed tab noticed its own gap and did not time anyone out for it', JSON.stringify(inf));
  check(await netState(pa) === 3 && await netState(pb) === 3, 'both guests are still in the round', await netState(pa) + ',' + await netState(pb));
  const before = await tickOf(pb);
  await sleep(3000);
  check((await tickOf(pb)) - before >= 60, 'and it plays on at speed', (await tickOf(pb)) - before);
  for (const p of [pa, pb]) await waitNetState(p, [4, 5], 300000, 'the round ends');
  await withTimeout(host.exited, 60000, 'the host to finish');
  const machines = [{ name: 'native', log: parseLog(host.out) }, { name: 'frozen-a', log: await parseBrowserLog(pa) }, { name: 'frozen-b', log: await parseBrowserLog(pb) }];
  compareMachines(machines, 'frozen', 10);
  await finish(pa); await finish(pb);
};

scenarios['host-away'] = async (ctx) => {
  console.log('host-away: a browser hosts; its tab does not run for five seconds; the guests say the host is away, then play on');
  const q = wsQuery(ctx);
  const ph = await openGame(ctx.context(), 'away-host', ctx.server, q);
  await startGame(ph);
  await netStart(ph, true, 'webHost', '');
  await waitNetState(ph, [2], 60000, 'the room is open');
  const room = await waitRoom(ph);
  check(/^[A-HJ-NP-Z2-9]{6}$/.test(room), 'the browser host opened a room', room);
  const panel = await ph.evaluate(() => ({ hidden: document.getElementById('room').hidden, code: document.getElementById('roomcode').textContent }));
  check(!panel.hidden && panel.code === room, 'the page shows the room code under the canvas, as selectable text', JSON.stringify(panel));
  const guest = startNative(['join', '--relay', ctx.relay.udp, '--room', room, '--rounds', '1', '--name', 'native', '--tick-ms', '33', '--timeout-secs', '900']);
  const pg = await openGame(ctx.context(), 'away-guest', ctx.server, q);
  await startGame(pg);
  await netStart(pg, false, 'webGuest', room);
  await waitNetState(pg, [2], 40000, 'joined');
  // The host is a menu host: it waits for every guest to say ready.
  await sleep(1500);
  await call(pg, 'bitriot_net_press_start');
  await sleep(1500);
  await call(ph, 'bitriot_net_press_start');
  for (const p of [ph, pg]) await waitNetState(p, [3], 40000, 'the round starts');
  await sleep(2000);
  const thaw = freeze(ph, 5000);
  let away = null;
  for (let i = 0; i < 16 && !away; i++) { await sleep(500); const st = (await info(pg)).stall; if (/host is away/.test(st)) away = st; }
  check(away !== null, 'the guests see a clear "host is away" state while the host tab is frozen', away || (await info(pg)).stall);
  await thaw();
  const t0 = await tickOf(pg);
  await sleep(4000);
  check((await tickOf(pg)) - t0 >= 60, 'and the match goes on when the host is back', (await tickOf(pg)) - t0);
  check(await netState(pg) === 3, 'the guest was not dropped by the gap', await netState(pg));
  const a = await parseBrowserLog(ph), b = await parseBrowserLog(pg), n = parseLog(guest.out);
  let common = 0, bad = 0;
  for (const [k, h] of a.cps) { if (b.cps.has(k)) { common++; if (b.cps.get(k) !== h) bad++; } if (n.cps.has(k) && n.cps.get(k) !== h) bad++; }
  check(common >= 5 && bad === 0, 'every checkpoint the machines share agrees', common + ' shared, ' + bad + ' differing');
  stopNative(guest);
  await finish(ph); await finish(pg);
};

async function waitRoom(page) {
  for (let i = 0; i < 100; i++) { const r = await callString(page, 'bitriot_net_room'); if (r) return r; await sleep(200); }
  return '';
}


// A LOCAL game in a hidden tab does not run (brief 09: it stops). Nothing drives hidden passes unless a network game is on.
scenarios['hidden-local'] = async (ctx) => {
  console.log('hidden-local: a local game in a throttled hidden tab stops (no ticks, no sound)');
  const page = await openGame(ctx.context(), 'hidden-local', ctx.server);
  await startGame(page);
  check((await call(page, 'bitriot_start_ai_match', 4)) === 1, 'a four-CPU local round starts');
  await page.waitForFunction(() => window.Module._bitriot_tick() > 30, null, { timeout: 30000 });
  await hidePage(page);
  await throttleLikeABackgroundTab(page);
  await sleep(2500);
  const t0 = await call(page, 'bitriot_tick'), s0 = await call(page, 'bitriot_sounds_played');
  await sleep(10000);
  const t1 = await call(page, 'bitriot_tick'), s1 = await call(page, 'bitriot_sounds_played');
  check(t1 - t0 <= 2, 'the local round does not advance while the tab is hidden', t0 + ' -> ' + t1);
  check(s1 === s0, 'and makes no sound', s0 + ' -> ' + s1);
  check((await call(page, 'bitriot_hidden_polls')) === 0, 'no hidden pass ran at all');
  await finish(page);
};

// Both machines of a match asleep for 12 s (longer than the 10 s timeout) with nobody awake to talk: neither may conclude the other died.
scenarios.asleep = async (ctx) => {
  console.log('asleep: a browser host and a browser guest both blocked for 12 s (more than the 10 s timeout): nobody is dropped');
  const q = wsQuery(ctx);
  const ph = await openGame(ctx.context(), 'asleep-host', ctx.server, q), pg = await openGame(ctx.context(), 'asleep-guest', ctx.server, q);
  await startGame(ph); await startGame(pg);
  await netStart(ph, true, 'webHost', '');
  await waitNetState(ph, [2], 60000, 'the room is open');
  const room = await waitRoom(ph);
  await netStart(pg, false, 'webGuest', room);
  await waitNetState(pg, [2], 40000, 'joined');
  await sleep(1500); await call(pg, 'bitriot_net_press_start'); await sleep(1500); await call(ph, 'bitriot_net_press_start');
  for (const p of [ph, pg]) await waitNetState(p, [3], 40000, 'the round starts');
  await sleep(2000);
  const tA = freeze(ph, 12000), tB = freeze(pg, 12000);
  await Promise.all([tA(), tB()]);
  await sleep(4000);
  const [ia, ib] = [await info(ph), await info(pg)];
  check(await netState(ph) === 3 && await netState(pg) === 3, 'both are still in the round after a gap longer than the timeout', (await netState(ph)) + ',' + (await netState(pg)) + ' ' + ia.endReason + ',' + ib.endReason);
  check(ia.sparseGaps >= 1 && ib.sparseGaps >= 1, 'each noticed its own gap', ia.sparseGaps + ',' + ib.sparseGaps);
  const t0 = await tickOf(pg);
  await sleep(3000);
  check((await tickOf(pg)) - t0 >= 60, 'and the match plays on at speed', (await tickOf(pg)) - t0);
  await finish(ph); await finish(pg);
};

// The dev knobs only work on a ?test=1 page, and a saved relay never beats the one in the page's address.
scenarios.knobs = async (ctx) => {
  console.log('knobs: dev URL parameters are ignored without ?test=1; ?relay= beats a saved relay; invite links carry the relay in use');
  const base = 'http://127.0.0.1:' + ctx.server.port + '/index.html';
  const evil = 'relay_wt=' + encodeURIComponent('https://127.0.0.1:1/vnet') + '&relay_cert=' + '00'.repeat(32) + '&impair=lat%3D900&hiddendriver=off';
  const plain = await ctx.context().newPage();
  plain.__problems = []; plain.__lines = []; plain.__name = 'knobs-plain';
  await plain.goto(base + '?' + evil);
  await plain.waitForFunction(() => window.bitriotReady === true, null, { timeout: 120000 });
  await plain.click('#start');
  await plain.waitForFunction(() => window.bitriotStarted === true && window.Module._bitriot_frames() > 5, null, { timeout: 60000 });
  check((await call(plain, 'bitriot_dev_knobs_active')) === 0, 'relay_wt, relay_cert, impair and hiddendriver are ignored on a page without ?test=1');
  check((await call(plain, 'bitriot_net_start', 1, 1)) === 0, 'and the test hooks are shut too');
  await plain.close();
  const test = await openGame(ctx.context(), 'knobs-test', ctx.server, { relay_wt: 'https://127.0.0.1:1/vnet', relay_cert: '00'.repeat(32), impair: 'lat=900' });
  await startGame(test);
  check((await call(test, 'bitriot_dev_knobs_active')) === 1, 'the same knobs work with ?test=1');
  await finish(test);

  // A saved relay in the browser, and an invite link (?relay=&room=) from a host: the link's relay is the one used.
  const { host, room } = await nativeHost(ctx, { peers: 1, seed: 3, config: 'players=4,map=13x11,blocks=30' });
  const context = ctx.context();
  await context.addInitScript(() => { try { window.localStorage.setItem('bitriot.netplay', 'name=Old\nrelay=ws://127.0.0.1:1/vnet\nroom=AAAAAA\n'); } catch (e) { /* */ } });
  const page = await openGame(context, 'knobs-invite', ctx.server, { ...wsQuery(ctx), room });
  await startGame(page);
  const key = async (k) => { await page.keyboard.press(k); await sleep(250); };
  await key('ArrowDown'); await key('Enter'); await key('ArrowDown'); await key('Enter');
  for (let i = 0; i < 3; i++) await key('ArrowDown');   // from the room field: relay, players, Connect
  await key('Enter');
  await waitNetState(page, [2, 3], 40000, 'joined with the invite link while another relay was saved');
  check(true, 'an invite link (?relay=&room=) joins although another relay was saved');
  const used = await page.evaluate(() => window.bitriotRoomRelay);
  check(used === 'ws://127.0.0.1:' + ctx.relay.httpPort + '/vnet', 'the page knows the relay in use, for the invite link it offers', used);
  stopNative(host);
  await finish(page);
};

// The real menus, with the keyboard.
scenarios.ui = async (ctx) => {
  console.log('ui: join by room code through the menus, the saved settings, the room panel');
  const { host, room } = await nativeHost(ctx, { peers: 1, seed: 3, config: 'players=4,map=13x11,blocks=30' });
  const q = wsQuery(ctx);
  let page = await openGame(ctx.context(), 'ui', ctx.server, q);
  await startGame(page);
  const key = async (k) => { await page.keyboard.press(k); await sleep(250); };
  // Main menu: Play, Online game, ... Down, Enter. Online menu: Host via relay, Join by room code, Back (a browser has no direct UDP).
  await key('ArrowDown'); await key('Enter');
  await page.screenshot({ path: path.join(shotDir, 'ui-online-menu.png') });
  await key('ArrowDown'); await key('Enter');
  // Join form: Your name, Room code, Relay, Players here, Connect, Back. The room code field has the focus.
  await page.keyboard.type(room.toLowerCase(), { delay: 40 });
  await key('ArrowUp'); await page.keyboard.type('Webby', { delay: 40 });
  await page.screenshot({ path: path.join(shotDir, 'ui-join-form.png') });
  await key('ArrowDown'); await key('ArrowDown'); await key('ArrowDown'); await key('ArrowDown'); await key('Enter');
  await waitNetState(page, [2, 3], 40000, 'joined by the form');
  check((await callString(page, 'bitriot_net_transport')) === 'relay-ws', 'the form joined over the WebSocket');
  const shown = await page.evaluate(() => ({ hidden: document.getElementById('room').hidden, code: document.getElementById('roomcode').textContent }));
  check(!shown.hidden && shown.code === room, 'the room panel under the canvas shows the code of the room joined', JSON.stringify(shown));
  await page.screenshot({ path: path.join(shotDir, 'ui-lobby.png') });
  const saved = await page.evaluate(() => window.localStorage.getItem('bitriot.netplay'));
  check(/name=Webby/.test(saved || '') && new RegExp('room=' + room).test(saved || '') && /relay=ws:\/\/127\.0\.0\.1:/.test(saved || ''), 'the name, room and relay were saved in the browser', JSON.stringify(saved));
  await withTimeout(host.exited, 120000, 'the host to start and finish').catch(() => stopNative(host));
  await finish(page);
  // A fresh page load: the join form comes back filled in.
  page = await openGame(ctx.context(), 'ui-reload', ctx.server, q);
  await startGame(page);
  await key('ArrowDown'); await key('Enter'); await key('ArrowDown'); await key('Enter');
  const shot = await page.locator('#canvas').screenshot({ path: path.join(shotDir, 'ui-join-form-reloaded.png') });
  const inspected = await page.evaluate(() => window.localStorage.getItem('bitriot.netplay'));
  check(inspected !== null && /name=Webby/.test(inspected), 'the settings survive a reload (localStorage)');
  check(shot.length > 2000, 'the reloaded join form drew (see ui-join-form-reloaded.png: name Webby, the room and relay filled in)');
  await finish(page);
};

scenarios.errors = async (ctx) => {
  console.log('errors: a room that does not exist, and a relay that is not there');
  const q = wsQuery(ctx);
  let page = await openGame(ctx.context(), 'err-room', ctx.server, q);
  await startGame(page);
  await page.evaluate(() => { window.Module.bitriotTestArgs = { name: 'lost', room: 'ZZZZZZ' }; });
  check((await call(page, 'bitriot_net_start', 0, 1)) === 1, 'the join starts');
  await waitNetState(page, [5], 30000, 'the relay refuses the room');
  const inf = await info(page);
  check(/no such room/.test(inf.endText), 'the session says the room does not exist', inf.endText);
  await page.screenshot({ path: path.join(shotDir, 'err-no-room.png') });
  await finish(page);
  page = await openGame(ctx.context(), 'err-relay', ctx.server, { relay: 'ws://127.0.0.1:1/vnet' });
  await startGame(page);
  check((await call(page, 'bitriot_net_start', 1, 1)) === 1, 'hosting on a dead relay starts');
  await waitNetState(page, [5], 60000, 'the relay cannot be reached');
  const inf2 = await info(page);
  check(inf2.endReason === 8, 'the session ended with a network error, not a hang', JSON.stringify(inf2));
  await page.screenshot({ path: path.join(shotDir, 'err-relay.png') });
  await finish(page, { allowProblems: true });
};

// ---- main --------------------------------------------------------------------------------------------------------------------------

(async () => {
  for (const f of ['index.html', 'BitRiot.js', 'BitRiot.wasm', 'BitRiot.data']) {
    if (!fs.existsSync(path.join(buildDir, f))) { console.error('Missing ' + path.join(buildDir, f) + ': build the web version first (see README.md).'); process.exit(2); }
  }
  const relayBin = buildRelay();
  const server = await serve(buildDir);
  const relay = await startRelay(relayBin);
  const browser = await chromium.launch({ args: GL_ARGS });
  const ctx = { server, relay, relayBin, browser, context: () => ctxOne, headlessBin };
  let ctxOne = await browser.newContext({ viewport: { width: 900, height: 620 }, colorScheme: 'dark' });
  console.log('relay udp ' + relay.udp + ' http ' + relay.http + ' wt ' + relay.wt + '; native ' + headlessBin + '; screenshots and logs in ' + shotDir);
  try {
    for (const name of Object.keys(scenarios)) {
      if (only && !only.includes(name)) continue;
      ctxOne = await browser.newContext({ viewport: { width: 900, height: 620 }, colorScheme: 'dark' });
      await scenarios[name](ctx);
      await ctxOne.close();
    }
  } catch (e) {
    failures++;
    console.log('  FAIL the test stopped: ' + (e && e.stack || e));
    await dumpOpenPages();
  } finally {
    await browser.close().catch(() => {});
    relay.stop();
    server.close();
  }
  console.log((failures === 0 ? 'PASS' : 'FAIL') + ': ' + (checks - failures) + ' of ' + checks + ' checks passed');
  process.exit(failures === 0 ? 0 : 1);
})();
