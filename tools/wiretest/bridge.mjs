// The bridge: a dedicated server as an endpoint on the switch that is not a tab.
//
// The far end here is a plain UDP echo socket rather than a real mcpe_server,
// because what needs checking is the switch, not the game -- that datagrams
// reach the server's port, that replies come back stamped with the server's
// route, that two players arrive on different source ports, and that a locked
// server stays shut until the right password opens it for that socket only.
//
// Wants the lobby started with a servers file pointing at ECHO_PORT below:
//
//   cat > /tmp/servers.json <<'JSON'
//   [{"id":"echo","name":"Echo","world":"test","host":"127.0.0.1","port":19199},
//    {"id":"locked","name":"Locked","world":"test","host":"127.0.0.1","port":19199,
//     "salt":"s","passwordHash":"11922f920d4af4e9819fb38ae4c3ea11cc09df3be1e00829b536374085747dbc"}]
//   JSON
//   MCPE_LOBBY_SERVERS=/tmp/servers.json node tools/lobby/server.js &
//   node tools/wiretest/bridge.mjs
//
// The hash above is sha256("s" + "open"), so the locked server's password is
// "open". Regenerate with:
//   node -e 'console.log(require("crypto").createHash("sha256").update("s"+"open").digest("hex"))'
import dgram from 'dgram';

const BASE = 'http://127.0.0.1:8477';
const WS = 'ws://127.0.0.1:8477/relay';
const ECHO_PORT = 19199;

let failures = 0;
function check(name, ok, detail) {
  console.log((ok ? 'ok   ' : 'FAIL ') + name + (detail === undefined ? '' : '  -> ' + detail));
  if (!ok) failures++;
}

const settle = (ms = 400) => new Promise((r) => setTimeout(r, ms));

/* The far end. It answers every datagram with the source port it saw, which is
 * what makes "each player gets its own socket" checkable from the outside. */
const echo = dgram.createSocket('udp4');
const seenPorts = new Set();
echo.on('message', (msg, rinfo) => {
  seenPorts.add(rinfo.port);
  echo.send(Buffer.concat([Buffer.from('from:'), Buffer.from(String(rinfo.port))]), rinfo.port, rinfo.address);
});
await new Promise((r) => echo.bind(ECHO_PORT, '127.0.0.1', r));

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

function datagram(route, text) {
  const bytes = Buffer.from(text);
  const out = new Uint8Array(4 + bytes.length);
  new DataView(out.buffer).setUint32(0, route >>> 0, false);
  out.set(bytes, 4);
  return out;
}

const sourceOf = (m) => new DataView(m.buffer, m.byteOffset).getUint32(0, false);
const bodyOf = (m) => Buffer.from(m.subarray(4)).toString('utf8');

const listed = (await fetch(BASE + '/list').then((r) => r.json())).players;
const openServer = listed.find((p) => p.dedicated && !p.locked);
const lockedServer = listed.find((p) => p.dedicated && p.locked);

check('an open dedicated server is on the board', !!openServer, openServer && openServer.name);
check('a locked one is on it too, and says so', !!lockedServer, lockedServer && lockedServer.name);
if (!openServer || !lockedServer) {
  console.log('\ncannot continue without both servers -- see the header of this file');
  process.exit(1);
}

const a = await open();
a.ws.send(datagram(openServer.route, 'hello'));
await settle(600);

check('a datagram addressed to a server reaches it', a.inbox.length > 0, a.inbox.length + ' reply(s)');
check('the reply is stamped with the server route, not 0',
      a.inbox.length > 0 && sourceOf(a.inbox[0]) === openServer.route,
      a.inbox.length ? sourceOf(a.inbox[0]) + ' vs ' + openServer.route : 'no reply');

/* The whole reason a socket is held per player rather than per server: the game
 * keys remote systems by address and port, so two players sharing one source
 * port would arrive as one peer changing its mind. */
const b = await open();
b.ws.send(datagram(openServer.route, 'hello'));
await settle(600);
check('a second player reaches it too', b.inbox.length > 0, b.inbox.length + ' reply(s)');
check('and arrives on a different source port', seenPorts.size >= 2,
      Array.from(seenPorts).join(', '));
check('each player is told only its own port back',
      a.inbox.length > 0 && b.inbox.length > 0 && bodyOf(a.inbox[0]) !== bodyOf(b.inbox[0]),
      a.inbox.length && b.inbox.length ? bodyOf(a.inbox[0]) + ' vs ' + bodyOf(b.inbox[0]) : 'missing');

// A locked server is shut before the game is ever reached.
const c = await open();
c.ws.send(datagram(lockedServer.route, 'hello'));
await settle(600);
check('a locked server drops datagrams from a socket that has not unlocked', c.inbox.length === 0,
      c.inbox.length + ' reply(s)');

const unlock = (conn, password, server) => fetch(BASE + '/unlock', {
  method: 'POST',
  body: JSON.stringify({ route: conn.hello().route, token: conn.hello().token, server: server.route, password })
});

check('a wrong password is refused', (await unlock(c, 'nope', lockedServer)).status === 403);
c.ws.send(datagram(lockedServer.route, 'hello'));
await settle(600);
check('and refusing it does not quietly open the door', c.inbox.length === 0, c.inbox.length + ' reply(s)');

check('the right password is accepted', (await unlock(c, 'open', lockedServer)).status === 200);
c.ws.send(datagram(lockedServer.route, 'hello'));
await settle(600);
check('and then the datagrams flow', c.inbox.length > 0, c.inbox.length + ' reply(s)');

/* Unlocking is something a socket does for itself. Without this, one player
 * knowing the password would be enough to open the server for everybody. */
const d = await open();
const stolen = await fetch(BASE + '/unlock', {
  method: 'POST',
  body: JSON.stringify({ route: c.hello().route, token: d.hello().token, server: lockedServer.route, password: 'open' })
});
check('a socket cannot unlock on behalf of another', stolen.status === 403, stolen.status);
d.ws.send(datagram(lockedServer.route, 'hello'));
await settle(600);
check('so the other socket is still shut out', d.inbox.length === 0, d.inbox.length + ' reply(s)');

for (const conn of [a, b, c, d]) conn.ws.close();
echo.close();
await settle(300);

console.log(failures ? '\n' + failures + ' failed' : '\nall passed');
process.exit(failures ? 1 : 0);
