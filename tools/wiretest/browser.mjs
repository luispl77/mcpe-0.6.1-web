/* Two real tabs, the real shell.html, the real relay.
 *
 * The wasm half is covered by raktest; this covers the half that only exists in
 * a browser -- that the page boots, that window.mcpeNet gets a route from the
 * relay, and that a datagram put in one tab comes out of the other.
 */
import { chromium } from 'playwright-core';

const PAGE = 'http://127.0.0.1:8000/minecraftpe.html' +
             '?touch=0&lobby=http://127.0.0.1:8477&relay=ws://127.0.0.1:8477/relay';

let failures = 0;
function check(name, ok, detail) {
  console.log((ok ? 'ok   ' : 'FAIL ') + name + (detail === undefined ? '' : '  -> ' + detail));
  if (!ok) failures++;
}

const browser = await chromium.launch({
  args: ['--enable-unsafe-swiftshader', '--use-gl=swiftshader', '--no-sandbox']
});

async function tab(label) {
  const page = await browser.newPage();
  const errors = [];
  page.on('pageerror', (e) => errors.push(String(e)));
  page.on('console', (m) => { if (m.type() === 'error') errors.push(m.text()); });
  await page.goto(PAGE, { waitUntil: 'domcontentloaded' });
  return { label, page, errors };
}

const a = await tab('A');
const b = await tab('B');

// The relay socket opens on its own; the game does not have to be running.
for (const t of [a, b]) {
  await t.page.waitForFunction(
    () => window.mcpeNet && window.mcpeNet.ready(), null, { timeout: 20000 }
  ).catch(() => {});
}

const routeA = await a.page.evaluate(() => window.mcpeNet.route());
const routeB = await b.page.evaluate(() => window.mcpeNet.route());

check('tab A got a relay route', routeA > 0, String(routeA));
check('tab B got a different one', routeB > 0 && routeB !== routeA, String(routeB));

// A datagram across two real browser sockets, through the real relay.
await b.page.evaluate(() => { window.__got = []; const real = window.mcpeNet.recv;
  window.__drain = () => { let d, out = []; while ((d = real.call(window.mcpeNet))) out.push(d); return out; }; });

await a.page.evaluate((dest) => {
  window.mcpeNet.send(dest, new Uint8Array([0x84, 1, 2, 3, 250]));
}, routeB);

await new Promise((r) => setTimeout(r, 600));

const got = await b.page.evaluate(() => window.__drain().map((d) => ({ route: d.route, bytes: [...d.bytes] })));
check('B received exactly one datagram', got.length === 1, JSON.stringify(got));
if (got.length === 1) {
  check('  addressed from A', got[0].route === routeA, got[0].route + ' vs ' + routeA);
  check('  payload intact, header stripped', got[0].bytes.join(',') === '132,1,2,3,250', got[0].bytes.join(','));
}

// The page must survive the wasm booting; the old Join-card crash showed up here.
await new Promise((r) => setTimeout(r, 4000));
const stillUp = await a.page.evaluate(() => !!(window.mcpeNet && window.mcpeNet.ready()));
check('tab A is still alive after the module has had time to boot', stillUp);

/* Two things the game has always said at console.error, neither a fault:
 * the LEGACY_GL_EMULATION banner, and SDL asking for a frame rate before
 * emscripten_set_main_loop has registered one. Both are on the live github.io
 * build too, which is the check that settled whether they were ours -- if
 * either stops appearing, delete it from here rather than leaving a filter that
 * hides something real. */
const realErrors = a.errors.concat(b.errors).filter(
  (e) => !/favicon|menu\.mp3|Failed to load resource|using emscripten GL/i.test(e))
  .filter((e) => !/set_main_loop_timing/.test(e));
check('no page errors', realErrors.length === 0, realErrors.slice(0, 3).join(' | '));

// The link across only appears when this deployment has no relay of its own.
const withRelay = await a.page.evaluate(() => document.getElementById('mpMsg').textContent);
check('a multiplayer deployment says so rather than linking away',
      /Multiplayer is on/.test(withRelay), withRelay);

const plain = await browser.newPage();
await plain.goto('http://127.0.0.1:8000/minecraftpe.html?touch=0', { waitUntil: 'domcontentloaded' });
const noRelay = await plain.evaluate(() => document.getElementById('mpMsg').textContent);
const href = await plain.evaluate(() => { const el = document.querySelector('#mpMsg a'); return el && el.href; });
check('a single-player deployment links to the multiplayer one',
      /Single player/.test(noRelay) && /mcpe\.continualmi\.com/.test(href || ''), noRelay + '  ' + href);

await browser.close();
console.log(failures ? '\n' + failures + ' FAILED' : '\nall passed');
process.exit(failures ? 1 : 0);
