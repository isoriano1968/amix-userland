'use strict';

// The extension's end of the vscode-agent protocol (components/developer/
// vscode-agent/README.md).  The agent on AMIX dials in here; both ends prove
// that they know the shared secret, then the extension sends programs and
// runs them.  Plain Node, no vscode API, so tests can drive it.

const crypto = require('crypto');
const net = require('net');
const { EventEmitter } = require('events');

const PROTO = 1;

function enc(s) {
  return Buffer.from(String(s), 'utf8').toString('latin1').replace(/[\x00-\x20%\x7f-\xff]/g,
    (c) => '%' + c.charCodeAt(0).toString(16).toUpperCase().padStart(2, '0'));
}

function dec(s) {
  return Buffer.from(s.replace(/%([0-9A-Fa-f]{2})/g,
    (_, h) => String.fromCharCode(parseInt(h, 16))), 'latin1').toString('utf8');
}

function mac(secret, who, a, b) {
  return crypto.createHmac('sha256', Buffer.from(secret, 'utf8')).update(`${who} ${a} ${b}`).digest('hex');
}

function sameHex(a, b) {
  const x = Buffer.from(a || '', 'utf8');
  const y = Buffer.from(b || '', 'utf8');
  return x.length === y.length && crypto.timingSafeEqual(x, y);
}

// one agent that has signed in
class Agent extends EventEmitter {
  constructor(socket, info) {
    super();
    this.socket = socket;
    this.info = info;            // { user, uid, free, dir, uname, version, address }
    this.runs = new Map();
    this.waiting = [];           // [{ match(verb, args) -> bool, resolve, reject }]
    this.closed = false;
    this.nextId = 1;
  }

  get name() {
    const u = (this.info.uname || '').split(' ');
    return u[1] || this.info.address;
  }

  send(line, payload) {
    if (this.closed) throw new Error('the agent is gone');
    this.socket.write(line + '\n');
    if (payload && payload.length) this.socket.write(payload);
  }

  expect(match, timeoutMs) {
    return new Promise((resolve, reject) => {
      const w = { match, resolve, reject };
      if (timeoutMs) {
        w.timer = setTimeout(() => {
          this.waiting = this.waiting.filter((x) => x !== w);
          reject(new Error('no answer from the agent'));
        }, timeoutMs);
      }
      this.waiting.push(w);
    });
  }

  frame(verb, args, payload) {
    if (verb === 'OUT') {
      const r = this.runs.get(args[0]);
      if (r) r.emit('data', payload);
      return;
    }
    if (verb === 'EXIT' || verb === 'SIGNAL') {
      const r = this.runs.get(args[0]);
      if (r) {
        this.runs.delete(args[0]);
        if (verb === 'EXIT') r.emit('exit', { code: Number(args[1]) });
        else r.emit('exit', { signal: Number(args[1]), core: args[2] === '1' });
      }
      return;
    }
    for (let i = 0; i < this.waiting.length; i++) {
      const w = this.waiting[i];
      const got = w.match(verb, args, payload);
      if (got !== undefined) {
        this.waiting.splice(i, 1);
        if (w.timer) clearTimeout(w.timer);
        if (got instanceof Error) w.reject(got);
        else w.resolve(got);
        return;
      }
    }
  }

  gone(err) {
    if (this.closed) return;
    this.closed = true;
    for (const w of this.waiting) {
      if (w.timer) clearTimeout(w.timer);
      w.reject(err || new Error('the agent went away'));
    }
    this.waiting = [];
    for (const r of this.runs.values()) r.emit('exit', { lost: true });
    this.runs.clear();
    this.emit('close');
  }

  // copy a file into the session folder (programs run from there)
  put(name, data, mode = 0o755) {
    const sha = crypto.createHash('sha256').update(data).digest('hex');
    const p = this.expect((v, a) => {
      if (v === 'OK' && a[0] === 'PUT' && a[1] === name) return true;
      if (v === 'ERR' && a[0] === 'PUT' && a[1] === name) return new Error(dec(a.slice(2).join(' ')));
      return undefined;
    });
    this.send(`PUT ${name} ${mode.toString(8)} ${data.length} ${sha}`, data);
    return p;
  }

  // a file from the program's working folder (run/), or null
  get(name) {
    const p = this.expect((v, a, payload) => {
      if (v === 'FILE' && a[0] === name) {
        const sha = crypto.createHash('sha256').update(payload).digest('hex');
        return sha === a[2] ? payload : new Error(`${name}: sha256 mismatch`);
      }
      if (v === 'ERR' && a[0] === 'GET' && (a[1] === name || a[1] === '-')) return null;
      return undefined;
    });
    this.send(`GET ${name}`);
    return p;
  }

  clean() {
    const p = this.expect((v, a) => (v === 'OK' && a[0] === 'CLEAN' ? a[1]
      : v === 'ERR' && a[0] === 'CLEAN' ? new Error('clean failed') : undefined), 30000);
    this.send('CLEAN');
    return p;
  }

  ping() {
    const tok = crypto.randomBytes(4).toString('hex');
    const p = this.expect((v, a) => (v === 'PONG' && a[0] === tok ? true : undefined), 10000);
    this.send(`PING ${tok}`);
    return p;
  }

  // start argv on a pty; returns a Run that emits 'data' and 'exit'
  run(argv, { env = {}, cols = 80, rows = 24 } = {}) {
    const id = 'r' + this.nextId++;
    const r = new EventEmitter();
    r.id = id;
    r.write = (data) => {
      const b = Buffer.isBuffer(data) ? data : Buffer.from(data, 'utf8');
      if (!this.closed && this.runs.has(id)) this.send(`IN ${id} ${b.length}`, b);
    };
    r.resize = (c, rw) => { if (!this.closed && this.runs.has(id)) this.send(`WINCH ${id} ${c} ${rw}`); };
    r.kill = (sig = 15) => { if (!this.closed && this.runs.has(id)) this.send(`KILL ${id} ${sig}`); };
    this.runs.set(id, r);
    const started = this.expect((v, a) => {
      if (v === 'STARTED' && a[0] === id) return Number(a[1]);
      if (v === 'ERR' && a[0] === 'RUN' && a[1] === id) return new Error(dec(a.slice(2).join(' ')));
      return undefined;
    });
    const envTok = Object.entries(env).map(([k, val]) => enc(`${k}=${val}`));
    this.send(['RUN', id, cols, rows, ...envTok, '--', ...argv.map(enc)].join(' '));
    r.started = started.then((pid) => { r.pid = pid; return pid; },
      (e) => { this.runs.delete(id); throw e; });
    return r;
  }

  close() {
    if (!this.closed) {
      try { this.socket.write('BYE\n'); } catch (e) { /* gone already */ }
      this.socket.end();
    }
  }
}

// frames: "VERB args\n", and for OUT/IN/FILE/PUT a payload whose length the
// header gives
function attachParser(socket, onFrame) {
  let buf = Buffer.alloc(0);
  let pending = null;       // { verb, args, need }
  socket.on('data', (chunk) => {
    buf = buf.length ? Buffer.concat([buf, chunk]) : chunk;
    for (;;) {
      if (pending) {
        if (buf.length < pending.need) return;
        const payload = buf.subarray(0, pending.need);
        buf = buf.subarray(pending.need);
        const p = pending;
        pending = null;
        onFrame(p.verb, p.args, Buffer.from(payload));
        continue;
      }
      const nl = buf.indexOf(10);
      if (nl < 0) {
        if (buf.length > 65536) socket.destroy(new Error('line too long'));
        return;
      }
      const line = buf.subarray(0, nl).toString('latin1').replace(/\r$/, '');
      buf = buf.subarray(nl + 1);
      const t = line.split(' ').filter((x) => x.length);
      if (!t.length) continue;
      const verb = t[0];
      const args = t.slice(1);
      const need = verb === 'OUT' ? Number(args[1]) : verb === 'FILE' ? Number(args[1]) : 0;
      if (need > 0) pending = { verb, args: verb === 'OUT' ? [args[0]] : args, need };
      else onFrame(verb, verb === 'OUT' ? [args[0]] : args, Buffer.alloc(0));
    }
  });
}

// listens for agents; emits 'agent' (Agent), 'reject' ({address, reason})
class AgentServer extends EventEmitter {
  constructor(secretFn) {
    super();
    this.secretFn = secretFn;    // () => secret string, or '' when there is none
    this.server = null;
    this.agents = new Set();
  }

  listen(port, host) {
    return new Promise((resolve, reject) => {
      const srv = net.createServer((s) => this.accept(s));
      srv.once('error', reject);
      srv.listen(port, host, () => {
        srv.removeListener('error', reject);
        srv.on('error', (e) => this.emit('error', e));
        this.server = srv;
        resolve(srv.address());
      });
    });
  }

  accept(socket) {
    const address = `${socket.remoteAddress}:${socket.remotePort}`;
    const secret = this.secretFn();
    socket.setNoDelay(true);
    let agent = null;
    let state = 'hello';
    let nonceB = '';
    let nonceA = '';
    let version = '';
    const timer = setTimeout(() => {
      if (!agent) { this.emit('reject', { address, reason: 'no sign-in' }); socket.destroy(); }
    }, 20000);
    socket.on('error', () => {});
    socket.on('close', () => {
      clearTimeout(timer);
      if (agent) { this.agents.delete(agent); agent.gone(); }
    });
    attachParser(socket, (verb, args, payload) => {
      if (agent) { agent.frame(verb, args, payload); return; }
      if (state === 'hello') {
        if (verb !== 'AMIX-AGENT' || Number(args[0]) !== PROTO || !/^[0-9a-f]{16,64}$/.test(args[2] || '')) {
          this.emit('reject', { address, reason: `not an AMIX agent of protocol ${PROTO}` });
          socket.destroy();
          return;
        }
        if (!secret) {
          this.emit('reject', { address, reason: 'no agent key set in VS Code' });
          socket.destroy();
          return;
        }
        version = args[1];
        nonceA = args[2];
        nonceB = crypto.randomBytes(16).toString('hex');
        socket.write(`AUTH ${nonceB} ${mac(secret, 'ext', nonceA, nonceB)}\n`);
        state = 'auth';
        return;
      }
      if (state === 'auth') {
        if (verb === 'OK' && sameHex(args[0], mac(secret, 'agent', nonceB, nonceA))) {
          const info = { version, address };
          for (const kv of args.slice(1)) {
            const i = kv.indexOf('=');
            if (i > 0) info[kv.slice(0, i)] = dec(kv.slice(i + 1));
          }
          clearTimeout(timer);
          agent = new Agent(socket, info);
          this.agents.add(agent);
          this.emit('agent', agent);
          return;
        }
        this.emit('reject', {
          address,
          reason: verb === 'ERR' ? 'the agent has a different key' : 'the agent could not prove its key',
        });
        socket.destroy();
      }
    });
  }

  close() {
    for (const a of this.agents) a.socket.destroy();
    this.agents.clear();
    if (this.server) this.server.close();
    this.server = null;
  }
}

function newKey() {
  return crypto.randomBytes(32).toString('hex');
}

module.exports = { AgentServer, Agent, newKey, enc, dec, mac };
