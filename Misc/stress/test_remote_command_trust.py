#!/usr/bin/env python3
"""Run the production client against a hostile local NQ server.

The peer plays a malicious server: it stuffs commands that try to send, read,
replace or persist the user's rcon credentials. A user-written autoexec loop
provides trusted actions after connecting, so each run also checks that the
user's own rcon, binds and aliases still work.

Requires an SDL3/OpenGL build, software GL and a Quake pak0.pak.
Modes: bound (rcon_address matches the server), mismatch (it doesn't), legacy
(rcon_address empty, so rcon targets the current connection).
"""
from pathlib import Path
import argparse, os, shutil, socket, struct, subprocess, tempfile, time

ROOT = Path(__file__).resolve().parents[2]
CTL, DATA, ACK, EOM, UNREL = 0x80000000, 0x10000, 0x20000, 0x80000, 0x100000
CCREQ_RCON = 0x05
PASSWORD = '1234'  # numeric, so the `if` comparison oracle would apply


def string(s): return s.encode() + b'\0'
def stuff(s): return bytes([9]) + string(s)
def info():  # svc_serverinfo (FTE-RMQ, 1 client, deathmatch), then svc_signonnum 1
    return (bytes([11]) + struct.pack('<III', 0x58455446, 0x20000000, 666) + bytes([1, 0]) +
            string('Remote trust test') + string('maps/e1m1.bsp') + b'\0' + b'\0' + bytes([25, 1]))


class Peer:
    def __init__(self):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(('127.0.0.1', 0)); self.sock.setblocking(False)
        self.port = self.sock.getsockname()[1]
        self.address = None; self.connected = False
        self.reliable = []; self.waiting = None; self.sendseq = 0; self.recvseq = 0
        self.commands = []; self.rcon = []

    def send(self, flags, seq, payload=b''):
        self.sock.sendto(struct.pack('!II', flags | (8 + len(payload)), seq) + payload, self.address)

    def stuff(self, text): self.reliable.append(stuff(text))

    def parse_commands(self, payload):
        pos = 0
        while pos < len(payload):
            op = payload[pos]; pos += 1
            if op == 4:
                end = payload.index(0, pos); self.commands.append(payload[pos:end].decode(errors='replace')); pos = end + 1
            elif op == 1: pass
            elif op == 5: pos += 4
            else: return

    def poll(self):
        while True:
            try: packet, address = self.sock.recvfrom(65535)
            except BlockingIOError: break
            if packet[:4] == b'\xff' * 4 or len(packet) < 5: continue
            flags = struct.unpack_from('!I', packet)[0]
            if flags & CTL:
                body = packet[4:]
                if body[0] == 1 and body[1:7] == b'QUAKE\0':
                    if address != self.address:
                        self.address = address; self.connected = True
                        self.reliable.append(info())
                    reply = bytes([0x81]) + struct.pack('<I', self.port)
                    self.sock.sendto(struct.pack('!I', CTL | (4 + len(reply))) + reply, address)
                elif body[0] == CCREQ_RCON:
                    parts = body[1:].split(b'\0')
                    self.rcon.append((parts[0].decode(errors='replace'), parts[1].decode(errors='replace')))
                continue
            if address != self.address or len(packet) < 8: continue
            seq = struct.unpack_from('!I', packet, 4)[0]
            if flags & ACK:
                if self.waiting and self.waiting[0] == seq: self.waiting = None
            elif flags & DATA:
                self.send(ACK, seq)
                if seq != self.recvseq: continue
                self.recvseq += 1
                self.parse_commands(packet[8:])
            elif flags & UNREL:
                self.parse_commands(packet[8:])
        now = time.monotonic()
        if self.address and not self.waiting and self.reliable:
            payload = self.reliable.pop(0); self.waiting = (self.sendseq, payload, now)
            self.send(DATA | EOM, self.sendseq, payload); self.sendseq += 1
        elif self.waiting and now - self.waiting[2] >= 0.5:
            seq, payload, _ = self.waiting; self.send(DATA | EOM, seq, payload)
            self.waiting = (seq, payload, now)


def run(binary, pak, root, mode):
    peer = Peer()
    game = root / mode / 'id1'; game.mkdir(parents=True)
    (game / 'pak0.pak').symlink_to(pak)
    rcon_address = {'bound': f'127.0.0.1:{peer.port}', 'mismatch': f'127.0.0.2:{peer.port}', 'legacy': ''}[mode]
    base = 'cl_web_download_url ""\ncl_web_download_url2 ""\ncl_demoreel 0\nhost_maxfps 144\nvid_vsync 0\ncfg_save_aliases 1\ndeveloper 1\n'
    (game / 'config.cfg').write_text(base)
    (game / 'user_rcon.cfg').write_text('rcon via_exec\n')
    (game / 'autoexec.cfg').write_text(base + f'''
seta rcon_password {PASSWORD}
rcon_address "{rcon_address}"
bind k "echo userbind"
alias useralias "rcon via_user_alias"
set trust_go 0
set trust_save 0
set trust_evil 0
alias do_go "rcon trusted_ok; set trust_go 0"
alias do_save "writeconfig trustcheck; set trust_save 0"
alias do_evil "evil; set trust_evil 0"
set trust_loop 1
alias tick "wait; if trust_go do_go; if trust_save do_save; if trust_evil do_evil; if trust_loop tick"
connect 127.0.0.1:{peer.port}
tick
''')
    logfile = root / mode / 'client.log'
    env = {**os.environ, 'SDL_VIDEO_DRIVER': 'offscreen', 'LIBGL_ALWAYS_SOFTWARE': '1'}

    def pump(cond, what, timeout=20):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            peer.poll()
            if cond(): return
            if proc.poll() is not None: raise RuntimeError('engine exited during: ' + what)
            time.sleep(0.002)
        raise RuntimeError(f'timeout: {what}; commands={peer.commands[-8:]} rcon={peer.rcon} '
                           f'sendseq={peer.sendseq} waiting={peer.waiting and peer.waiting[0]} queued={len(peer.reliable)}')

    probe = [0]
    def step(text):
        probe[0] += 1; name = f'probe{probe[0]}'
        peer.stuff(text + f'\ncmd {name}\n')
        pump(lambda: name in peer.commands, name)

    with logfile.open('w') as log:
        proc = subprocess.Popen([str(binary), '-basedir', str(game.parent), '-nohome', '-window', '-width', '320', '-height', '240',
                                 '-nosound', '-nojoy', '-nomouse', '-noipx'], cwd=game.parent, env=env, stdout=log, stderr=subprocess.STDOUT)
        try:
            pump(lambda: peer.connected, 'connect', 40)

            # Direct, chained, delayed and split attempts to trigger rcon.
            step('rcon direct')
            step('echo x; rcon chained')
            step('wait; rcon after_wait')
            peer.stuff('rcon spl'); step('it')
            step('useralias')                       # user's alias, run by the server
            step('alias evil "rcon via_server_alias"; evil')
            step('exec user_rcon.cfg')
            step('cmdcycle "rcon via_cycle" "echo b"')
            step('bind k "rcon via_bind"; bind j "rcon via_bind_j"')

            # Credential and binding tampering, including flag changes.
            step(f'rcon_password stolen; set rcon_password stolen; seta rcon_password stolen; '
                 f'setfl rcon_password stolen u; setfl password 99 u; toggle rcon_password; reset rcon_password; '
                 f'rcon_address 10.9.9.9; set rcon_address 10.9.9.9; password 77')
            # Exposure: query the secret, then export the console and config to
            # data/, which the server's CSQC can read back with fopen.
            step('rcon_password; cvarlist rcon_; condump data/secret_probe; writeconfig data/secret_cfg; '
                 'condump secret_plain; writeconfig configs/secret_cfg2')
            # Oracle: only a match would forward "leak".
            step(f'if rcon_password == {PASSWORD} "cmd leak"; if rcon_password "cmd leak2"')

            assert not peer.rcon, f'server triggered rcon: {peer.rcon}'
            for leak in ('leak', 'leak2'):
                assert leak not in peer.commands, 'if oracle answered: ' + leak
            assert not [c for c in peer.commands if 'rcon_password' in c or c.startswith('setinfo password')], peer.commands

            # The user's own scripts still work, with the original credentials.
            step("set trust_evil 1")                   # trusted loop runs the server's alias: still untrusted
            time.sleep(0.5); peer.poll()
            step("set trust_go 1")
            if mode == 'mismatch':
                time.sleep(1.0); peer.poll()
                assert not peer.rcon, f'rcon ignored rcon_address: {peer.rcon}'
                assert 'not connected to rcon_address' in logfile.read_text(errors='replace')
            else:
                pump(lambda: peer.rcon, 'trusted rcon')
                assert peer.rcon == [(PASSWORD, 'trusted_ok')], peer.rcon

            # Persistence: the server's bind and alias never reach the config.
            step("set trust_save 1")
            pump(lambda: list(root.joinpath(mode).rglob('trustcheck.cfg')), 'writeconfig')
            time.sleep(0.2)
            cfg = next(root.joinpath(mode).rglob('trustcheck.cfg')).read_text(errors='replace')
            assert 'bind "k" "echo userbind"' in cfg, 'user bind not preserved'
            assert f'seta rcon_password "{PASSWORD}"' in cfg, 'user writeconfig lost the archived password'
            assert 'via_bind' not in cfg and 'via_server_alias' not in cfg, 'server bind/alias persisted'
            assert 'useralias' in cfg and 'tick' in cfg, 'user aliases missing'

            text = logfile.read_text(errors='replace')
            exported = [f for f in root.joinpath(mode).rglob('*') if f.is_file() and 'secret_' in f.name]
            assert not exported, f'server exported files: {exported}'
            assert f'is "{PASSWORD}"' not in text and f'rcon_password "{PASSWORD}"' not in text, 'secret printed for server'
            assert '"rcon_password" is "<hidden>"' in text
            assert 'condump: blocked a command from the server' in text
            assert 'writeconfig: blocked a command from the server' in text
            assert text.count('rcon: blocked a command from the server') >= 7, 'expected blocked rcon attempts'
            assert 'Blocked server change to protected cvar rcon_password' in text
            assert 'Blocked server change to protected cvar rcon_address' in text
            assert 'if: rcon_password is not available to server commands' in text
            print(f'PASS: hostile server ({mode}): rcon, credential, flag, oracle, export, alias, bind, exec and persistence '
                  f'attacks blocked; user rcon {"refused off-target" if mode == "mismatch" else "sent"}', flush=True)
        except Exception:
            lines = [l for l in logfile.read_text(errors='replace').splitlines() if l.strip() and 'RelativeMouseMode' not in l]
            print('\n'.join(lines[-80:])); raise
        finally:
            peer.sock.close(); proc.terminate()
            try: proc.wait(timeout=5)
            except subprocess.TimeoutExpired: proc.kill(); proc.wait()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bin', type=Path, default=ROOT / 'Quake/quakespasm')
    parser.add_argument('--pak', type=Path, required=True)
    parser.add_argument('--mode', choices=['bound', 'mismatch', 'legacy'])
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='qssm-remote-trust-') as tmp:
        # History uses the executable's directory even with -basedir.
        binary = Path(tmp) / 'quakespasm'
        shutil.copy2(args.bin.resolve(), binary)
        for mode in ([args.mode] if args.mode else ['bound', 'mismatch', 'legacy']):
            run(binary, args.pak.resolve(), Path(tmp), mode)
