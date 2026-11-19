# vscode-agent

Runs programs from VS Code on AMIX: console programs in a VS Code terminal,
X and CDE programs on the Amiga's own screen. It is the AMIX side of the
**AMIX: Run on Target** command in the [VS Code extension](../vscode).

The agent **dials out** to VS Code. An emulator's NAT, such as FS-UAE's
slirp network, lets nothing dial into AMIX, and a stock AMIX has no ssh. A
real Amiga works the same way. The agent needs nothing but AMIX's libc and
sockets.

## Setting it up

The AMIX Installer sets it up when you pick **VS Code agent** in the
Development step. By hand:

1. In VS Code, run **AMIX: Create Agent Key**. It shows the configuration
   to use, with the key and this computer's addresses.
2. On AMIX, as root, put that text into `/etc/vscode-agent.conf` (the
   example is `/etc/vscode-agent.conf.example`), then make it root's
   alone: `chmod 600 /etc/vscode-agent.conf`. The agent refuses a file
   others can read.
3. Start it with `/etc/init.d/vscode-agent start`. From then on it starts
   at every boot (`/etc/rc2.d/S87vscode-agent`).

Without `/etc/vscode-agent.conf` the rc script does nothing and prints
nothing.

| Key | Meaning |
|---|---|
| `connect HOST:PORT` | where VS Code listens; several lines are tried in turn. FS-UAE's host is `10.0.2.2` |
| `user NAME` | the user the programs run as; never root |
| `secret KEY` | the key from VS Code, 16 characters or more |
| `tmpdir DIR` | where session folders go (`/tmp`) |
| `reserve-mb N` | a file that would leave less free space is refused (100) |
| `retry S` | seconds between dial attempts (10) |
| `log FILE` | a log; the agent keeps it under 64 KB |

## Security

- **Sign-in:** both ends prove that they know the secret
  (HMAC-SHA-256 challenge and response). The secret never crosses the
  wire, and a listener with another key gets nothing.
- **What is not protected:** after the sign-in the connection is neither
  encrypted nor protected against changes. Use it on your own network or
  in an emulator, not across the internet.
- **Who runs the programs:** they run as `user`, never as root, each in
  its own session on a pseudo terminal. AMIX's user licensing is
  registered for them: AMIX would otherwise kill a non-root program with a
  terminal at its first fork.
- **Files:** the programs you send are kept in a root-owned session folder,
  `/tmp/vscode-agent.PID`. The user's working folder `run/` inside it is
  read (GET) and removed only by a child process that has become the
  user, so a link planted there never leads root anywhere.
- **Cleanup:** when VS Code goes away, its programs get SIGHUP, then
  SIGKILL, and the session folder is removed.

## Protocol (version 1)

The protocol uses text lines. `%XX` encodes spaces, control characters,
`%` and bytes from 0x7f in tokens. A frame that carries data gives its
length, and exactly that many bytes follow the line.

```
agent → AMIX-AGENT 1 <version> <nonceA>
ext   → AUTH <nonceB> <hmac(key, "ext <nonceA> <nonceB>")>
agent → OK <hmac(key, "agent <nonceB> <nonceA>")> user=U uid=N free=KB dir=D uname=…
        (or ERR AUTH …, and the connection closes)

ext   → PUT <name> <mode, octal> <size> <sha256>   + size bytes
agent → OK PUT <name> | ERR PUT <name> <why>
ext   → RUN <id> <cols> <rows> [NAME=VALUE …] -- <argv…>
agent → STARTED <id> <pid> | ERR RUN <id> <why>
ext   → IN <id> <n>  + n bytes            (keys to the program)
agent → OUT <id> <n> + n bytes            (the terminal's output)
ext   → WINCH <id> <cols> <rows>
ext   → KILL <id> <signal>
agent → EXIT <id> <code> | SIGNAL <id> <signal> <core 0|1>
ext   → GET <name>                        (from run/, e.g. core)
agent → FILE <name> <size> <sha256> + size bytes | ERR GET <name> <why>
ext   → CLEAN                             (ends programs, new empty session)
agent → OK CLEAN <dir>
ext   → PING <token>    agent → PONG <token>
ext   → BYE
```

How a program starts:
- **Name:** a program name without `/` that came with PUT runs from the
  session folder. Any other name is looked up in `PATH`.
- **Working folder:** `run/`.
- **Environment:** `HOME`, `USER`, `LOGNAME`, `SHELL`, `PATH`,
  `TERM=xterm` and `VSCODE_AGENT_DIR`, plus what RUN gives.
- **X programs:** with `DISPLAY` set, `XAUTHORITY` defaults to the user's
  `~/.Xauthority`. A window can only open on the Amiga screen while that
  user is logged in to the desktop.

## Building and testing

`make build` makes the AMIX binary. It is built with gcc 14 for
`-m68020-60`, so one binary runs on 68020 to 68060. libamixc99 (only
`snprintf` and `vsnprintf`) is linked in from its archive, so the agent
needs nothing beyond a stock AMIX 2.1: for a quick test, the binary, the
rc script and a configuration are all it takes. The archive comes from the
sysroot (`usr/local/lib/libamixc99.a`, the OpenIndiana package
`developer/amix/amixc99`) or from `library/amixc99`'s stage.

`make test` builds the agent for the host and drives it with the
extension's own protocol code (`../vscode/src/agent-link.js`). The test
covers:
- sign-in, and wrong keys at both ends;
- PUT, RUN with arguments and environment;
- the pty: input, window size and resize;
- KILL, a missing program, GET;
- refused names and checksums, a 3 MB file;
- CLEAN, and the cleanup and new dial after a disconnect.

`make stage` fills `build/stage` for the package.
