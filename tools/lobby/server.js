#!/usr/bin/env node
/* mcpe-lobby -- the board, and the wire between the players on it.
 *
 * Two halves of the same missing LAN, in one process.
 *
 * The board. 0.6.1 finds games by shouting into the LAN:
 * RakNetInstance::pingForHosts broadcasts to 255.255.255.255 on 19132-19135 and
 * fills the list from whoever pongs back. A browser tab has no UDP socket to
 * shout from, so on the web that list is always empty. This replaces the shout:
 * a tab says "I am playing" while it plays, and asks who else is.
 *
 * The wire. A tab cannot open a UDP socket to the player it picked either, so
 * /relay is a switch: every tab holds one WebSocket to it, datagrams arrive
 * addressed to another tab's route, and it forwards them. It does not speak
 * RakNet and does not want to -- the payloads are opaque, and the game's own
 * handshake, ordering and reliability run end to end between the two tabs.
 *
 * They are one process because the alternative is two Node runtimes, and the
 * runtime is almost the entire cost: the board is a Map of short strings and the
 * switch is a Map of sockets. See "What it costs" in README.md.
 *
 * Nothing is persisted. Every board entry is worthless thirty seconds after it
 * is written and a route means nothing once its socket is gone, so there is
 * nothing a database would be preserving. Restarting drops both; the next
 * heartbeat rebuilds the board and a reconnect rebuilds the route.
 */

'use strict';

const http = require('http');
const crypto = require('crypto');

const NAME     = 'mcpe-lobby';
const VERSION  = '1';
const PORT     = Number(process.env.MCPE_LOBBY_PORT || 8477);
const HOST     = process.env.MCPE_LOBBY_HOST || '127.0.0.1';

// A tab heartbeats every 10s; three misses and it is gone. Long enough to ride
// out a phone changing network, short enough that the list is not a graveyard.
const TTL_MS   = Number(process.env.MCPE_LOBBY_TTL || 35000);

// Ceilings, not tuning. Anyone on the internet can reach this, so nothing here
// is allowed to grow without one.
const MAX_PLAYERS    = 200;   // whole board; the one that bounds memory
const MAX_BODY       = 1024;  // bytes of request body

/* One address announcing a crowd of fake players. Generous on purpose: a source
 * is an IP, and a household, a school or a phone network is one IP for everyone
 * behind it. This was 4, which measured as 4 players per NAT and would have
 * capped the whole board at 4 the moment it went behind a TLS proxy -- see
 * TRUST_PROXY below. MAX_PLAYERS is what actually protects memory; this only
 * stops one source owning the whole board. */
const MAX_PER_SOURCE = 25;

/* Whether X-Forwarded-For can be believed. Off by default because the header is
 * client-supplied: trusting it on a directly-reachable service means anyone can
 * forge a source and walk straight through MAX_PER_SOURCE.
 *
 * Turn it on only when this sits behind a proxy that *overwrites* the header --
 * nginx `proxy_set_header X-Forwarded-For $remote_addr;`, not the $proxy_add_
 * variant that appends, which would let a client prepend whatever it liked. */
const TRUST_PROXY = process.env.MCPE_LOBBY_TRUST_PROXY === '1';

/* Relay ceilings. A datagram is one RakNet packet: MAXIMUM_MTU_SIZE is 1492, so
 * 4096 is well clear of anything the game will send and small enough that a
 * connection's partial-frame buffer cannot become interesting. Anything larger
 * is not the game and gets the connection closed rather than truncated.
 *
 * The rate cap is per connection. 0.6.1 sends a few KB/s per player once a world
 * is loaded and bursts to a few hundred while chunks go out, so 512 KB/s is far
 * above honest use; a tab over it has its frames dropped, and one at four times
 * it is not the game and is disconnected. */
const MAX_DATAGRAM   = 4096;
const RATE_BYTES     = Number(process.env.MCPE_RELAY_RATE || 512 * 1024);
const MAX_SOCKETS    = Number(process.env.MCPE_RELAY_MAX || 200);

const WS_GUID = '258EAFA5-E914-47DA-95CA-C5AB0DC85B11';

const startedAt = Date.now();
/** id -> {name, world, route, source, seen} */
const players = new Map();
/** route -> connection. The switching table; see the relay section below. */
const routes = new Map();

/* The game font is a 2013 ASCII bitmap and these strings are drawn straight
 * into other people's menus, so anything that is not printable ASCII cannot be
 * displayed and does not belong in the list. Cutting it here rather than in the
 * client means the board itself is never holding something unrenderable. */
function clean(value, max) {
	return String(value == null ? '' : value)
		.replace(/[^\x20-\x7e]/g, '')
		.trim()
		.slice(0, max);
}

function sweep() {
	const cutoff = Date.now() - TTL_MS;
	for (const [id, p] of players)
		if (p.seen < cutoff) players.delete(id);
}

/* Makes room on a full board by dropping the stalest entry, but only one that
 * has stopped heartbeating recently enough to be a ghost rather than a player.
 *
 * A flat refusal at MAX_PLAYERS meant a real player could be turned away by 200
 * entries that were merely on their way out, which is the common case and it
 * self-heals only after a full TTL. Refusing to evict anything fresher than
 * half a TTL is what keeps this from becoming a rolling window that a flood
 * could use to push live players off the board. Returns whether it freed a slot.
 */
function evictStalest() {
	const staleBefore = Date.now() - TTL_MS / 2;
	let oldestId = null;
	let oldestSeen = Infinity;
	for (const [id, p] of players)
		if (p.seen < oldestSeen) { oldestSeen = p.seen; oldestId = id; }

	if (oldestId === null || oldestSeen >= staleBefore) return false;
	players.delete(oldestId);
	return true;
}

function sourceOf(req) {
	// Behind a reverse proxy every player arrives from the proxy's address, so
	// the per-source cap would be counting the proxy rather than the player --
	// but only a proxy we put there can be believed. See TRUST_PROXY.
	if (TRUST_PROXY) {
		const fwd = req.headers['x-forwarded-for'];
		if (fwd) return String(fwd).split(',')[0].trim();
	}
	return (req.socket && req.socket.remoteAddress) || '?';
}

function send(res, code, body) {
	const text = typeof body === 'string' ? body : JSON.stringify(body);
	res.writeHead(code, {
		'Content-Type': typeof body === 'string' ? 'text/plain' : 'application/json',
		'Content-Length': Buffer.byteLength(text),
		// The page is served from github.io and this is not, so every request is
		// cross-origin. There is nothing to protect: no credentials, no cookies,
		// and the whole board is public by design.
		'Access-Control-Allow-Origin': '*',
		'Cache-Control': 'no-store',
		'Server': NAME + '/' + VERSION
	});
	res.end(text);
}

/* Calls done(body) exactly once: with the parsed object, or null for anything
 * unusable. The settled guard is the point -- 'end' and 'error' can both fire on
 * the same request, and a second call would mean a second writeHead on a
 * response already sent, which is an uncaught throw rather than a 400. */
function readBody(req, done) {
	let size = 0;
	let settled = false;
	const chunks = [];

	const finish = (value) => {
		if (settled) return;
		settled = true;
		done(value);
	};

	req.on('data', (c) => {
		if (settled) return;
		size += c.length;
		// Answer before hanging up, so an oversized announce gets told why
		// instead of seeing the connection vanish under it.
		if (size > MAX_BODY) { finish(undefined); return; }
		chunks.push(c);
	});
	req.on('end', () => {
		try { finish(JSON.parse(Buffer.concat(chunks).toString('utf8') || '{}')); }
		catch (e) { finish(null); }
	});
	req.on('error', () => finish(null));
	req.on('aborted', () => finish(null));
}

const server = http.createServer((req, res) => {
	const route = (req.url || '/').split('?')[0].replace(/\/+$/, '') || '/';

	if (req.method === 'OPTIONS') {
		res.writeHead(204, {
			'Access-Control-Allow-Origin': '*',
			'Access-Control-Allow-Methods': 'GET, POST, OPTIONS',
			'Access-Control-Allow-Headers': 'Content-Type',
			'Access-Control-Max-Age': '86400'
		});
		return res.end();
	}

	// Says what it is to anyone who finds the port open. Wanted by whoever is
	// looking at a process list months from now and asking what 8477 is.
	if (route === '/' && req.method === 'GET') {
		sweep();
		return send(res, 200, {
			service: NAME,
			version: VERSION,
			purpose: 'presence list and datagram relay for the Minecraft PE 0.6.1 web build',
			repo: 'https://github.com/luispl77/mcpe-0.6.1-web',
			carriesGameTraffic: true,
			players: players.size,
			connected: routes.size,
			ttlSeconds: Math.round(TTL_MS / 1000),
			uptimeSeconds: Math.round((Date.now() - startedAt) / 1000)
		});
	}

	if (route === '/list' && req.method === 'GET') {
		sweep();
		const now = Date.now();
		const out = [];
		for (const [id, p] of players)
			out.push({
				id: id,
				name: p.name,
				world: p.world,
				// 0 for a player with no relay socket: on the board, but not
				// reachable. The client turns this into the address it would
				// dial, and does not offer to dial 0.
				route: p.route,
				age: Math.round((now - p.seen) / 1000)
			});
		return send(res, 200, { players: out });
	}

	if (route === '/announce' && req.method === 'POST') {
		return readBody(req, (body) => {
			if (body === undefined) return send(res, 413, { error: 'body too large' });
			if (!body) return send(res, 400, { error: 'bad json' });

			const id = clean(body.id, 40);
			if (!id) return send(res, 400, { error: 'missing id' });

			sweep();

			// Leaving a world: drop out of the list rather than linger for a TTL.
			if (body.gone) { players.delete(id); return send(res, 200, { ok: true }); }

			const source = sourceOf(req);
			const existing = players.get(id);
			if (!existing) {
				if (players.size >= MAX_PLAYERS && !evictStalest())
					return send(res, 503, { error: 'full' });
				let fromSource = 0;
				for (const p of players.values()) if (p.source === source) fromSource++;
				if (fromSource >= MAX_PER_SOURCE) return send(res, 429, { error: 'too many from source' });
			}

			const name = clean(body.name, 20) || 'Player';
			players.set(id, {
				name: name,
				world: clean(body.world, 20),
				/* A route is only worth publishing if the announcing tab is
				 * really the one holding it. The relay hands out a token with
				 * the route on the socket it belongs to, and this is the only
				 * thing that knows both -- without the check any tab could
				 * publish somebody else's route and quietly collect their
				 * joiners. An announce that fails it is still listed, just not
				 * as joinable. */
				route: routeFor(body.route, body.token),
				source: source,
				seen: Date.now()
			});
			return send(res, 200, { ok: true });
		});
	}

	return send(res, 404, { error: 'not found', service: NAME });
});

/* ---------------------------------------------------------------------------
 * The relay
 *
 * A switch, not a server: it forwards opaque datagrams between tabs and has no
 * idea what RakNet is. Every tab holds one socket, is given a route when it
 * connects, and addresses each datagram with the route it is for.
 *
 *     client -> [4-byte destination route][payload]
 *     server -> [4-byte source route     ][payload]
 *
 * Rewriting the header on the way out is what makes it usable: the receiving tab
 * needs to know who sent it, and the sender cannot be trusted to say. That one
 * substitution is also the whole of the security model for spoofing between
 * connected players -- you can address anyone, but you cannot claim to be them.
 *
 * WebSocket is spoken by hand rather than through `ws`. It is about a hundred
 * lines for the subset a browser actually sends, and it keeps this a single file
 * with no node_modules to deploy or keep patched.
 * ------------------------------------------------------------------------- */

/** Frames a payload for sending. Server-to-client frames are never masked. */
function frame(opcode, payload) {
	const len = payload.length;
	let header;
	if (len < 126) {
		header = Buffer.alloc(2);
		header[1] = len;
	} else if (len < 65536) {
		header = Buffer.alloc(4);
		header[1] = 126;
		header.writeUInt16BE(len, 2);
	} else {
		header = Buffer.alloc(10);
		header[1] = 127;
		header.writeUInt32BE(0, 2);
		header.writeUInt32BE(len, 6);
	}
	header[0] = 0x80 | opcode;
	return Buffer.concat([header, payload]);
}

function drop(conn, why) {
	if (conn.closed) return;
	conn.closed = true;
	if (conn.route) routes.delete(conn.route);
	try { conn.socket.end(frame(0x8, Buffer.alloc(0))); } catch (e) { /* already gone */ }
	try { conn.socket.destroy(); } catch (e) { /* already gone */ }
	if (why) conn.why = why;
}

/* Routes are handed out in order and reused once their socket has gone. 24 bits
 * because that is what survives the trip through the client: the game keys
 * remote systems by IPv4 address, so a route travels as the low three octets of
 * a 10.0.0.0/8 address. Never 0 -- the client reads 0 as "not joinable". */
let nextRoute = 1;
function allocRoute() {
	for (let tries = 0; tries < 0x1000000; tries++) {
		const candidate = nextRoute;
		nextRoute = (nextRoute + 1) & 0x00ffffff;
		if (nextRoute === 0) nextRoute = 1;
		if (!routes.has(candidate)) return candidate;
	}
	return 0;
}

/** The route to publish for an announce, or 0 if it cannot be vouched for. */
function routeFor(route, token) {
	const wanted = Number(route) & 0x00ffffff;
	if (!wanted) return 0;
	const conn = routes.get(wanted);
	if (!conn || !token || conn.token !== String(token)) return 0;
	return wanted;
}

/* Bytes per second, per connection, counted on a rolling one-second window.
 * Over the cap the frame is dropped and the connection carries on -- a burst
 * while chunks go out is normal and should not end a game. Far over it, nothing
 * that is playing behaves like that, and the connection goes. */
function spend(conn, bytes) {
	const now = Date.now();
	if (now - conn.windowAt >= 1000) { conn.windowAt = now; conn.spent = 0; }
	conn.spent += bytes;
	if (conn.spent > RATE_BYTES * 4) { drop(conn, 'rate'); return false; }
	return conn.spent <= RATE_BYTES;
}

function deliver(conn, message) {
	// Shorter than its own header is not addressed to anybody.
	if (message.length < 4) return;
	if (!spend(conn, message.length)) return;

	const target = routes.get(message.readUInt32BE(0) & 0x00ffffff);
	// Silently, on purpose: a datagram for a player who has just closed their
	// tab is the ordinary case, and RakNet is already built to time out a peer
	// that stops answering. Telling the sender would only invent a second way
	// for it to find out something it handles perfectly well on its own.
	if (!target || target.closed || target === conn) return;

	const out = Buffer.allocUnsafe(message.length);
	out.writeUInt32BE(conn.route, 0);
	message.copy(out, 4, 4);
	try { target.socket.write(frame(0x2, out)); } catch (e) { drop(target, 'write'); }
}

function onFrame(conn, fin, opcode, payload) {
	if (opcode === 0x8) return drop(conn, 'closed');
	if (opcode === 0x9) { try { conn.socket.write(frame(0xa, payload)); } catch (e) { drop(conn); } return; }
	if (opcode === 0xa) return;

	if (opcode === 0x0) {
		if (!conn.frag) return drop(conn, 'stray continuation');
		if (conn.frag.length + payload.length > MAX_DATAGRAM) return drop(conn, 'oversize');
		conn.frag = Buffer.concat([conn.frag, payload]);
	} else {
		if (conn.frag) return drop(conn, 'interleaved');
		conn.frag = payload;
	}

	if (!fin) return;
	const message = conn.frag;
	conn.frag = null;
	deliver(conn, message);
}

/** Pulls every whole frame out of what has arrived so far. */
function onData(conn, chunk) {
	if (conn.closed) return;
	conn.seen = Date.now();
	conn.buf = conn.buf.length ? Buffer.concat([conn.buf, chunk]) : chunk;

	// A frame header can be 14 bytes; anything beyond a datagram plus that is
	// either a lie about the length or a client this is not for.
	if (conn.buf.length > MAX_DATAGRAM + 64) return drop(conn, 'flood');

	for (;;) {
		const b = conn.buf;
		if (b.length < 2) return;

		const fin = (b[0] & 0x80) !== 0;
		const opcode = b[0] & 0x0f;
		const masked = (b[1] & 0x80) !== 0;
		let len = b[1] & 0x7f;
		let off = 2;

		if (len === 126) {
			if (b.length < off + 2) return;
			len = b.readUInt16BE(off);
			off += 2;
		} else if (len === 127) {
			if (b.length < off + 8) return;
			// The high word being non-zero means a payload of at least 4GB.
			if (b.readUInt32BE(off) !== 0) return drop(conn, 'oversize');
			len = b.readUInt32BE(off + 4);
			off += 8;
		}

		if (len > MAX_DATAGRAM) return drop(conn, 'oversize');
		// Every browser frame is masked. An unmasked one is not a browser.
		if (!masked) return drop(conn, 'unmasked');
		if (b.length < off + 4 + len) return;

		const mask = b.subarray(off, off + 4);
		off += 4;
		const payload = Buffer.allocUnsafe(len);
		for (let i = 0; i < len; i++) payload[i] = b[off + i] ^ mask[i & 3];
		off += len;

		conn.buf = b.subarray(off);
		onFrame(conn, fin, opcode, payload);
		if (conn.closed) return;
	}
}

server.on('upgrade', (req, socket, head) => {
	const path = (req.url || '/').split('?')[0].replace(/\/+$/, '') || '/';
	const key = req.headers['sec-websocket-key'];

	if (path !== '/relay' || !key ||
	    String(req.headers.upgrade || '').toLowerCase() !== 'websocket') {
		socket.destroy();
		return;
	}

	if (routes.size >= MAX_SOCKETS) { socket.destroy(); return; }

	const route = allocRoute();
	if (!route) { socket.destroy(); return; }

	const conn = {
		socket: socket,
		route: route,
		token: crypto.randomBytes(12).toString('hex'),
		buf: Buffer.alloc(0),
		frag: null,
		closed: false,
		seen: Date.now(),
		windowAt: Date.now(),
		spent: 0
	};
	routes.set(route, conn);

	// Nagle would sit on a 30-byte movement packet waiting for company. This is
	// a game's wire; latency is the whole product.
	socket.setNoDelay(true);

	// The kernel's own liveness check, for a peer that stops answering without
	// ever sending a FIN -- a phone going into a tunnel, not a tab being closed.
	socket.setKeepAlive(true, 30000);

	socket.on('data', (chunk) => onData(conn, chunk));
	socket.on('error', () => drop(conn, 'error'));
	socket.on('close', () => drop(conn, 'close'));

	/* 'end' as well as 'close', and this is not belt and braces. A socket taken
	 * from an HTTP upgrade can sit half-open after the client's FIN: 'end'
	 * fires, 'close' does not, and the route stays in the switching table for
	 * a peer that has gone. Measured: 200 clients exited and the board still
	 * reported 200 connected with one real socket on the port. */
	socket.on('end', () => drop(conn, 'end'));

	socket.write(
		'HTTP/1.1 101 Switching Protocols\r\n' +
		'Upgrade: websocket\r\n' +
		'Connection: Upgrade\r\n' +
		'Sec-WebSocket-Accept: ' +
			crypto.createHash('sha1').update(key + WS_GUID).digest('base64') + '\r\n' +
		'\r\n');

	// The route is the tab's address for as long as this socket lives, and the
	// token is what lets it prove to /announce that the route is really its own.
	socket.write(frame(0x1, Buffer.from(
		JSON.stringify({ type: 'hello', route: route, token: conn.token }), 'utf8')));

	if (head && head.length) onData(conn, head);
});

/* Nothing is heard from a tab sitting on the title screen, so silence alone
 * cannot mean gone. Ping first and give it a window to answer; a browser
 * replies to a ping without the page being involved, so anything still there
 * will. This is the backstop for a connection that dies without either a FIN or
 * a reset -- the case keepalive is slow to notice and 'end' never sees. */
const PING_AFTER_MS = 30000;
const SILENT_FOR_MS = 90000;

setInterval(() => {
	const now = Date.now();
	for (const conn of Array.from(routes.values())) {
		if (conn.closed) continue;
		if (now - conn.seen > SILENT_FOR_MS) { drop(conn, 'silent'); continue; }
		if (now - conn.seen > PING_AFTER_MS) {
			try { conn.socket.write(frame(0x9, Buffer.alloc(0))); }
			catch (e) { drop(conn, 'ping'); }
		}
	}
}, 15000).unref();

server.listen(PORT, HOST, () => {
	console.log(NAME + '/' + VERSION + ' listening on ' + HOST + ':' + PORT +
	            ' (ttl ' + Math.round(TTL_MS / 1000) + 's, max ' + MAX_PLAYERS +
	            ', relay /relay max ' + MAX_SOCKETS + ')');
});

// Stopping is a supported outcome, not a failure: the client treats an
// unreachable lobby exactly like an empty LAN, so a clean exit here leaves the
// Join card looking like it did before any of this existed.
for (const signal of ['SIGINT', 'SIGTERM'])
	process.on(signal, () => {
		console.log(NAME + ' stopping; ' + players.size + ' player(s) and ' +
		            routes.size + ' relay socket(s) dropped');
		// close() stops accepting but waits on established connections, and a
		// relay socket is deliberately long-lived -- without this the service
		// would sit there until the last player got bored.
		for (const conn of Array.from(routes.values())) drop(conn, 'shutdown');
		server.close(() => process.exit(0));
	});
