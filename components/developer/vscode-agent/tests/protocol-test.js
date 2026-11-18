'use strict';
// Drives a host build of vscode-agent through the extension's own protocol
// code (../../vscode/src/agent-link.js).
//   node tests/protocol-test.js AGENT_BINARY
const assert = require('assert');
const fs = require('fs');
const os = require('os');
const path = require('path');
const { spawn } = require('child_process');
const { AgentServer, newKey } = require('../../vscode/src/agent-link.js');

const bin = path.resolve(process.argv[2] || 'build/host/vscode-agent');
const work = fs.mkdtempSync(path.join(os.tmpdir(), 'vscode-agent-test-'));
const tmpdir = path.join(work, 'tmp');
fs.mkdirSync(tmpdir);

let failed = 0;
const procs = [];

function conf(port, secret) {
  const f = path.join(work, `conf.${port}.${secret.slice(0, 4)}`);
  fs.writeFileSync(f, `# test\nconnect 127.0.0.1:${port}\nsecret ${secret}\ntmpdir ${tmpdir}\n` +
    'reserve-mb 1\nretry 1\n', { mode: 0o600 });
  return f;
}

function startAgent(c) {
  const p = spawn(bin, ['-f', '-c', c], { stdio: ['ignore', 'ignore', 'pipe'] });
  p.log = '';
  p.stderr.on('data', (d) => { p.log += d; });
  procs.push(p);
  return p;
}

function once(em, ev, ms = 10000) {
  return new Promise((res, rej) => {
    const t = setTimeout(() => rej(new Error(`timeout waiting for ${ev}`)), ms);
    em.once(ev, (v) => { clearTimeout(t); res(v); });
  });
}

function collect(run) {
  let out = '';
  run.on('data', (d) => { out += d.toString('latin1'); });
  return () => out;
}

async function test(name, fn) {
  try {
    await fn();
    console.log(`ok   ${name}`);
  } catch (e) {
    failed++;
    console.log(`FAIL ${name}: ${e.stack || e}`);
  }
}

(async () => {
  const key = newKey();
  const srv = new AgentServer(() => key);
  const { port } = await srv.listen(0, '127.0.0.1');
  const rejects = [];
  srv.on('reject', (r) => rejects.push(r.reason));

  const agentProc = startAgent(conf(port, key));
  const agent = await once(srv, 'agent');

  await test('sign-in gives user, session folder and uname', async () => {
    assert.strictEqual(agent.info.user, os.userInfo().username);
    assert.ok(agent.info.dir.startsWith(tmpdir + '/vscode-agent.'), agent.info.dir);
    assert.ok(fs.statSync(path.join(agent.info.dir, 'run')).isDirectory());
    assert.ok(/Linux|AMIX|UNIX_System_V/.test(agent.info.uname), agent.info.uname);
    assert.ok(Number(agent.info.free) > 0);
  });

  await test('ping', async () => { assert.strictEqual(await agent.ping(), true); });

  await test('put a program and run it with arguments and environment', async () => {
    const script = '#!/bin/sh\necho "args:$1|$2"\necho "env:$GREETING"\necho "cwd:`pwd`"\nexit 3\n';
    await agent.put('hello.sh', Buffer.from(script));
    const st = fs.statSync(path.join(agent.info.dir, 'hello.sh'));
    assert.strictEqual(st.mode & 0o777, 0o755);
    const r = agent.run(['hello.sh', 'a b', '100%'], { env: { GREETING: 'grüß dich' } });
    const out = collect(r);
    await r.started;
    const ex = await once(r, 'exit');
    assert.deepStrictEqual(ex, { code: 3 });
    const o = Buffer.from(out(), 'latin1').toString('utf8');
    assert.ok(o.includes('args:a b|100%'), o);
    assert.ok(o.includes('env:grüß dich'), o);
    assert.ok(o.includes(`cwd:${agent.info.dir}/run`), o);
  });

  await test('a pty: input, window size, tty', async () => {
    const r = agent.run(['/bin/sh', '-c', 'tty; stty size; read x; echo "got:$x"; exit 0'],
      { cols: 100, rows: 30 });
    const out = collect(r);
    await r.started;
    await new Promise((res) => setTimeout(res, 300));
    r.write('typed\r');
    const ex = await once(r, 'exit');
    assert.strictEqual(ex.code, 0);
    assert.ok(/\/dev\/pts\/\d+/.test(out()), out());
    assert.ok(out().includes('30 100'), out());
    assert.ok(out().includes('got:typed'), out());
  });

  await test('resize reaches the program', async () => {
    const r = agent.run(['/bin/sh', '-c', 'read x; stty size'], { cols: 80, rows: 24 });
    const out = collect(r);
    await r.started;
    r.resize(132, 50);
    await new Promise((res) => setTimeout(res, 300));
    r.write('\r');
    await once(r, 'exit');
    assert.ok(out().includes('50 132'), out());
  });

  await test('kill gives a signal', async () => {
    const r = agent.run(['/bin/sh', '-c', 'sleep 30']);
    await r.started;
    r.kill(9);
    const ex = await once(r, 'exit');
    assert.strictEqual(ex.signal, 9);
  });

  await test('a missing program exits 127', async () => {
    const r = agent.run(['no-such-program-here']);
    const out = collect(r);
    await r.started;
    const ex = await once(r, 'exit');
    assert.strictEqual(ex.code, 127);
    assert.ok(out().includes('no-such-program-here'), out());
  });

  await test('get a file the program wrote; a missing one is null', async () => {
    const r = agent.run(['/bin/sh', '-c', 'printf "x%.0s" $(seq 1 20000) > out.txt']);
    await r.started;
    await once(r, 'exit');
    const b = await agent.get('out.txt');
    assert.strictEqual(b.length, 20000);
    assert.strictEqual(await agent.get('nothing'), null);
  });

  await test('bad names and a bad checksum are refused', async () => {
    await assert.rejects(agent.put('../evil', Buffer.from('x')), /bad name/);
    await assert.rejects(agent.put('.hidden', Buffer.from('x')), /bad name/);
    const crypto = require('crypto');
    // a wrong hash: send by hand
    const data = Buffer.from('abc');
    const p = agent.expect((v, a) => (a[1] === 'bad.bin' ? (v === 'ERR' ? a.slice(2).join(' ') : 'ok') : undefined));
    agent.send(`PUT bad.bin 755 3 ${crypto.createHash('sha256').update('abd').digest('hex')}`, data);
    assert.ok(/mismatch/.test(await p));
    assert.ok(!fs.existsSync(path.join(agent.info.dir, 'bad.bin')));
    assert.strictEqual(await agent.ping(), true, 'still in step after refused payloads');
  });

  await test('a big file arrives whole', async () => {
    const big = require('crypto').randomBytes(3 * 1024 * 1024 + 17);
    await agent.put('big.bin', big, 0o644);
    assert.ok(fs.readFileSync(path.join(agent.info.dir, 'big.bin')).equals(big));
  });

  await test('clean empties the session', async () => {
    const r = agent.run(['/bin/sh', '-c', 'mkdir -p d/e; echo x > d/e/f; ln -s / link']);
    await r.started;
    await once(r, 'exit');
    const old = agent.info.dir;
    const dir = await agent.clean();
    assert.ok(!fs.existsSync(old) || old === dir);
    assert.deepStrictEqual(fs.readdirSync(dir), ['run']);
    assert.deepStrictEqual(fs.readdirSync(path.join(dir, 'run')), []);
    agent.info.dir = dir;
  });

  await test('disconnect ends programs, removes the session, agent dials again', async () => {
    const r = agent.run(['/bin/sh', '-c', 'sleep 60']);
    const pid = await r.started;
    const dir = agent.info.dir;
    await agent.put('marker', Buffer.from('x'));
    const back = once(srv, 'agent', 15000);
    agent.close();
    const again = await back;
    // the same agent may reuse the folder name: it must come back empty
    assert.ok(!fs.existsSync(path.join(dir, 'marker')), `${dir} was not cleaned`);
    assert.deepStrictEqual(fs.readdirSync(again.info.dir), ['run']);
    let alive = true;
    try { process.kill(pid, 0); } catch (e) { alive = false; }
    assert.ok(!alive, `pid ${pid} still runs`);
    again.close();
  });

  await test('the agent refuses a listener with the wrong key, and vice versa', async () => {
    const other = new AgentServer(() => newKey());
    const o = await other.listen(0, '127.0.0.1');
    const rej = [];
    other.on('reject', (x) => rej.push(x.reason));
    startAgent(conf(o.port, key));
    await new Promise((res) => setTimeout(res, 2500));
    assert.strictEqual(other.agents.size, 0);
    assert.ok(rej.some((x) => /different key|could not prove/.test(x)), JSON.stringify(rej));
    other.close();
  });

  srv.close();
  for (const p of procs) p.kill('SIGTERM');
  await new Promise((res) => setTimeout(res, 500));
  const left = fs.readdirSync(tmpdir);
  if (left.length) { failed++; console.log(`FAIL session folders left: ${left}`); }
  if (failed) console.log(agentProc.log);
  fs.rmSync(work, { recursive: true, force: true });
  console.log(failed ? `${failed} failed` : 'all passed');
  process.exit(failed ? 1 : 0);
})().catch((e) => { console.error(e); for (const p of procs) p.kill(); process.exit(1); });
