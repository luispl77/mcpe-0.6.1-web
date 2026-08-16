// Does a backgrounded tab still run the network?
//
// The claim under test: requestAnimationFrame stops in a tab nobody is looking
// at, so runFrame() -- and the RunUpdateCycleOnce() inside it -- stops with it,
// which is what dropped the connection. pumpIfFrameLoopStalled() is supposed to
// notice that and keep the network going without drawing anything.
//
// B is opened by A with window.open so the two are tabs of one window; two
// Playwright pages are two windows, and an unfocused window is throttled but
// never actually hidden, which is a weaker thing to test against.

import { chromium } from 'playwright-core';

const PAGE = 'http://127.0.0.1:8000/minecraftpe.html';
const RELAY = 'ws://127.0.0.1:8477/relay';
const LOBBY = 'http://127.0.0.1:8477';

let failures = 0;
function ok(cond, what, extra = '') {
  console.log(`${cond ? 'ok  ' : 'FAIL'} ${what}${extra ? '  -> ' + extra : ''}`);
  if (!cond) failures++;
}

// Counts rAF frames and mcpe_pump_net() calls from now on, so the two can be
// compared across a visibility change.
const INSTRUMENT = `
  window.__probe = { frames: 0, pumps: 0 };
  (function rafTick() {
    window.__probe.frames++;
    requestAnimationFrame(rafTick);
  })();
  var real = Module._mcpe_pump_net;
  Module._mcpe_pump_net = function () { window.__probe.pumps++; return real.apply(this, arguments); };
`;

const url = (n) =>
  `${PAGE}?touch=1&relay=${encodeURIComponent(RELAY)}&lobby=${encodeURIComponent(LOBBY)}&probe=${n}`;

const browser = await chromium.launch({ headless: false });
const context = await browser.newContext();

const a = await context.newPage();
await a.goto(url('A'));

const opened = context.waitForEvent('page');
await a.evaluate((u) => window.open(u), url('B'));
const b = await opened;

for (const [p, n] of [[a, 'A'], [b, 'B']]) {
  await p.waitForFunction('window.mcpeReady === true', null, { timeout: 120000 });
  console.log(`tab ${n} booted`);
}

// Both tabs need a relay slot, or there is no socket to be woken by.
for (const [p, n] of [[a, 'A'], [b, 'B']]) {
  await p.evaluate('window.mcpeNet.open()');
  await p.waitForFunction('window.mcpeNet.ready()', null, { timeout: 15000 });
  const route = await p.evaluate('window.mcpeNet.route()');
  ok(route > 0, `tab ${n} has a relay route`, String(route));
}

const routeA = await a.evaluate('window.mcpeNet.route()');
await a.evaluate(INSTRUMENT);

// --- while A is the tab you are looking at -------------------------------
await a.bringToFront();
await a.evaluate('window.__probe = {frames: 0, pumps: 0}');
await new Promise((r) => setTimeout(r, 2000));
const front = await a.evaluate('window.__probe');
ok(front.frames > 30, 'rAF runs while A is in front', `${front.frames} frames in 2s`);
ok(front.pumps === 0, 'pump keeps out of the way while frames flow', `${front.pumps} pumps`);

// --- now switch to B -----------------------------------------------------
await b.bringToFront();
await a.evaluate('window.__probe = {frames: 0, pumps: 0}');
await new Promise((r) => setTimeout(r, 4000));
const hidden = await a.evaluate('window.__probe');

// Not asserted, reported: document.hidden is exactly the signal this fix
// stopped depending on. Under Xvfb it stays false while the frame loop is
// being throttled into the ground, which is the case that would have gone
// unhandled had the pump been gated on it.
console.log(`     (document.hidden === ${await a.evaluate('document.hidden')}, ` +
            `frames ${front.frames}/2s -> ${hidden.frames}/4s)`);

const frontRate = front.frames / 2, hiddenRate = hidden.frames / 4;
ok(hiddenRate < frontRate / 5, "A's frame loop has collapsed -- this is the bug",
   `${frontRate.toFixed(0)}fps -> ${hiddenRate.toFixed(1)}fps`);
ok(hidden.pumps >= 3, 'the network is pumped anyway', `${hidden.pumps} pumps in 4s`);

// --- and a datagram from B wakes A straight away, not on the next timer ---
await a.evaluate('window.__probe = {frames: 0, pumps: 0}');
await b.evaluate((r) => window.mcpeNet.send(r, new Uint8Array([1, 2, 3, 4])), routeA);
await new Promise((r) => setTimeout(r, 250));   // well inside the 1s interval
const woken = await a.evaluate('window.__probe');
ok(woken.pumps >= 1, 'an arriving datagram pumps a hidden tab immediately',
   `${woken.pumps} pumps within 250ms`);
// The pump reaches RakNet, but there is no RakNet here to reach: this harness
// drives window.mcpeNet directly and never starts a world, so no peer has been
// through Startup() and RunUpdateCycleOnce() returns on its endThreads check
// without draining anything. So the datagram is still on the inbox -- which is
// the right answer for "nothing was lost", and as far as this test can go.
// What happens once a peer *is* started is what tools/wiretest/raknet.cpp
// covers, with two real peers wired to each other.
ok(await a.evaluate('window.mcpeNet.recv() !== null'),
   'the datagram is intact on the inbox (no peer started in this harness)');

// --- back to A: the pump stands down -------------------------------------
await a.bringToFront();
await a.evaluate('window.__probe = {frames: 0, pumps: 0}');
await new Promise((r) => setTimeout(r, 2000));
const backFront = await a.evaluate('window.__probe');
ok(backFront.frames > 30, 'frames resume when A comes back', `${backFront.frames} frames in 2s`);
ok(backFront.pumps === 0, 'pump stands down again', `${backFront.pumps} pumps`);

await browser.close();
console.log(failures ? `\n${failures} failed` : '\nall good');
process.exit(failures ? 1 : 0);
