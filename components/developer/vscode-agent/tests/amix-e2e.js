'use strict';
// The agent on AMIX (FS-UAE), driven through the extension's protocol code.
//   node tests/amix-e2e.js KEY PORT [USER]
// Waits for the agent to dial in, then checks it as root runs it: programs
// as USER on a STREAMS pty, AMIX's user licensing (a fork with a terminal),
// input, window size, signals, core files, GET, CLEAN, reconnect.
const assert = require('assert');
const crypto = require('crypto');
const { AgentServer } = require('../../vscode/src/agent-link.js');

const [key, port, user = 'guest'] = process.argv.slice(2);
let failed = 0;

function once(em, ev, ms) {
  return new Promise((res, rej) => {
    const t = setTimeout(() => rej(new Error(`timeout waiting for ${ev}`)), ms);
    em.once(ev, (v) => { clearTimeout(t); res(v); });
  });
}

async function runIt(agent, argv, opts = {}, input) {
  const r = agent.run(argv, opts);
  let out = '';
  r.on('data', (d) => { out += d.toString('latin1'); });
  // listening from the start: a program may end while input is still being sent
  const exited = once(r, 'exit', opts.timeout || 60000);
  exited.catch(() => {});
  await r.started;
  if (input) {
    await new Promise((res) => setTimeout(res, 1500));
    for (const i of [].concat(input)) {
      if (typeof i === 'function') i(r);
      else r.write(i);
      await new Promise((res) => setTimeout(res, 1000));
    }
  }
  let ex;
  try {
    ex = await exited;
  } catch (e) {
    console.log(`     | (no exit) output so far: ${JSON.stringify(out)}`);
    r.kill(9);
    throw e;
  }
  return { ex, out };
}

async function test(name, fn) {
  const t0 = Date.now();
  try {
    await fn();
    console.log(`ok   ${name} (${((Date.now() - t0) / 1000).toFixed(1)} s)`);
  } catch (e) {
    failed++;
    console.log(`FAIL ${name}: ${e.stack || e}`);
  }
}

(async () => {
  const srv = new AgentServer(() => key);
  await srv.listen(Number(port), '0.0.0.0');
  srv.on('reject', (r) => console.log(`reject ${r.address}: ${r.reason}`));
  console.log(`listening on ${port}, waiting for the agent`);
  let agent = await once(srv, 'agent', 30 * 60 * 1000);
  console.log(`agent: ${JSON.stringify(agent.info)}`);

  await test('signed in as root, runs programs as the configured user', async () => {
    assert.strictEqual(agent.info.user, user);
    assert.notStrictEqual(agent.info.uid, '0');
  });

  await test('id, tty and environment of a program', async () => {
    const { ex, out } = await runIt(agent, ['/sbin/sh', '-c',
      '/usr/bin/id; /usr/bin/tty; echo "HOME=$HOME TERM=$TERM DISPLAY=$DISPLAY"; /usr/bin/pwd'],
    { env: { DISPLAY: ':0' } });
    console.log(out.replace(/\r/g, '').trim().split('\n').map((l) => `     | ${l}`).join('\n'));
    assert.strictEqual(ex.code, 0);
    assert.ok(new RegExp(`uid=\\d+\\(${user}\\)`).test(out), out);
    assert.ok(/\/dev\/pts\/?\d+/.test(out), out);
    assert.ok(out.includes('TERM=xterm DISPLAY=:0'), out);
    assert.ok(/vscode-agent\.\d+(\.\d+)?\/run/.test(out), out);
  });

  await test('a non-root fork with a terminal survives (user licensing registered)', async () => {
    const { ex, out } = await runIt(agent, ['/sbin/sh', '-c',
      '/usr/bin/sleep 1 & wait; /usr/bin/echo forked-ok; /usr/bin/ls / > /dev/null && echo exec-ok']);
    assert.strictEqual(ex.code, 0, `${JSON.stringify(ex)} ${out}`);
    assert.ok(out.includes('forked-ok') && out.includes('exec-ok'), out);
  });

  await test('modes of the pty, then one typed line', async () => {
    const { ex, out } = await runIt(agent, ['/sbin/sh', '-c',
      '/usr/bin/stty -a; echo ready; read x; echo "got:$x"'], { timeout: 30000 },
    ['line one\r']);
    console.log(out.replace(/\r/g, '').trim().split('\n').map((l) => `     | ${l}`).join('\n'));
    assert.strictEqual(ex.code, 0);
    assert.ok(out.includes('got:line one'), out);
  });

  await test('a program sent with PUT, arguments, input on the pty', async () => {
    const script = '#!/sbin/sh\necho "args:$1|$2"\necho "type:"\nread x\necho "got:$x"\nexit 5\n';
    await agent.put('t.sh', Buffer.from(script));
    const { ex, out } = await runIt(agent, ['t.sh', 'a b', '100%'], {}, ['hello amix\r']);
    assert.strictEqual(ex.code, 5, out);
    assert.ok(out.includes('args:a b|100%'), out);
    assert.ok(out.includes('got:hello amix'), out);
  });

  await test('window size at start and after a resize', async () => {
    const { out } = await runIt(agent, ['/sbin/sh', '-c',
      '/usr/bin/stty -a | /usr/bin/head -2; read x; /usr/bin/stty -a | /usr/bin/head -2'],
    { cols: 100, rows: 30 }, [(r) => r.resize(132, 50), '\r']);
    console.log(out.replace(/\r/g, '').trim().split('\n').map((l) => `     | ${l}`).join('\n'));
    assert.ok(/rows\s*=\s*30/.test(out) && /columns\s*=\s*100/.test(out), out);
    assert.ok(/rows\s*=\s*50/.test(out) && /columns\s*=\s*132/.test(out), out);
  });

  await test('KILL, and a core file that GET brings back', async () => {
    const k = await runIt(agent, ['/sbin/sh', '-c', '/usr/bin/sleep 60'], {}, [(r) => r.kill(9)]);
    assert.ok(k.ex.signal === 9 || k.ex.code === 137, JSON.stringify(k.ex));
    // AMIX's sh -c 'kill -QUIT $$' exits with 208: the agent sends SIGQUIT itself
    const c = await runIt(agent, ['/usr/bin/sleep', '60'], {}, [(r) => r.kill(3)]);
    console.log(`     | ${JSON.stringify(c.ex)}`);
    assert.strictEqual(c.ex.signal, 3);
    if (c.ex.core) {
      const core = await agent.get('core');
      assert.ok(core && core.length > 1000, 'core fetched');
      console.log(`     | core: ${core.length} bytes, ELF ${core.toString('latin1', 1, 4)}`);
    }
    assert.strictEqual(await agent.get('no-such-file'), null);
  });

  await test('a link the user plants in run/ does not give root files away', async () => {
    await runIt(agent, ['/sbin/sh', '-c', '/usr/bin/ln -s /etc/shadow run-link; /usr/bin/ln -s /etc/shadow core2']);
    assert.strictEqual(await agent.get('core2'), null);
  });

  await test('512 KB to AMIX, checked', async () => {
    const data = crypto.randomBytes(512 * 1024);
    const t0 = Date.now();
    await agent.put('big.bin', data, 0o644);
    const s = (Date.now() - t0) / 1000;
    console.log(`     | ${(512 / s).toFixed(0)} KB/s`);
    const { out } = await runIt(agent, ['/sbin/sh', '-c', '/usr/bin/sum $VSCODE_AGENT_DIR/big.bin']);
    assert.ok(/\d+\s+1024/.test(out), out);
  });

  await test('CLEAN, then disconnect: programs end, session folders go, agent dials again', async () => {
    await agent.clean();
    const r = agent.run(['/sbin/sh', '-c', '/usr/bin/sleep 300']);
    const pid = await r.started;
    const back = once(srv, 'agent', 120000);
    agent.close();
    agent = await back;
    const { out } = await runIt(agent, ['/sbin/sh', '-c',
      `/usr/bin/ls -d /tmp/vscode-agent.*; /usr/bin/ps -p ${pid} > /dev/null && echo still-running || echo gone`]);
    console.log(out.replace(/\r/g, '').trim().split('\n').map((l) => `     | ${l}`).join('\n'));
    assert.ok(out.includes('gone'), out);
    assert.strictEqual(out.match(/vscode-agent\.\d+/g).length, 1, out);
  });

  await test("the agent's log", async () => {
    const { out } = await runIt(agent, ['/usr/bin/cat', '/var/log/vscode-agent.log']);
    console.log(out.replace(/\r/g, '').trim().split('\n').slice(-12).map((l) => `     | ${l}`).join('\n'));
  });

  agent.close();
  srv.close();
  console.log(failed ? `${failed} failed` : 'all passed');
  process.exit(failed ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(1); });
