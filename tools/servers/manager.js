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

/* Accounts.
 *
 * Until now a world was owned by a *browser*: /create handed back a secret and
 * whichever localStorage held it could delete the world. That is the most that
 * can be checked without accounts, and it is not much -- clear your site data
 * and your worlds are nobody's, open the page on your phone and they are not
 * yours there either.
 *
 * An account is a name and a password and nothing else. No email, no recovery,
 * no profile: this is a login for the server list on a Minecraft build from
 * 2013, and every field that is not here is a field that cannot leak. It buys
 * exactly three things -- ownership that follows you between devices, a name
 * other players see, and a cap so one person cannot fill the box.
 *
 * Deliberately not a login for the *game*. Single player, joining, and hosting
 * your own tab all work signed out, exactly as they did.
 */
const ACCOUNTS_FILE = process.env.MCPE_ACCOUNTS_FILE || path.join(ROOT || '.', 'accounts.json');
const MAX_ACCOUNTS  = Number(process.env.MCPE_MAX_ACCOUNTS || 200);

/** How many worlds one account may have up at once. */
const WORLDS_PER_ACCOUNT = Number(process.env.MCPE_WORLDS_PER_ACCOUNT || 3);

/* Guessing a password should cost something. Per source and generous, because
 * a source is an IP and a phone network is one IP -- the same caveat as
 * CREATE_PER_HOUR, and the same modest goal: a brake, not a boundary. */
const LOGIN_PER_HOUR    = Number(process.env.MCPE_LOGIN_PER_HOUR || 30);
const REGISTER_PER_HOUR = Number(process.env.MCPE_REGISTER_PER_HOUR || 5);

const TOKEN_MS = 30 * 24 * 3600 * 1000;

/** How much one world may say per second before it is quietened. */
const LOG_LINES_PER_SEC = Number(process.env.MCPE_LOG_LINES_PER_SEC || 20);

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
const logins  = new Map();   // source -> [timestamps]
const signups = new Map();   // source -> [timestamps]

/* ---------------------------------------------------------------------------
 * Accounts
 * ------------------------------------------------------------------------- */

/** user -> {user, salt, hash, madeAt}. `hash` is scrypt, never the password. */
const accounts = new Map();

/** Signs tokens. Persisted, so a restart does not sign everybody out. */
let tokenSecret = '';

function loadAccounts() {
	let blob;
	try {
		blob = JSON.parse(fs.readFileSync(ACCOUNTS_FILE, 'utf8'));
	} catch (e) {
		tokenSecret = crypto.randomBytes(32).toString('hex');
		return;
	}
	tokenSecret = typeof blob.secret === 'string' && blob.secret.length >= 32
		? blob.secret : crypto.randomBytes(32).toString('hex');
	for (const a of (Array.isArray(blob.accounts) ? blob.accounts : []))
		if (typeof a.user === 'string' && typeof a.salt === 'string' && typeof a.hash === 'string')
			accounts.set(a.user, { user: a.user, salt: a.salt, hash: a.hash, madeAt: Number(a.madeAt) || 0 });
	if (accounts.size) console.log('loaded %d account(s)', accounts.size);
}

function saveAccounts() {
	const blob = { secret: tokenSecret, accounts: Array.from(accounts.values()) };
	const tmp = ACCOUNTS_FILE + '.tmp';
	// 0600 before anything is in it: this file is the only thing standing
	// between a readable disk and everybody's worlds.
	fs.writeFileSync(tmp, JSON.stringify(blob, null, 1), { mode: 0o600 });
	fs.renameSync(tmp, ACCOUNTS_FILE);
}

/* A name is [a-z0-9_], because it is a key here, a directory-adjacent thing
 * nowhere, and something other players read everywhere. Lowercased so that
 * `Luis` and `luis` cannot be two people -- a list where two rows look the same
 * is a list where somebody joins the wrong one. */
function cleanUser(value) {
	if (typeof value !== 'string') return '';
	const user = value.trim().toLowerCase();
	return /^[a-z0-9_]{3,16}$/.test(user) ? user : '';
}

/** scrypt, not sha256: this one is worth being slow to check. */
function hashPassword(password, salt, done) {
	crypto.scrypt(password, salt, 32, (err, key) => done(err ? '' : key.toString('hex')));
}

/* A token is signed rather than stored.
 *
 * The alternative is a table of live sessions, which has to be persisted, swept
 * and capped, and buys one thing this does not have: server-side logout. That
 * is not worth a fourth piece of state here -- signing out drops the token in
 * the browser, and changing your password invalidates every token ever issued,
 * because the hash is part of what is signed. */
function tokenFor(account) {
	const expiry = Date.now() + TOKEN_MS;
	const body = account.user + '.' + expiry;
	return body + '.' + crypto.createHmac('sha256', tokenSecret)
		.update(body + '.' + account.hash).digest('hex');
}

/** The account this token names, or null. */
function accountFor(token) {
	if (typeof token !== 'string') return null;
	const parts = token.split('.');
	if (parts.length !== 3) return null;

	const account = accounts.get(parts[0]);
	if (!account) return null;

	const expiry = Number(parts[1]);
	if (!(expiry > Date.now())) return null;

	const body = parts[0] + '.' + parts[1];
	const want = crypto.createHmac('sha256', tokenSecret)
		.update(body + '.' + account.hash).digest('hex');
	// Shape first: timingSafeEqual throws on a length mismatch.
	if (!/^[0-9a-f]{64}$/.test(parts[2])) return null;
	if (!crypto.timingSafeEqual(Buffer.from(parts[2], 'hex'), Buffer.from(want, 'hex'))) return null;

	return account;
}

/** How many worlds this account has up. */
function worldsOf(user) {
	let n = 0;
	for (const s of running.values()) if (s.ownerAccount === user) n++;
	return n;
}

function rateLimited(table, source, perHour) {
	const now = Date.now();
	const recent = (table.get(source) || []).filter((t) => now - t < 3600000);
	if (recent.length >= perHour) return true;
	recent.push(now);
	table.set(source, recent);
	return false;
}

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
			id: s.id, name: s.name, world: s.world, mode: s.mode, seed: s.seed,
			host: '127.0.0.1', port: s.port,
			salt: s.salt, passwordHash: s.passwordHash,
			ownerHash: s.ownerHash, ownerAccount: s.ownerAccount
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

/* What a world says, into the journal with its id in front of it.
 *
 * Both streams were resume()d -- drained and dropped on the floor. That is fine
 * right up until somebody asks why a server feels slow, at which point there is
 * nothing whatsoever to look at: no tick rate, no level-load timings, not even
 * the line the binary prints when it finishes generating. A world that cannot
 * say anything cannot be diagnosed, only guessed at.
 *
 * Whole lines only, and with a brake on. This is the one place in this process
 * where something else decides how much gets written, and a server stuck in a
 * printing loop must not fill the journal or this process's memory. */
function relay(stream, entry) {
	let held = '';
	stream.setEncoding('utf8');
	stream.on('data', (chunk) => {
		held += chunk;
		// A "line" longer than this is not a line; keep the tail and move on
		// rather than growing a buffer without limit.
		if (held.length > 8192) held = held.slice(-8192);

		let nl;
		while ((nl = held.indexOf('\n')) >= 0) {
			const line = held.slice(0, nl).replace(/[\x00-\x08\x0b-\x1f\x7f]/g, '').trim();
			held = held.slice(nl + 1);
			if (!line) continue;

			const now = Date.now();
			if (now - entry.saidAt >= 1000) { entry.saidAt = now; entry.said = 0; }
			if (++entry.said > LOG_LINES_PER_SEC) {
				if (entry.said === LOG_LINES_PER_SEC + 1)
					console.log('%s: (saying too much, quietening for a second)', entry.id);
				continue;
			}
			console.log('%s: %s', entry.id, line);
		}
	});
}

function start(entry) {
	// Before relay() below attaches, because its handlers read these and
	// arithmetic on undefined is a brake that silently does nothing.
	entry.said = 0;
	entry.saidAt = Date.now();

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
	].concat(entry.seed ? ['--seed', entry.seed] : []), { cwd: dir, stdio: ['ignore', 'pipe', 'pipe'] });

	relay(child.stdout, entry);
	relay(child.stderr, entry);

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
			servers: running.size, max: MAX_SERVERS,
			accounts: accounts.size
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

	/* ---------------------------------------------------------------------
	 * Accounts
	 *
	 * Three routes and no more. Registering and signing in answer the same
	 * shape, because the screen that calls them is one screen with two
	 * buttons, and /whoami is what a browser holding a month-old token uses
	 * to find out whether it is still anybody.
	 *
	 * Every failure below is the same sentence -- "that name and password do
	 * not match" -- whether the name exists or not. A login that says which
	 * half was wrong is a login that tells a stranger which names are real.
	 * ------------------------------------------------------------------- */

	if ((route === '/register' || route === '/login') && req.method === 'POST') {
		return readBody(req, (body) => {
			if (body === undefined) return send(res, 413, { error: 'body too large' });
			if (!body) return send(res, 400, { error: 'bad json' });

			const registering = route === '/register';
			const source = sourceOf(req);
			if (rateLimited(registering ? signups : logins, source,
			                registering ? REGISTER_PER_HOUR : LOGIN_PER_HOUR))
				return send(res, 429, { error: 'too many tries from here, wait a while' });

			const user = cleanUser(body.user);
			const pass = typeof body.pass === 'string' ? body.pass : '';

			if (registering) {
				// Said plainly, because these are rules about what you typed
				// and not hints about who else exists.
				if (!user) return send(res, 400, { error: 'name: 3-16 letters, numbers or _' });
				if (pass.length < 6 || pass.length > 64)
					return send(res, 400, { error: 'password: at least 6 characters' });
				if (accounts.has(user)) return send(res, 409, { error: 'that name is taken' });
				if (accounts.size >= MAX_ACCOUNTS) return send(res, 503, { error: 'no room for more accounts' });

				const salt = crypto.randomBytes(16).toString('hex');
				return hashPassword(pass, salt, (hash) => {
					if (!hash) return send(res, 500, { error: 'could not make the account' });
					const account = { user: user, salt: salt, hash: hash, madeAt: Date.now() };
					accounts.set(user, account);
					try { saveAccounts(); } catch (e) {
						accounts.delete(user);
						console.log('could not write accounts: %s', e.message);
						return send(res, 500, { error: 'could not make the account' });
					}
					console.log('account %s created', user);
					return send(res, 200, { ok: true, user: user, token: tokenFor(account) });
				});
			}

			const account = user && accounts.get(user);
			if (!account || !pass) return send(res, 403, { error: 'wrong name or password' });

			return hashPassword(pass, account.salt, (hash) => {
				if (!hash || hash.length !== account.hash.length ||
				    !crypto.timingSafeEqual(Buffer.from(hash, 'hex'), Buffer.from(account.hash, 'hex')))
					return send(res, 403, { error: 'wrong name or password' });
				return send(res, 200, { ok: true, user: account.user, token: tokenFor(account) });
			});
		});
	}

	/* Is this token still anybody, and what does it own?
	 *
	 * Both in one answer because the page asks both at the same moment and for
	 * the same reason: the game asks canManageServer() as the selection moves,
	 * which has to be answered without a round trip, so the page keeps the list
	 * and refreshes it rather than asking per row. */
	if (route === '/whoami' && req.method === 'POST') {
		return readBody(req, (body) => {
			if (!body) return send(res, 400, { error: 'bad json' });
			const account = accountFor(body.token);
			if (!account) return send(res, 403, { error: 'not signed in' });

			const mine = [];
			for (const s of running.values())
				if (s.ownerAccount === account.user) mine.push(s.id);
			return send(res, 200, { ok: true, user: account.user, servers: mine, max: WORLDS_PER_ACCOUNT });
		});
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

			/* Signed in or not.
			 *
			 * Both are allowed, and which one you used decides who owns the
			 * world: an account if there is one, otherwise the browser secret
			 * this has always minted. Keeping the anonymous path is what lets
			 * accounts be added without invalidating a single world that
			 * already exists -- the check below simply grew a second way to
			 * pass, and every old world still answers to its old key. */
			const account = accountFor(body.token);
			if (account && worldsOf(account.user) >= WORLDS_PER_ACCOUNT)
				return send(res, 429, {
					error: 'you already have ' + WORLDS_PER_ACCOUNT + ' servers up'
				});

			const name = cleanName(body.name) || 'World';
			const id = idFor(name);
			if (!id) return send(res, 503, { error: 'no free name' });

			const port = freePort();
			if (!port) return send(res, 503, { error: 'no free port' });

			const password = typeof body.password === 'string' ? body.password : '';
			const salt = password ? crypto.randomBytes(8).toString('hex') : '';

			/* Who may delete or reconfigure this world. Handed back once, to the
			 * page that asked for it, and kept here only as a hash -- so this
			 * file being readable does not hand anybody else the world.
			 *
			 * Only minted for a world made signed out. With an account there is
			 * already something durable to hang ownership on, and a second key
			 * would only be a second way to lose it. */
			const owner = account ? '' : crypto.randomBytes(16).toString('hex');
			const entry = {
				id: id, name: name, world: cleanName(body.world) || 'dedicated', port: port,
				/* Fixed when the world is made and never after: the mode is
				 * written into level.dat at generation, so changing it here
				 * later would just be a lie the board told about the world. */
				mode: body.mode === 'survival' ? 'survival' : 'creative',
				/* Digits only, and capped: it goes on a command line as an
				 * argument, and a seed is a number however it was typed. Empty
				 * means the server picks one. */
				seed: String(body.seed || '').replace(/[^0-9-]/g, '').slice(0, 19),
				salt: salt,
				passwordHash: password ? crypto.createHash('sha256').update(salt + password).digest('hex') : '',
				ownerHash: owner ? crypto.createHash('sha256').update(owner).digest('hex') : '',
				ownerAccount: account ? account.user : ''
			};

			running.set(id, entry);
			try { start(entry); } catch (e) {
				running.delete(id);
				return send(res, 500, { error: 'could not start' });
			}
			recent.push(now);
			creates.set(source, recent);
			publish();

			// The only time the owner secret is ever sent anywhere. Absent when
			// an account owns the world, which is the point of having one.
			return send(res, 200, {
				id: id, name: name, locked: !!entry.passwordHash,
				owner: owner, ownerAccount: entry.ownerAccount
			});
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

	/* Who is asking, for the routes that change a world rather than list one.
	 *
	 * Two ways in and no third: the browser that made the world, or an operator
	 * with the key. Anonymous is not one of them -- the page is public, and a
	 * delete button anybody could reach is a way to lose somebody's evening. */
	function mayManage(entry, body) {
		if (OPERATOR_KEY && String(body.key || '') === OPERATOR_KEY) return true;

		/* A world is owned one way or the other and never both, so this is a
		 * branch rather than two chances: an account-owned world has no owner
		 * hash to fall back to, and letting it fall through would only mean
		 * comparing a secret against an empty string. */
		if (entry.ownerAccount) {
			const account = accountFor(body.token);
			return !!account && account.user === entry.ownerAccount;
		}

		const owner = String(body.owner || '');
		if (!owner || !entry.ownerHash) return false;
		// A world made before owners existed has no hash, and a file edited by
		// hand could have a malformed one; timingSafeEqual throws on a length
		// mismatch, so the shape is checked before the value.
		if (!/^[0-9a-f]{64}$/.test(entry.ownerHash)) return false;
		const given = crypto.createHash('sha256').update(owner).digest('hex');
		return crypto.timingSafeEqual(Buffer.from(given, 'hex'), Buffer.from(entry.ownerHash, 'hex'));
	}

	/* Deleting a world stops it and moves its directory aside; it does not
	 * remove anything. A world is the one thing here that cannot be rebuilt --
	 * the binary, the page and the services can all be reinstalled from the
	 * repo, and what somebody spent an evening building cannot. The dot prefix
	 * keeps the leftovers out of the id space, so restore() never finds them
	 * and an operator can sweep them up whenever. */
	if (route === '/delete' && req.method === 'POST') {
		return readBody(req, (body) => {
			if (!body) return send(res, 400, { error: 'bad json' });
			const entry = running.get(String(body.id || ''));
			if (!entry) return send(res, 404, { error: 'no such server' });
			if (!mayManage(entry, body)) return send(res, 403, { error: 'not yours' });

			stop(entry);
			const dir = path.join(ROOT, entry.id);
			try {
				const stamp = new Date().toISOString().replace(/[-:T]/g, '').slice(0, 14);
				fs.renameSync(dir, path.join(ROOT, '.deleted-' + entry.id + '-' + stamp));
			} catch (e) {
				console.log('%s: could not archive %s: %s', entry.id, dir, e.message);
			}
			console.log('%s deleted', entry.id);
			return send(res, 200, { ok: true });
		});
	}

	/* Renaming, and setting or clearing the password.
	 *
	 * The mode is deliberately not here: it is written into level.dat when the
	 * world is generated, so changing it on this side would only be the board
	 * telling a story about a world that disagreed.
	 *
	 * A password change takes effect on the next datagram, because the lock
	 * lives in the lobby's switch and the lobby re-reads this file whenever it
	 * changes -- which is also why nothing needs restarting for it. */
	if (route === '/configure' && req.method === 'POST') {
		return readBody(req, (body) => {
			if (!body) return send(res, 400, { error: 'bad json' });
			const entry = running.get(String(body.id || ''));
			if (!entry) return send(res, 404, { error: 'no such server' });
			if (!mayManage(entry, body)) return send(res, 403, { error: 'not yours' });

			if (typeof body.name === 'string' && cleanName(body.name))
				entry.name = cleanName(body.name);

			/* Handing a world to an account, which only an operator may do.
			 *
			 * There is deliberately no route by which a *visitor* claims an
			 * unowned world -- that would be a race whose prize is somebody
			 * else's evening, and every world made from now on has an owner
			 * from the moment it exists. This exists for the handful made
			 * before accounts did, and it is a person with the key deciding,
			 * not the first browser to ask. */
			if (OPERATOR_KEY && String(body.key || '') === OPERATOR_KEY &&
			    typeof body.ownerAccount === 'string') {
				const to = cleanUser(body.ownerAccount);
				if (to && !accounts.has(to)) return send(res, 404, { error: 'no such account' });
				entry.ownerAccount = to;
				entry.ownerHash = '';   // one owner, one way
				console.log('%s now belongs to %s', entry.id, to || '(nobody)');
			}

			if (typeof body.password === 'string') {
				if (body.password) {
					entry.salt = crypto.randomBytes(8).toString('hex');
					entry.passwordHash = crypto.createHash('sha256')
						.update(entry.salt + body.password).digest('hex');
				} else {
					entry.salt = '';
					entry.passwordHash = '';
				}
			}

			publish();
			return send(res, 200, {
				ok: true, name: entry.name, locked: !!entry.passwordHash,
				ownerAccount: entry.ownerAccount || ''
			});
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
			seed: String(entry.seed || '').replace(/[^0-9-]/g, '').slice(0, 19),
			salt: typeof entry.salt === 'string' ? entry.salt : '',
			passwordHash: typeof entry.passwordHash === 'string' ? entry.passwordHash : '',
			ownerHash: typeof entry.ownerHash === 'string' ? entry.ownerHash : '',
			ownerAccount: cleanUser(entry.ownerAccount)
		};
		running.set(id, restored);
		try { start(restored); } catch (e) { running.delete(id); }
	}
	if (running.size) console.log('restored %d world(s)', running.size);
}

loadAccounts();
restore();
publish();
server.listen(PORT, HOST, () => {
	console.log(NAME + '/' + VERSION + ' listening on ' + HOST + ':' + PORT +
	            ' (bin ' + BIN + ', root ' + ROOT + ', max ' + MAX_SERVERS +
	            ', ports ' + PORT_FIRST + '-' + PORT_LAST + ')');
});

for (const signal of ['SIGINT', 'SIGTERM'])
	process.on(signal, () => {
		if (leaving) return;
		leaving = true;   // before any stop(), or the record goes with them

		const children = Array.from(running.values());
		console.log(NAME + ' stopping; ' + children.length + ' world(s) going down with it');
		// Children are ours, so they go when we do -- but cleanly, so the levels
		// are saved rather than lost.
		for (const entry of children) stop(entry);

		/* Wait for them to actually be gone.
		 *
		 * This used to count to 1500ms and exit. A world saves a 21 MB
		 * chunks.dat on the way down and does not always manage it in a second
		 * and a half, and exiting first hands the rest to systemd's SIGKILL --
		 * so the save that the SIGINT above exists to allow was being cut off
		 * by the very thing that sent it. Waiting is not a courtesy here; it is
		 * the whole point of stopping them one at a time.
		 *
		 * Bounded, because a world that will not go down must not wedge a
		 * restart. TimeoutStopSec is 30s and SendSIGKILL sweeps up after it, so
		 * this stays well inside that and lets systemd be the backstop. */
		let pending = 0;
		let done = false;
		const leave = () => { if (done) return; done = true; process.exit(0); };

		for (const entry of children) {
			if (!entry.child || entry.child.exitCode !== null || entry.child.signalCode !== null)
				continue;
			pending++;
			entry.child.once('exit', () => {
				if (--pending === 0) {
					console.log('all worlds saved and stopped');
					leave();
				}
			});
		}

		if (!pending) return leave();
		setTimeout(() => {
			console.log('gave up waiting for ' + pending + ' world(s)');
			leave();
		}, 20000);
	});
