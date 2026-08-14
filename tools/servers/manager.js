#!/usr/bin/env node
/* mcpe-servers -- makes worlds that outlive the tab that asked for one.
 *
 * The lobby is the board and the switch; this is the only thing that starts a
 * process or owns a directory, and the two talk through a file and a signal
 * rather than through each other. That split is deliberate: the lobby is
 * reachable from the internet and spends its day handling datagrams from
 * strangers, and it should not also be the thing that can spawn.
 *
 *     POST /create  {name, password?}   -> starts a world, returns its id
 *     GET  /list                        -> what is running
 *     POST /stop    {id, key}           -> stops one (operator key)
 *
 * It writes MCPE_SERVERS_FILE, which the lobby reads, and sends the lobby a
 * SIGHUP so it reloads without dropping the players already in a world.
 *
 * It supervises its own children rather than writing systemd units, so it needs
 * no privilege at all: no sudo rule, no polkit policy, nothing that could be
 * turned into more than it was meant to be. The cost is that a server dies when
 * this does, which is why it writes its state down and restarts what it finds.
 */

'use strict';

const http = require('http');
const crypto = require('crypto');
const fs = require('fs');
const path = require('path');
const { spawn } = require('child_process');

const NAME    = 'mcpe-servers';
const VERSION = '1';
const PORT    = Number(process.env.MCPE_MANAGER_PORT || 8478);
const HOST    = process.env.MCPE_MANAGER_HOST || '127.0.0.1';

/** The game binary. Without it this refuses to start rather than 500 per call. */
const BIN = process.env.MCPE_SERVER_BIN || '';

/** Where worlds live. One directory per server, named by its id. */
const ROOT = process.env.MCPE_SERVERS_ROOT || '';

/** The file the lobby reads, and the process to nudge once it changes. */
const SERVERS_FILE = process.env.MCPE_SERVERS_FILE || '';
const LOBBY_PIDFILE = process.env.MCPE_LOBBY_PIDFILE || '';

/* Ceilings. Anyone who can reach the page can reach /create, so every one of
 * these is the difference between a feature and a way to fill a disk.
 *
 * The port range is fixed and small on purpose: it is what the firewall rule is
 * written against, and a server that cannot get a port from it is a server that
 * does not start rather than one that lands somewhere unexpected. */
const MAX_SERVERS  = Number(process.env.MCPE_MAX_SERVERS || 8);
const PORT_FIRST   = Number(process.env.MCPE_PORT_FIRST || 19150);
const PORT_LAST    = Number(process.env.MCPE_PORT_LAST  || 19199);
const MAX_BODY     = 1024;

/* A world with nobody in it is a world that should not still be costing a
 * process and a disk. The lobby knows who is connected; this only knows how
 * long ago it was told somebody was, so the page reports activity and this
 * reaps on silence. Generous, because "nobody is playing right now" is the
 * ordinary state of a server people come back to in the evening. */
const IDLE_MS  = Number(process.env.MCPE_SERVER_IDLE || 6 * 60 * 60 * 1000);
const SWEEP_MS = 5 * 60 * 1000;

/** Stopping somebody else's world is an operator action, not a visitor's. */
const OPERATOR_KEY = process.env.MCPE_OPERATOR_KEY || '';

/* Creating a world costs a process and a directory, so it is rate limited by
 * source in a way that listing is not. Not a security boundary -- a source is
 * an IP and a phone network is one IP -- just a brake. */
const CREATE_PER_HOUR = Number(process.env.MCPE_CREATE_PER_HOUR || 6);

if (!BIN || !ROOT || !SERVERS_FILE) {
	console.error('mcpe-servers: MCPE_SERVER_BIN, MCPE_SERVERS_ROOT and MCPE_SERVERS_FILE are all required');
	process.exit(2);
}
if (!fs.existsSync(BIN)) {
	console.error('mcpe-servers: no such binary: ' + BIN);
	process.exit(2);
}
fs.mkdirSync(ROOT, { recursive: true });

/** id -> {id, name, world, port, salt, passwordHash, child, startedAt, seenAt} */
const running = new Map();
const creates = new Map();   // source -> [timestamps]

/* A name is shown to other players and used for nothing else. The id is what
 * becomes a directory, and it is derived here rather than taken: no dots, no
 * slashes, nothing that could climb out of ROOT however the name was written.
 * The two are separate so that a name can be anything printable without the
 * filesystem having an opinion about it. */
function cleanName(value) {
	if (typeof value !== 'string') return '';
	return value.replace(/[\x00-\x1f\x7f]/g, '').trim().slice(0, 20);
}

function idFor(name) {
	const base = name.toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-+|-+$/g, '').slice(0, 20) || 'world';
	if (!running.has(base)) return base;
	for (let n = 2; n < 1000; n++) {
		const candidate = (base + '-' + n).slice(0, 24);
		if (!running.has(candidate)) return candidate;
	}
	return '';
}

function freePort() {
	for (let p = PORT_FIRST; p <= PORT_LAST; p++) {
		let taken = false;
		for (const s of running.values()) if (s.port === p) { taken = true; break; }
		if (!taken) return p;
	}
	return 0;
}

/* What the lobby reads. Rewritten whole every time rather than patched, because
 * this process is the only writer and a partial file is worse than a late one:
 * the lobby treats an unparseable file as "no dedicated servers", which would
 * take every running world off the board at once. */
/* Set once this process is on its way out.
 *
 * The servers file is two things at once -- what the lobby reads, and this
 * process's own record of what should be running -- and on shutdown those two
 * disagree. Every world is stopped, so `running` empties, so publishing would
 * write [] over the record and restore() would find nothing to bring back: a
 * clean `systemctl stop` would silently deregister every world on the box.
 * Nothing is deleted by that -- the directories stay -- but a world you cannot
 * see and cannot join is gone as far as anyone playing is concerned. */
let leaving = false;

function publish() {
	if (leaving) return;
	const list = [];
	for (const s of running.values())
		list.push({
			id: s.id, name: s.name, world: s.world, mode: s.mode,
			host: '127.0.0.1', port: s.port,
			salt: s.salt, passwordHash: s.passwordHash
		});

	const tmp = SERVERS_FILE + '.tmp';
	fs.writeFileSync(tmp, JSON.stringify(list, null, 1));
	fs.renameSync(tmp, SERVERS_FILE);   // atomic, so the lobby never reads a half-written list

	if (!LOBBY_PIDFILE) return;
	try {
		const pid = Number(fs.readFileSync(LOBBY_PIDFILE, 'utf8').trim());
		if (pid > 0) process.kill(pid, 'SIGHUP');
	} catch (e) {
		// The lobby not being up is not this process's problem to solve; it will
		// read the file when it starts.
	}
}

function start(entry) {
	const dir = path.join(ROOT, entry.id);
	fs.mkdirSync(dir, { recursive: true });

	/* argv, never a shell string. Nothing below is interpolated into a command
	 * line, so a name of `; rm -rf /` is a name and not a second command --
	 * but the id is what reaches the filesystem anyway, and it is [a-z0-9-]. */
	const child = spawn(BIN, [
		'--port', String(entry.port),
		'--externalpath', dir,
		'--cachepath', dir,
		'--leveldir', entry.id,
		'--levelname', entry.name,
		'--gamemode', entry.mode === 'survival' ? 'survival' : 'creative'
	], { cwd: dir, stdio: ['ignore', 'pipe', 'pipe'] });

	child.stdout.resume();
	child.stderr.resume();

	child.on('exit', (code, signal) => {
		console.log('%s exited (code %s, signal %s)', entry.id, code, signal);
		if (running.get(entry.id) === entry) { running.delete(entry.id); publish(); }
	});

	entry.child = child;
	entry.startedAt = Date.now();
	entry.seenAt = Date.now();
	console.log('%s started on %d (pid %d)', entry.id, entry.port, child.pid);
}

function stop(entry) {
	if (!running.has(entry.id)) return;
	running.delete(entry.id);
	try { entry.child.kill('SIGINT'); } catch (e) { /* already gone */ }
	// SIGINT is what main_dedicated traps to save the level; a world that will
	// not go down cleanly still has to go down.
	setTimeout(() => { try { entry.child.kill('SIGKILL'); } catch (e) {} }, 10000).unref();
	publish();
}

function send(res, code, body) {
	const payload = Buffer.from(JSON.stringify(body), 'utf8');
	res.writeHead(code, {
		'content-type': 'application/json',
		'content-length': payload.length,
		'access-control-allow-origin': '*',
		'access-control-allow-headers': 'content-type',
		'cache-control': 'no-store'
	});
	res.end(payload);
}

function readBody(req, done) {
	let size = 0;
	const chunks = [];
	req.on('data', (c) => {
		size += c.length;
		if (size > MAX_BODY) { done(undefined); req.destroy(); return; }
		chunks.push(c);
	});
	req.on('end', () => {
		if (size > MAX_BODY) return;
		try { done(JSON.parse(Buffer.concat(chunks).toString('utf8') || '{}')); }
		catch (e) { done(null); }
	});
}

const sourceOf = (req) =>
	(process.env.MCPE_MANAGER_TRUST_PROXY === '1' && req.headers['x-forwarded-for']) ||
	req.socket.remoteAddress || '?';

const server = http.createServer((req, res) => {
	const route = (req.url || '/').split('?')[0].replace(/\/+$/, '') || '/';

	if (req.method === 'OPTIONS') return send(res, 204, {});

	if (route === '/' && req.method === 'GET')
		return send(res, 200, {
			service: NAME, version: VERSION,
			purpose: 'starts and stops dedicated worlds for the Minecraft PE 0.6.1 web build',
			servers: running.size, max: MAX_SERVERS
		});

	if (route === '/list' && req.method === 'GET') {
		const now = Date.now();
		const out = [];
		for (const s of running.values())
			out.push({
				id: s.id, name: s.name, world: s.world, mode: s.mode,
				locked: !!s.passwordHash,
				upSeconds: Math.round((now - s.startedAt) / 1000),
				idleSeconds: Math.round((now - s.seenAt) / 1000)
			});
		return send(res, 200, { servers: out });
	}

	if (route === '/create' && req.method === 'POST') {
		return readBody(req, (body) => {
			if (body === undefined) return send(res, 413, { error: 'body too large' });
			if (!body) return send(res, 400, { error: 'bad json' });

			const source = sourceOf(req);
			const now = Date.now();
			const recent = (creates.get(source) || []).filter((t) => now - t < 3600000);
			if (recent.length >= CREATE_PER_HOUR)
				return send(res, 429, { error: 'too many worlds from here, try later' });

			if (running.size >= MAX_SERVERS)
				return send(res, 503, { error: 'no free slots' });

			const name = cleanName(body.name) || 'World';
			const id = idFor(name);
			if (!id) return send(res, 503, { error: 'no free name' });

			const port = freePort();
			if (!port) return send(res, 503, { error: 'no free port' });

			const password = typeof body.password === 'string' ? body.password : '';
			const salt = password ? crypto.randomBytes(8).toString('hex') : '';
			const entry = {
				id: id, name: name, world: cleanName(body.world) || 'dedicated', port: port,
				/* Fixed when the world is made and never after: the mode is
				 * written into level.dat at generation, so changing it here
				 * later would just be a lie the board told about the world. */
				mode: body.mode === 'survival' ? 'survival' : 'creative',
				salt: salt,
				passwordHash: password ? crypto.createHash('sha256').update(salt + password).digest('hex') : ''
			};

			running.set(id, entry);
			try { start(entry); } catch (e) {
				running.delete(id);
				return send(res, 500, { error: 'could not start' });
			}
			recent.push(now);
			creates.set(source, recent);
			publish();

			return send(res, 200, { id: id, name: name, locked: !!entry.passwordHash });
		});
	}

	/* Activity, so that idle reaping has something to go on. Anybody can say a
	 * server is busy, which is fine: the failure it buys is a world staying up
	 * that could have gone down, and never one going down underneath a player. */
	if (route === '/seen' && req.method === 'POST') {
		return readBody(req, (body) => {
			if (!body) return send(res, 400, { error: 'bad json' });
			const entry = running.get(String(body.id || ''));
			if (entry) entry.seenAt = Date.now();
			return send(res, 200, { ok: true });
		});
	}

	if (route === '/stop' && req.method === 'POST') {
		return readBody(req, (body) => {
			if (!body) return send(res, 400, { error: 'bad json' });
			if (!OPERATOR_KEY || String(body.key || '') !== OPERATOR_KEY)
				return send(res, 403, { error: 'not an operator' });
			const entry = running.get(String(body.id || ''));
			if (!entry) return send(res, 404, { error: 'no such server' });
			stop(entry);
			return send(res, 200, { ok: true });
		});
	}

	return send(res, 404, { error: 'not found', service: NAME });
});

setInterval(() => {
	const now = Date.now();
	for (const entry of Array.from(running.values()))
		if (now - entry.seenAt > IDLE_MS) {
			console.log('%s idle for %d minutes, stopping', entry.id, Math.round((now - entry.seenAt) / 60000));
			stop(entry);
		}
}, SWEEP_MS).unref();

/* What was running when this last stopped.
 *
 * The servers file is this process's own record as much as it is the lobby's
 * input, so starting up means reading it back and putting the worlds it names
 * back on their ports. Without this a restart of the manager is indistinguishable
 * from every world being deleted -- the children die with it either way, but
 * only this decides whether they come back.
 *
 * Ids and ports are taken from the file rather than reallocated, so a world
 * keeps its place on the board and its directory across a restart. */
function restore() {
	let list;
	try {
		list = JSON.parse(fs.readFileSync(SERVERS_FILE, 'utf8'));
	} catch (e) {
		return;   // nothing was running, or nothing readable. Either way: start empty.
	}
	if (!Array.isArray(list)) return;

	for (const entry of list) {
		const id = typeof entry.id === 'string' ? entry.id.replace(/[^a-z0-9-]/g, '') : '';
		const port = Number(entry.port);
		if (!id || !(port >= PORT_FIRST && port <= PORT_LAST)) continue;
		if (running.size >= MAX_SERVERS || running.has(id)) continue;

		const restored = {
			id: id,
			name: cleanName(entry.name) || 'World',
			world: cleanName(entry.world) || 'dedicated',
			port: port,
			mode: entry.mode === 'survival' ? 'survival' : 'creative',
			salt: typeof entry.salt === 'string' ? entry.salt : '',
			passwordHash: typeof entry.passwordHash === 'string' ? entry.passwordHash : ''
		};
		running.set(id, restored);
		try { start(restored); } catch (e) { running.delete(id); }
	}
	if (running.size) console.log('restored %d world(s)', running.size);
}

restore();
publish();
server.listen(PORT, HOST, () => {
	console.log(NAME + '/' + VERSION + ' listening on ' + HOST + ':' + PORT +
	            ' (bin ' + BIN + ', root ' + ROOT + ', max ' + MAX_SERVERS +
	            ', ports ' + PORT_FIRST + '-' + PORT_LAST + ')');
});

for (const signal of ['SIGINT', 'SIGTERM'])
	process.on(signal, () => {
		leaving = true;   // before any stop(), or the record goes with them
		console.log(NAME + ' stopping; ' + running.size + ' world(s) going down with it');
		// Children are ours, so they go when we do -- but cleanly, so the levels
		// are saved rather than lost.
		for (const entry of Array.from(running.values())) stop(entry);
		setTimeout(() => process.exit(0), 1500);
	});
