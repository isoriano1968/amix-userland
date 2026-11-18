'use strict';

// The "amix" launch type: VS Code's Run and Debug view, launch.json, F5 and
// Ctrl+F5 run the program on the AMIX target through Run on Target.  The
// adapter is inline (no separate process).  Breakpoints and stepping need
// gdb on AMIX, which is not there yet, so F5 runs without debugging and
// says so.

const vscode = require('vscode');

const TYPE = 'amix';

function defaultConfig() {
  return {
    type: TYPE,
    request: 'launch',
    name: 'Run on AMIX target',
    program: '',
    args: [],
    buildFirst: true
  };
}

class Adapter {
  constructor(run) {
    this.run = run;               // (config) => Promise<{done, kill}|undefined>
    this.seq = 1;
    this.handle = null;
    this.ended = false;
    this.emitter = new vscode.EventEmitter();
    this.onDidSendMessage = this.emitter.event;
  }

  send(msg) {
    this.emitter.fire({ seq: this.seq++, ...msg });
  }

  respond(req, body, error) {
    const r = { type: 'response', request_seq: req.seq, command: req.command, success: !error };
    if (body) r.body = body;
    if (error) r.message = error;
    this.send(r);
  }

  event(name, body) {
    this.send({ type: 'event', event: name, body });
  }

  output(text, category = 'console') {
    this.event('output', { category, output: text });
  }

  end(exitCode) {
    if (this.ended) return;
    this.ended = true;
    if (exitCode !== undefined) this.event('exited', { exitCode });
    this.event('terminated', {});
  }

  async launch(req) {
    const a = req.arguments || {};
    if (!a.noDebug) {
      this.output('AMIX: breakpoints and stepping need gdb on AMIX, which is not there yet; '
        + 'the program runs without debugging.\n', 'important');
    }
    let h;
    try {
      h = await this.run(a);
    } catch (e) {
      this.respond(req, null, e.message);
      this.end();
      return;
    }
    if (!h) {
      this.respond(req, null, 'nothing was started on the AMIX target (see the message)');
      this.end();
      return;
    }
    this.handle = h;
    this.respond(req);
    this.output(`AMIX: ${h.name} runs on ${h.agent}; its terminal is "AMIX: ${h.name} @ ${h.agent}".\n`);
    h.done.then((e) => {
      if (e.lost) this.output('AMIX: the connection to the target was lost.\n', 'stderr');
      else if (e.error) this.output(`AMIX: ${e.error}\n`, 'stderr');
      else if (e.signal) this.output(`AMIX: ${h.name} ended by signal ${e.signal}${e.core ? ', core dumped' : ''}.\n`);
      else this.output(`AMIX: ${h.name} exited with ${e.code}.\n`);
      this.end(e.code !== undefined ? e.code : 128 + (e.signal || 0));
    });
  }

  handleMessage(m) {
    if (m.type !== 'request') return;
    switch (m.command) {
      case 'initialize':
        this.respond(m, { supportsConfigurationDoneRequest: true, supportsTerminateRequest: true });
        this.event('initialized', {});
        break;
      case 'launch':
        this.launch(m);
        break;
      case 'threads':
        this.respond(m, { threads: [{ id: 1, name: 'program on AMIX' }] });
        break;
      case 'terminate':
        if (this.handle) this.handle.kill(15);
        this.respond(m);
        break;
      case 'disconnect':
        if (this.handle && !this.ended) this.handle.kill(1);
        this.respond(m);
        this.end();
        break;
      default:
        this.respond(m);
    }
  }

  dispose() {
    this.emitter.dispose();
  }
}

function activate(context, run) {
  context.subscriptions.push(
    vscode.debug.registerDebugConfigurationProvider(TYPE, {
      provideDebugConfigurations: () => [defaultConfig()],
      // F5 without a launch.json: run what Run on Target would run
      resolveDebugConfiguration: (folder, config) => {
        if (!config.type && !config.request && !config.name) return defaultConfig();
        return config;
      }
    }),
    vscode.debug.registerDebugAdapterDescriptorFactory(TYPE, {
      createDebugAdapterDescriptor: () => new vscode.DebugAdapterInlineImplementation(new Adapter(run))
    })
  );
}

module.exports = { activate, Adapter, defaultConfig, TYPE };
