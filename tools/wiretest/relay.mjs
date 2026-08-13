// End-to-end check of the lobby+relay: two sockets, a datagram between them,
// and the announce/list path that publishes a route.
const BASE = 'http://127.0.0.1:8477';
const WS = 'ws://127.0.0.1:8477/relay';

let failures = 0;
function check(name, ok, detail) {
  console.log((ok ? 'ok   ' : 'FAIL ') + name + (detail === undefined ? '' : '  -> ' + detail));
  if (!ok) failures++;
}

function open() {
  return new Promise((resolve, reject) => {
    const ws = new WebSocket(WS);
    ws.binaryType = 'arraybuffer';
    const inbox = [];
    let hello = null;
    ws.onmessage = (e) => {
      if (typeof e.data === 'string') { hello = JSON.parse(e.data); resolve({ ws, inbox, hello: () => hello }); }
      else inbox.push(new Uint8Array(e.data));
    };
    ws.onerror = () => reject(new Error('ws error'));
    setTimeout(() => reject(new Error('no hello')), 3000);
  });
}

function datagram(route, bytes) {
  const out = new Uint8Array(4 + bytes.length);
  new DataView(out.buffer).setUint32(0, route >>> 0, false);
  out.set(bytes, 4);
  return out;
}

const settle = (ms = 250) => new Promise((r) => setTimeout(r, ms));

const a = await open();
const b = await open();

check('two sockets get distinct routes', a.hello().route !== b.hello().route,
      a.hello().route + ' vs ' + b.hello().route);
check('hello carries a token', typeof a.hello().token === 'string' && a.hello().token.length >= 16);

// A -> B, and the header must come back rewritten as A's route.
a.ws.send(datagram(b.hello().route, new Uint8Array([9, 8, 7, 6])));
await settle();
check('B received one datagram', b.inbox.length === 1, 'got ' + b.inbox.length);
if (b.inbox.length) {
  const got = b.inbox[0];
  const src = new DataView(got.buffer, got.byteOffset).getUint32(0, false);
  check('source route is A, not what A wrote', src === a.hello().route, src + ' vs ' + a.hello().route);
  check('payload survives intact', [...got.slice(4)].join(',') === '9,8,7,6', [...got.slice(4)].join(','));
}

// Unknown destination is dropped, not fatal.
a.ws.send(datagram(0xabcdef, new Uint8Array([1])));
await settle();
check('socket survives a datagram to nobody', a.ws.readyState === 1);

// A 1400-byte datagram is a normal RakNet packet and must pass whole.
const big = new Uint8Array(1400).map((_, i) => i & 0xff);
a.ws.send(datagram(b.hello().route, big));
await settle();
const last = b.inbox[b.inbox.length - 1];
check('1400-byte datagram arrives whole', last && last.length === 1404, last && last.length);
check('  and its bytes are unchanged', last && last[4] === 0 && last[1403] === ((1399) & 0xff));

// announce with a good token publishes the route; a bad one publishes 0.
const announce = (body) => fetch(BASE + '/announce', {
  method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body)
}).then((r) => r.json());
const list = () => fetch(BASE + '/list').then((r) => r.json());

await announce({ id: 'aaa', name: 'Alice', world: 'Home', route: a.hello().route, token: a.hello().token });
await announce({ id: 'bbb', name: 'Mallory', world: 'Fake', route: a.hello().route, token: 'wrong' });
const players = (await list()).players;
const alice = players.find((p) => p.id === 'aaa');
const mallory = players.find((p) => p.id === 'bbb');

check('a vouched route is published', alice && alice.route === a.hello().route, alice && alice.route);
check('a stolen route is not', mallory && mallory.route === 0, mallory && mallory.route);
check('the impostor is still listed, just unjoinable', !!mallory);

// Closing a socket frees its route from the switch.
const freed = b.hello().route;
b.ws.close();
await settle(400);
const info = await fetch(BASE + '/').then((r) => r.json());
check('closing a socket drops its route', info.connected === 1, 'connected=' + info.connected);
check('service reports it carries game traffic', info.carriesGameTraffic === true);

/* A client that vanishes without a WebSocket close frame -- a tab crashing, a
 * process being killed -- must free its route too. This is a regression test:
 * a socket taken from an HTTP upgrade can sit half-open after the peer's FIN,
 * firing 'end' but never 'close', and the first version of the relay leaked
 * every such route. 200 dead clients still showed as 200 connected. */
import net from 'node:net';
import crypto from 'node:crypto';

const before = (await fetch(BASE + '/').then((r) => r.json())).connected;

const raw = net.connect(8477, '127.0.0.1');
await new Promise((resolve) => {
  raw.on('connect', () => {
    raw.write(
      'GET /relay HTTP/1.1\r\nHost: 127.0.0.1\r\nUpgrade: websocket\r\n' +
      'Connection: Upgrade\r\nSec-WebSocket-Version: 13\r\n' +
      'Sec-WebSocket-Key: ' + crypto.randomBytes(16).toString('base64') + '\r\n\r\n');
  });
  raw.on('data', () => resolve());
  setTimeout(resolve, 2000);
});

const during = (await fetch(BASE + '/').then((r) => r.json())).connected;
check('a raw upgraded socket takes a route', during === before + 1, before + ' -> ' + during);

raw.destroy();
await settle(600);
const after = (await fetch(BASE + '/').then((r) => r.json())).connected;
check('a client that dies without a close frame frees it', after === before, during + ' -> ' + after);

a.ws.close();
await settle(100);
console.log(failures ? '\n' + failures + ' FAILED' : '\nall passed');
process.exit(failures ? 1 : 0);
