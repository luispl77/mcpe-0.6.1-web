#!/usr/bin/env node
/* mcpe-lobby -- who is playing right now, for the Join Game card.
 *
 * 0.6.1 finds games by shouting into the LAN: RakNetInstance::pingForHosts
 * broadcasts to 255.255.255.255 on 19132-19135 and fills the list from whoever
 * pongs back. A browser tab has no UDP socket to shout from, so on the web that
 * list is always empty. This service replaces the shout: a tab says "I am
 * playing" while it plays, and asks who else is.
 *
 * It is deliberately a presence board and nothing more. It carries no game
 * traffic, so it is not on the path of anything being played -- see README.md
 * for why joining is a separate problem.
 *
 * State is one Map in memory. That is not a shortcut to be fixed later: every
 * entry is worthless thirty seconds after it is written, so there is nothing a
 * database would be preserving. Restarting drops the board and the next
 * heartbeat rebuilds it, which is also what makes this safe to stop -- see
 * "Stopping" in README.md.
 */

'use strict';

const http = require('http');

const NAME     = 'mcpe-lobby';
const VERSION  = '1';
const PORT     = Number(process.env.MCPE_LOBBY_PORT || 8477);
const HOST     = process.env.MCPE_LOBBY_HOST || '127.0.0.1';

// A tab heartbeats every 10s; three misses and it is gone. Long enough to ride
// out a phone changing network, short enough that the list is not a graveyard.
const TTL_MS   = Number(process.env.MCPE_LOBBY_TTL || 35000);

// Ceilings, not tuning. Anyone on the internet can reach this, so nothing here
// is allowed to grow without one.
const MAX_PLAYERS    = 200;   // whole board
const MAX_PER_SOURCE = 4;     // one address announcing many fake players
const MAX_BODY       = 1024;  // bytes of request body

const startedAt = Date.now();
/** id -> {name, world, source, seen} */
const players = new Map();

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

function sourceOf(req) {
	// Behind a reverse proxy the socket address is the proxy, so the per-source
	// cap would be counting the proxy rather than the player. Trust the first
	// XFF hop only because this sits behind our own front end.
	const fwd = req.headers['x-forwarded-for'];
	if (fwd) return String(fwd).split(',')[0].trim();
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
			purpose: 'presence list for the Minecraft PE 0.6.1 web build Join Game screen',
			repo: 'https://github.com/luispl77/mcpe-0.6.1-web',
			carriesGameTraffic: false,
			players: players.size,
			ttlSeconds: Math.round(TTL_MS / 1000),
			uptimeSeconds: Math.round((Date.now() - startedAt) / 1000)
		});
	}

	if (route === '/list' && req.method === 'GET') {
		sweep();
		const now = Date.now();
		const out = [];
		for (const [id, p] of players)
			out.push({ id: id, name: p.name, world: p.world, age: Math.round((now - p.seen) / 1000) });
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
				if (players.size >= MAX_PLAYERS) return send(res, 503, { error: 'full' });
				let fromSource = 0;
				for (const p of players.values()) if (p.source === source) fromSource++;
				if (fromSource >= MAX_PER_SOURCE) return send(res, 429, { error: 'too many from source' });
			}

			const name = clean(body.name, 20) || 'Player';
			players.set(id, {
				name: name,
				world: clean(body.world, 20),
				source: source,
				seen: Date.now()
			});
			return send(res, 200, { ok: true });
		});
	}

	return send(res, 404, { error: 'not found', service: NAME });
});

server.listen(PORT, HOST, () => {
	console.log(NAME + '/' + VERSION + ' listening on ' + HOST + ':' + PORT +
	            ' (ttl ' + Math.round(TTL_MS / 1000) + 's, max ' + MAX_PLAYERS + ')');
});

// Stopping is a supported outcome, not a failure: the client treats an
// unreachable lobby exactly like an empty LAN, so a clean exit here leaves the
// Join card looking like it did before any of this existed.
for (const signal of ['SIGINT', 'SIGTERM'])
	process.on(signal, () => {
		console.log(NAME + ' stopping; ' + players.size + ' player(s) dropped');
		server.close(() => process.exit(0));
	});
