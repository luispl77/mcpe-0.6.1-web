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
const dgram = require('dgram');
const fs = require('fs');
const path = require('path');

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

/* Dedicated servers: worlds that are not anybody's tab.
 *
 * The file is a list of {id, name, world, host, port} and optionally
 * {salt, passwordHash}; it is read at startup and on SIGHUP, which is what the
 * provisioner touches after adding one. Absent or unreadable means there are no
 * dedicated servers and everything else works exactly as before -- the same way
 * an absent relay means no multiplayer rather than an error.
 *
 * Every player talking to a server needs its own UDP source port, because the
 * game keys remote systems by address *and* port and would otherwise see one
 * peer changing its mind. So the bridge holds a socket per (server, player)
 * pair rather than per server. Both ceilings below exist because that number is
 * the product of two things a stranger can influence. */
const SERVERS_FILE   = process.env.MCPE_LOBBY_SERVERS || '';
const MAX_BRIDGE     = Number(process.env.MCPE_BRIDGE_MAX || 400);
const BRIDGE_IDLE_MS = Number(process.env.MCPE_BRIDGE_IDLE || 60000);

const WS_GUID = '258EAFA5-E914-47DA-95CA-C5AB0DC85B11';

/* The game itself, when this is the deployment serving it as well as the one
 * joining it up. Empty -- the default -- serves no files at all and this is
 * purely a lobby, which is how it runs locally and in the tests.
 *
 * It is here rather than in the proxy in front because that proxy is Caddy in a
 * container on a box doing other things, and giving it a bind mount for a game
 * means editing a compose file that production depends on. One process that
 * can be installed, checked and stopped on its own is worth more than the
 * marginal efficiency of a dedicated file server. */
const WEB_ROOT = process.env.MCPE_WEB_ROOT || '';

/* Where the service's own routes live, when it is also serving the game.
 * `/lobby` puts the board at `/lobby/list` and leaves `/` to be the page.
 *
 * Serving both from one origin is deliberate -- it means no CORS and one
 * certificate -- but it makes `/` ambiguous, and the ambiguity is not
 * resolvable by guessing: `/` is the service's own description, and it is also
 * index.html. So a web root without a prefix is refused at startup rather than
 * silently picking one and leaving the other unreachable. */
const PREFIX = (process.env.MCPE_LOBBY_PREFIX || '').replace(/\/+$/, '');

if (WEB_ROOT && !PREFIX) {
	console.error(NAME + ': MCPE_WEB_ROOT needs MCPE_LOBBY_PREFIX (e.g. /lobby).');
	console.error('  Both the page and this service want "/", so the service has to move.');
	process.exit(2);
}

const CONTENT_TYPES = {
	'.html': 'text/html; charset=utf-8',
	'.js':   'text/javascript; charset=utf-8',
	// Required, not cosmetic: the browser refuses to stream-compile a wasm
	// module served as anything else, and falls back to a slower path or fails.
	'.wasm': 'application/wasm',
	'.data': 'application/octet-stream',
	'.mp3':  'audio/mpeg',
	'.png':  'image/png'
};

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

/** The service route a request is asking for, or null if it is asking for a file. */
function routeOf(url) {
	const asked = (url || '/').split('?')[0].replace(/\/+$/, '') || '/';
	if (!PREFIX) return asked;
	if (asked === PREFIX) return '/';
	if (asked.startsWith(PREFIX + '/')) return asked.slice(PREFIX.length);
	return null;
}

const server = http.createServer((req, res) => {
	const route = routeOf(req.url);

	// Not addressed to the service at all: it is the page, or a 404 for one.
	if (route === null) {
		if (WEB_ROOT && (req.method === 'GET' || req.method === 'HEAD'))
			return serveFile(req, res, (req.url || '/').split('?')[0]);
		return send(res, 404, { error: 'not found', service: NAME });
	}

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
			servers: servers.size,
			connected: tabs(),
			bridged: bridgeSockets,
			ttlSeconds: Math.round(TTL_MS / 1000),
			uptimeSeconds: Math.round((Date.now() - startedAt) / 1000)
		});
	}

	if (route === '/list' && req.method === 'GET') {
		sweep();
		const now = Date.now();
		const out = [];
		/* Servers first, and always: they are the thing that is still there
		 * tomorrow, and a board sorted by whoever announced last would bury them
		 * under whoever is currently playing. `locked` and `dedicated` are extra
		 * keys rather than a change of shape -- the client reads name, world and
		 * route positionally and ignores the rest, so an older build still lists
		 * them correctly and simply cannot tell them apart. */
		for (const [route, s] of servers)
			out.push({
				id: s.id,
				name: s.name,
				world: s.world,
				route: route,
				age: 0,
				dedicated: true,
				locked: !!s.passwordHash
			});
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

	/* Opening a locked server, for the socket that asks.
	 *
	 * The token is what ties the request to a relay socket -- the same pairing
	 * /announce uses -- so unlocking is something a connection does for itself
	 * and not something one tab can do on another's behalf. Nothing is stored
	 * anywhere but on that connection, so it ends when the socket does.
	 *
	 * The attempt is charged to the socket rather than the address: behind a
	 * phone network or a school the address is everybody, and locking them all
	 * out because one of them is guessing is the wrong failure. */
	if (route === '/unlock' && req.method === 'POST') {
		return readBody(req, (body) => {
			if (body === undefined) return send(res, 413, { error: 'body too large' });
			if (!body) return send(res, 400, { error: 'bad json' });

			const conn = routes.get(Number(body.route) & 0x00ffffff);
			if (!conn || conn.isServer || !body.token || conn.token !== String(body.token))
				return send(res, 403, { error: 'not your route' });

			const server = servers.get(Number(body.server) & 0x00ffffff);
			if (!server) return send(res, 404, { error: 'no such server' });

			conn.tries = (conn.tries || 0) + 1;
			if (conn.tries > 10) return send(res, 429, { error: 'too many attempts' });

			if (!unlocks(server, body.password)) return send(res, 403, { error: 'wrong password' });

			if (!conn.unlocked) conn.unlocked = new Set();
			conn.unlocked.add(server.route);
			return send(res, 200, { ok: true });
		});
	}

	return send(res, 404, { error: 'not found', service: NAME });
});

/* Static files, when WEB_ROOT is set.
 *
 * Revalidated rather than cached: sync-from-pages replaces every file in place
 * and the names never change, so a browser told to cache minecraftpe.wasm would
 * keep playing the old build until it evicted it. An ETag off mtime and size
 * makes a repeat visit one cheap 304 per file instead of a re-download.
 *
 * Streamed rather than read: the .data package alone is 3.8 MB and the wasm is
 * larger, and this process is supposed to stay small. */
function serveFile(req, res, route) {
	const rel = route === '/' ? 'index.html' : route.replace(/^\/+/, '');

	// Resolve first, then check it is still inside the root. Rejecting "..' by
	// pattern is the version of this that gets caught out by encodings.
	let file;
	try { file = path.resolve(WEB_ROOT, decodeURIComponent(rel)); }
	catch (e) { return send(res, 400, { error: 'bad path' }); }

	const root = path.resolve(WEB_ROOT);
	if (file !== root && !file.startsWith(root + path.sep))
		return send(res, 403, { error: 'outside root' });

	fs.stat(file, (err, st) => {
		if (err || !st.isFile()) return send(res, 404, { error: 'not found', service: NAME });

		const etag = '"' + st.size.toString(16) + '-' + st.mtimeMs.toString(16) + '"';
		const headers = {
			'Content-Type': CONTENT_TYPES[path.extname(file).toLowerCase()] || 'application/octet-stream',
			'Cache-Control': 'no-cache',
			'ETag': etag,
			'Server': NAME + '/' + VERSION
		};

		if (req.headers['if-none-match'] === etag) {
			res.writeHead(304, headers);
			return res.end();
		}

		headers['Content-Length'] = st.size;
		res.writeHead(200, headers);
		if (req.method === 'HEAD') return res.end();

		const stream = fs.createReadStream(file);
		stream.on('error', () => res.destroy());
		res.on('close', () => stream.destroy());
		stream.pipe(res);
	});
}

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
	if (conn.route) {
		// A server's route lives in the same table but outlives every tab, so it
		// must not be swept away by one leaving.
		if (!conn.isServer) routes.delete(conn.route);
		releaseBridge(conn);
	}
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

/* ---------------------------------------------------------------------------
 * The bridge
 *
 * A dedicated server sits on the switch as an endpoint that is not a tab. It
 * holds a reserved route like everybody else, so a player addresses it exactly
 * the way they address another player and neither the game nor the page needs
 * to know the difference. What is behind the route is a UDP socket on this box
 * instead of a WebSocket.
 *
 * The reason this is one socket per (server, player) rather than one per server
 * is the far end: RakNet keys remote systems by SystemAddress, which is address
 * *and* port. Sharing a socket would present every player to the server as the
 * same peer, and the second one to connect would look like the first one having
 * a very strange time. Giving each player its own ephemeral port means the
 * server sees them as distinct without the bridge having to forge anything --
 * which it could not do anyway without raw sockets and root.
 * ------------------------------------------------------------------------- */

/** route -> {route, id, name, world, host, port, salt, passwordHash, peers} */
const servers = new Map();
let bridgeSockets = 0;

/* Sockets held by tabs. Servers sit in `routes` too -- that is what lets a
 * player address one without knowing it is not a player -- but they are not
 * connections and must not be counted as if they were: they would inflate what
 * the service reports as `connected`, and worse, they would eat into
 * MAX_SOCKETS, so that registering servers quietly lowered how many people
 * could be in a world at once. */
function tabs() { return routes.size - servers.size; }

/** Reads the servers file into `servers`, keeping routes stable across reloads. */
function loadServers() {
	if (!SERVERS_FILE) return;

	let list;
	try {
		list = JSON.parse(fs.readFileSync(SERVERS_FILE, 'utf8'));
	} catch (e) {
		// Deliberately not fatal, and deliberately loud. A syntax error in this
		// file must not take the board and the relay down with it: the players
		// already in a world are not part of what broke.
		console.error('mcpe-lobby: cannot read %s: %s', SERVERS_FILE, e.message);
		return;
	}
	if (!Array.isArray(list)) return;

	const byId = new Map();
	for (const [route, s] of servers) byId.set(s.id, route);

	const keep = new Set();
	for (const entry of list) {
		const id = clean(entry && entry.id, 40);
		const port = Number(entry && entry.port);
		if (!id || !(port > 0 && port < 65536)) continue;

		// A restart of a server must not move it on the board, so a route is
		// reused whenever the id is one we already had.
		let route = byId.get(id);
		if (!route) {
			route = allocRoute();
			if (!route) break;
		}
		const previous = servers.get(route);
		for (const peer of (previous ? previous.peers : new Map()).values())
			closePeer(peer);

		servers.set(route, {
			// Also carried on the record, not just the map key: the reply path
			// stamps datagrams with it, and a source route of 0 is what the
			// client reads as "not joinable" -- so getting this wrong does not
			// look like a bug here, it looks like the server refusing to answer.
			route: route,
			id: id,
			name: clean(entry.name, 20) || 'Server',
			world: clean(entry.world, 20),
			host: clean(entry.host, 60) || '127.0.0.1',
			port: port,
			salt: clean(entry.salt, 64) || '',
			passwordHash: clean(entry.passwordHash, 128) || '',
			peers: new Map()
		});
		routes.set(route, { isServer: true, route: route, closed: false });
		keep.add(route);
	}

	for (const [route, s] of Array.from(servers)) {
		if (keep.has(route)) continue;
		for (const peer of s.peers.values()) closePeer(peer);
		servers.delete(route);
		routes.delete(route);
	}
}

function closePeer(peer) {
	if (peer.closed) return;
	peer.closed = true;
	bridgeSockets--;
	clearTimeout(peer.idle);
	try { peer.socket.close(); } catch (e) { /* already gone */ }
}

/** Whether this password opens this server. Empty hash means it is not locked. */
function unlocks(server, password) {
	if (!server.passwordHash) return true;
	const given = crypto.createHash('sha256')
		.update(server.salt + String(password === undefined ? '' : password)).digest('hex');
	const a = Buffer.from(given, 'utf8');
	const b = Buffer.from(server.passwordHash, 'utf8');
	return a.length === b.length && crypto.timingSafeEqual(a, b);
}

/** The socket this player talks to this server through, made on first use. */
function peerFor(server, conn) {
	let peer = server.peers.get(conn.route);
	if (peer && !peer.closed) return peer;

	if (bridgeSockets >= MAX_BRIDGE) return null;

	const socket = dgram.createSocket('udp4');
	peer = { socket: socket, closed: false, idle: null };
	bridgeSockets++;
	server.peers.set(conn.route, peer);

	socket.on('message', (payload) => {
		touch(peer);
		if (conn.closed || payload.length > MAX_DATAGRAM) return;
		// Source-stamped with the server's route, by the same rule that governs
		// tab-to-tab: the receiver is told who sent it by the switch, never by
		// the sender.
		const out = Buffer.allocUnsafe(payload.length + 4);
		out.writeUInt32BE(server.route, 0);
		payload.copy(out, 4);
		try { conn.socket.write(frame(0x2, out)); } catch (e) { drop(conn, 'write'); }
	});

	// A bridge socket failing is one player's problem, not the service's.
	socket.on('error', () => { server.peers.delete(conn.route); closePeer(peer); });

	touch(peer);
	return peer;
}

/* Bridge sockets are cheap but not free, and the event that should close one --
 * a player leaving -- is a tab closing, which arrives as a websocket close and
 * not as anything RakNet says. So they also expire on their own. */
function touch(peer) {
	clearTimeout(peer.idle);
	peer.idle = setTimeout(() => closePeer(peer), BRIDGE_IDLE_MS);
	if (peer.idle.unref) peer.idle.unref();
}

/** Drops every bridge socket a departing tab was holding. */
function releaseBridge(conn) {
	for (const server of servers.values()) {
		const peer = server.peers.get(conn.route);
		if (peer) { server.peers.delete(conn.route); closePeer(peer); }
	}
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

	const wanted = message.readUInt32BE(0) & 0x00ffffff;
	const target = routes.get(wanted);
	// Silently, on purpose: a datagram for a player who has just closed their
	// tab is the ordinary case, and RakNet is already built to time out a peer
	// that stops answering. Telling the sender would only invent a second way
	// for it to find out something it handles perfectly well on its own.
	if (!target || target.closed || target === conn) return;

	// A dedicated server: same route space, different thing behind it.
	if (target.isServer) {
		const server = servers.get(wanted);
		if (!server) return;
		// A locked server is locked here rather than in the game, so that being
		// refused costs a stranger a datagram and not a seat. The unlock is per
		// socket, so it cannot be replayed from another tab.
		if (server.passwordHash && !(conn.unlocked && conn.unlocked.has(wanted))) return;
		const peer = peerFor(server, conn);
		if (!peer) return;
		touch(peer);
		try {
			peer.socket.send(message.subarray(4), server.port, server.host);
		} catch (e) { /* the next datagram will try again */ }
		return;
	}

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
	const asked = routeOf(req.url);
	const key = req.headers['sec-websocket-key'];

	if (asked !== '/relay' || !key ||
	    String(req.headers.upgrade || '').toLowerCase() !== 'websocket') {
		socket.destroy();
		return;
	}

	if (tabs() >= MAX_SOCKETS) { socket.destroy(); return; }

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

/* Somewhere for the thing that writes the servers file to find us, so it can
 * ask for a reload. Only written when asked for: this service is happy to be a
 * process nobody manages, and a stale pidfile is worse than no pidfile. */
const PIDFILE = process.env.MCPE_LOBBY_PIDFILE || '';
if (PIDFILE) {
	try {
		fs.writeFileSync(PIDFILE, String(process.pid));
		for (const signal of ['SIGINT', 'SIGTERM'])
			process.on(signal, () => { try { fs.unlinkSync(PIDFILE); } catch (e) { /* going down anyway */ } });
	} catch (e) {
		console.error('mcpe-lobby: cannot write %s: %s', PIDFILE, e.message);
	}
}

loadServers();

/* Reload rather than restart, because a restart would take the relay with it:
 * adding a server must not disconnect the players already in a world. The
 * provisioner writes the file and sends this. */
process.on('SIGHUP', () => {
	loadServers();
	console.log(NAME + ' reloaded servers: ' + servers.size);
});

server.listen(PORT, HOST, () => {
	console.log(NAME + '/' + VERSION + ' listening on ' + HOST + ':' + PORT +
	            ' (ttl ' + Math.round(TTL_MS / 1000) + 's, max ' + MAX_PLAYERS +
	            ', relay /relay max ' + MAX_SOCKETS +
	            ', web ' + (WEB_ROOT || 'off') +
	            ', servers ' + (SERVERS_FILE ? servers.size : 'off') + ')');
});

// Stopping is a supported outcome, not a failure: the client treats an
// unreachable lobby exactly like an empty LAN, so a clean exit here leaves the
// Join card looking like it did before any of this existed.
for (const signal of ['SIGINT', 'SIGTERM'])
	process.on(signal, () => {
		console.log(NAME + ' stopping; ' + players.size + ' player(s) and ' +
		            tabs() + ' relay socket(s) dropped');
		// close() stops accepting but waits on established connections, and a
		// relay socket is deliberately long-lived -- without this the service
		// would sit there until the last player got bored.
		for (const conn of Array.from(routes.values())) drop(conn, 'shutdown');
		server.close(() => process.exit(0));
	});
