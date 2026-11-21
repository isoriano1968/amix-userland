'use strict';
// One program on AMIX through the agent, for e2e scripts (an X window to
// screenshot, say): waits for the agent to dial in, sends BINARY, runs it as
// the agent's user and prints its output.
//   node tests/run-on-target.js KEY PORT USER BINARY [--env NAME=VALUE]...
// (USER: the login the agent must run programs as; - for any)
//        [--arg ARG]... [--keep SECONDS] [--cols N --rows N] [--wait MINUTES]
// --keep SECONDS: the program runs that long and is then ended (KILL 15,
// then 9); it may end sooner by itself. Without --keep it runs to its end.
// Prints "started PID", the output, then "exit {...}"; exit status 0 when the
// program started and (without --keep) exited 0, or (with --keep) was still
// running when the time was up.
const fs = require('fs');
const path = require('path');
const { AgentServer } = require('../../vscode/src/agent-link.js');

const argv = process.argv.slice(2);
const [key, port, user, binary] = argv;
if (!binary) {
  console.error('usage: run-on-target.js KEY PORT USER BINARY [--env N=V]... [--arg A]... '
    + '[--keep SECONDS] [--cols N --rows N] [--wait MINUTES]');
  process.exit(2);
}
const env = {};
const args = [];
let keep = 0;
let cols = 80;
let rows = 24;
let waitMin = 30;
for (let i = 4; i < argv.length; i++) {
  const o = argv[i];
  const v = argv[++i];
  if (v === undefined) { console.error(`${o} needs a value`); process.exit(2); }
  if (o === '--env') {
    const eq = v.indexOf('=');
    if (eq < 1) { console.error(`--env ${v}: NAME=VALUE`); process.exit(2); }
    env[v.slice(0, eq)] = v.slice(eq + 1);
  } else if (o === '--arg') args.push(v);
  else if (o === '--keep') keep = Number(v);
  else if (o === '--cols') cols = Number(v);
  else if (o === '--rows') rows = Number(v);
  else if (o === '--wait') waitMin = Number(v);
  else { console.error(`unknown option ${o}`); process.exit(2); }
}

function once(em, ev, ms) {
  return new Promise((res, rej) => {
    const t = setTimeout(() => rej(new Error(`timeout waiting for ${ev}`)), ms);
    em.once(ev, (x) => { clearTimeout(t); res(x); });
  });
}

(async () => {
  const srv = new AgentServer(() => key);
  await srv.listen(Number(port), '0.0.0.0');
  srv.on('reject', (r) => console.log(`reject ${r.address}: ${r.reason}`));
  console.log(`listening on ${port}, waiting for the agent`);
  const agent = await once(srv, 'agent', waitMin * 60 * 1000);
  console.log(`agent: ${JSON.stringify(agent.info)}`);
  if (user !== '-' && agent.info.user !== user) {
    console.log(`FAIL the agent runs programs as ${agent.info.user}, not ${user}`);
    process.exit(1);
  }
  const name = path.basename(binary);
  await agent.put(name, fs.readFileSync(binary), 0o755);
  const r = agent.run([name, ...args], { env, cols, rows });
  r.on('data', (d) => process.stdout.write(d.toString('latin1')));
  let ended = null;
  const exited = new Promise((res) => r.on('exit', (e) => { ended = e; res(e); }));
  const pid = await r.started;
  console.log(`started ${pid}`);
  let ok;
  if (keep > 0) {
    await Promise.race([exited, new Promise((res) => setTimeout(res, keep * 1000))]);
    ok = !ended;                  // still running when the time was up
    if (!ended) {
      r.kill(15);
      await Promise.race([exited, new Promise((res) => setTimeout(res, 5000))]);
      if (!ended) { r.kill(9); await exited; }
    }
  } else {
    await exited;
    ok = ended.code === 0;
  }
  console.log(`\nexit ${JSON.stringify(ended)}${keep > 0 ? (ok ? ' (ran the whole --keep time)' : ' (ended before --keep)') : ''}`);
  agent.close();
  srv.close();
  process.exit(ok ? 0 : 1);
})().catch((e) => { console.error(`FAIL ${e.stack || e}`); process.exit(1); });
