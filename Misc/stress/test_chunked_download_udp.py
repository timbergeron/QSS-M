"""Run a production Linux client against a local NQ UDP peer.

Requires an SDL2/OpenGL build, software GL, and a Quake pak0.pak containing
maps/e1m1.bsp. No stress hooks, registered assets, or external server needed.
Checks exact file contents and cleanup under response/request loss, reordering,
duplication, cancel/file changes and reconnects. The peer emulates the wire;
server pacing/fairness are covered by test_chunked_download.py.
"""
from pathlib import Path
import argparse, heapq, os, shutil, socket, struct, subprocess, tempfile, time

ROOT = Path(__file__).resolve().parents[2]
CTL, DATA, ACK, EOM, UNREL = 0x80000000, 0x10000, 0x20000, 0x80000, 0x100000

def string(s): return s.encode() + b'\0'
def stuff(s): return bytes([9]) + string(s)
def info(names):
    return bytes([11]) + struct.pack('<III', 0x58455446, 0x20000000, 666) + bytes([1, 0]) + string('Download UDP test') + string('maps/e1m1.bsp') + b'\0' + b''.join(string(n[6:]) for n in names) + b'\0' + bytes([5, 1, 0, 25, 1])
def wav(seed):
    data = bytes((i * seed + i // 97) % 256 for i in range(128 * 1024))
    return b'RIFF' + struct.pack('<I', 36 + len(data)) + b'WAVEfmt ' + struct.pack('<IHHIIHH', 16, 1, 1, 11025, 22050, 2, 16) + b'data' + struct.pack('<I', len(data)) + data

class Peer:
    def __init__(self, mode):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(('127.0.0.1', 0)); self.sock.setblocking(False)
        self.address = None; self.mode = mode
        self.files = {'sound/dl_probe_a.wav': wav(13), 'sound/dl_probe_b.wav': wav(29)}
        self.reliable = []; self.waiting = None; self.sendseq = 0; self.recvseq = 0; self.unrelseq = 0
        self.current = None; self.due = []; self.serial = 0; self.requests = 0; self.lost = 0
        self.request_packets = 0; self.lost_requests = 0
        self.unreliable_by_file = {}; self.reliable_requests = 0
        self.reliable_batch_sizes = []; self.outage_until = None
        self.saved = None; self.injected = 0; self.transitioned = False; self.connections = 0; self.done = False
        self.started = []; self.commands = []; self.last_unrel = -1
    def send(self, flags, seq, payload=b''):
        self.sock.sendto(struct.pack('!II', flags | (8 + len(payload)), seq) + payload, self.address)
    def reliable_send(self, payload): self.reliable.append(payload)
    def packet(self, payload):
        result = struct.pack('!II', UNREL | (8 + len(payload)), self.unrelseq) + payload
        self.unrelseq += 1
        return result
    def schedule(self, packet, delay):
        self.serial += 1; heapq.heappush(self.due, (time.monotonic() + delay, self.serial, packet, self.address))
    def command(self, command, reliable):
        self.commands.append(command)
        words = command.strip().split()
        if not words: return
        if words[0] == 'download':
            name = words[1].strip('"')
            if name not in self.files:
                self.reliable_send(bytes([41]) + struct.pack('<ii', -1, -1) + string(name)); return
            self.current = name; self.started.append(name)
            start = bytes([41]) + struct.pack('<ii', -1, len(self.files[name])) + string(name)
            if self.mode != 'legacy': start = stuff(f'//cl_downloadsequence {self.unrelseq} "{name}"\n') + start
            self.reliable_send(start)
        elif words[0] == 'nextdl':
            chunk = int(words[1])
            if chunk < 0:
                if self.current and not self.saved:
                    # Reserve a NEW sequence for an old-file duplicate. Deliver it only
                    # after the next reliable start: NQ sequence rejection alone cannot help.
                    payload = bytes([41]) + struct.pack('<I', 0) + self.files[self.current][:1024]
                    self.saved = self.packet(payload)
                self.current = None; return
            if not self.current: return
            if reliable: self.reliable_requests += 1
            if self.saved and self.current.endswith('_b.wav') and self.mode in ('normal', 'cancel', 'fallback', 'fallbackcancel'):
                self.schedule(self.saved, 0); self.saved = None; self.injected += 1
            self.requests += 1
            if self.mode in ('cancel', 'reconnect', 'fallbackcancel', 'fallbackreconnect') and not self.transitioned and self.requests >= 8:
                payload = bytes([41]) + struct.pack('<I', 0) + self.files[self.current][:1024]
                self.saved = self.packet(payload); self.transitioned = True; self.current = None
                if self.mode.endswith('cancel'):
                    self.reliable_send(stuff('stopdownload\n') + info(['sound/dl_probe_b.wav']))
                else:
                    port = self.sock.getsockname()[1]
                    self.reliable_send(stuff(f'connect 127.0.0.1:{port}\n'))
                return
            data = self.files[self.current]
            if chunk * 1024 >= len(data): return
            payload = bytes([41]) + struct.pack('<I', chunk) + data[chunk*1024:(chunk+1)*1024].ljust(1024, b'\0')
            packet = self.packet(payload)
            if self.requests % 19 == 0: self.lost += 1; return
            # Reordering deliberately crosses NQ unreliable sequence numbers.
            delay = 0.07 if self.requests % 13 == 0 else 0.025
            if self.mode == 'fallbackrtt': delay += 0.075
            self.schedule(packet, delay)
            if self.requests % 23 == 0: self.schedule(packet, delay + 0.04)
        elif words[0] == 'prespawn': self.done = True
    def parse_commands(self, payload, reliable):
        pos = 0
        before = self.reliable_requests
        while pos < len(payload):
            op = payload[pos]; pos += 1
            if op == 4:
                end = payload.index(0, pos); self.command(payload[pos:end].decode(), reliable); pos = end + 1
            elif op == 1: pass
            elif op == 5: pos += 4
            elif op == 2: return
            else: return
        if reliable and self.reliable_requests > before:
            self.reliable_batch_sizes.append(self.reliable_requests - before)
    def poll(self):
        while True:
            try: packet, address = self.sock.recvfrom(65535)
            except BlockingIOError: break
            if packet[:4] == b'\xff'*4: continue
            if len(packet) < 5: continue
            flags = struct.unpack_from('!I', packet)[0]
            if flags & CTL:
                if packet[4] == 1 and packet[5:11] == b'QUAKE\0':
                    if address != self.address:
                        self.connections += 1; self.address = address
                        self.sendseq = self.recvseq = self.unrelseq = 0; self.last_unrel = -1
                        self.reliable.clear(); self.waiting = None; self.due.clear()
                        if self.mode.endswith('reconnect') and self.transitioned:
                            self.saved = None
                            names = ['sound/dl_probe_b.wav']
                        else: names = list(self.files)
                        self.reliable_send(info(names))
                    reply = bytes([0x81]) + struct.pack('<I', self.sock.getsockname()[1])
                    self.sock.sendto(struct.pack('!I', CTL | (4 + len(reply))) + reply, address)
                continue
            if address != self.address or len(packet) < 8: continue
            seq = struct.unpack_from('!I', packet, 4)[0]
            if flags & ACK:
                if self.waiting and self.waiting[0] == seq: self.waiting = None
            elif flags & DATA:
                if self.mode == 'fallbackrtt':
                    # The NQ reliable stream cannot send another message until
                    # this ACK returns. Expose the cost of single-request batches.
                    self.schedule(struct.pack('!II', ACK | 8, seq), 0.1)
                else: self.send(ACK, seq)
                if seq != self.recvseq: continue
                self.recvseq += 1
                assert flags & EOM, 'fixture expects short client reliable messages'
                self.parse_commands(packet[8:], True)
            elif flags & UNREL:
                if seq <= self.last_unrel: continue
                self.last_unrel = seq
                if packet[8:].startswith(b'\x04nextdl '):
                    self.request_packets += 1
                    self.unreliable_by_file[self.current] = self.unreliable_by_file.get(self.current, 0) + 1
                    if self.mode == 'outage' and self.current == 'sound/dl_probe_b.wav':
                        # The first file proved unreliable requests work. Blackhole
                        # the second file for longer than the fallback timeout.
                        if self.outage_until is None: self.outage_until = time.monotonic() + 6
                        if time.monotonic() < self.outage_until:
                            self.lost_requests += 1; continue
                    if self.mode.startswith('fallback') and not (self.mode == 'fallbackreconnect' and self.transitioned):
                        self.lost_requests += 1; continue
                    if self.request_packets % 13 == 0:
                        self.lost_requests += 1; continue
                self.parse_commands(packet[8:], False)
        now = time.monotonic()
        if not self.waiting and self.reliable:
            payload = self.reliable.pop(0); self.waiting = (self.sendseq, payload, now)
            self.send(DATA | EOM, self.sendseq, payload); self.sendseq += 1
        elif self.waiting and now - self.waiting[2] >= 0.5:
            seq, payload, _ = self.waiting; self.send(DATA | EOM, seq, payload)
            self.waiting = (seq, payload, now)
        while self.due and self.due[0][0] <= now:
            _, _, packet, address = heapq.heappop(self.due); self.sock.sendto(packet, address)

def run(binary, pak, root, mode):
    peer = Peer(mode); game = root / mode / 'id1'; game.mkdir(parents=True)
    (game / 'pak0.pak').symlink_to(pak)
    config = 'cl_web_download_url ""\ncl_web_download_url2 ""\ncl_demoreel 0\nhost_maxfps 144\nvid_vsync 0\ndeveloper 1\n'
    (game / 'config.cfg').write_text(config)
    (game / 'autoexec.cfg').write_text(config + f'connect 127.0.0.1:{peer.sock.getsockname()[1]}\n')
    logfile = root / mode / 'client.log'
    env = {**os.environ, 'SDL_VIDEODRIVER': 'offscreen', 'LIBGL_ALWAYS_SOFTWARE': '1'}
    started = time.monotonic()
    with logfile.open('w') as log:
        proc = subprocess.Popen([str(binary), '-basedir', str(game.parent), '-nohome', '-window', '-width', '320', '-height', '240', '-nosound', '-nojoy', '-nomouse', '-noipx'], cwd=game.parent, env=env, stdout=log, stderr=subprocess.STDOUT)
        try:
            while time.monotonic() - started < 40:
                peer.poll()
                if peer.done: break
                if proc.poll() is not None: raise RuntimeError('engine exited')
                time.sleep(0.002)
            else: raise RuntimeError('timeout; commands: ' + repr(peer.commands[-10:]))
            names = list(peer.files) if mode in ('normal', 'legacy', 'oldclient', 'fallback', 'fallbackrtt', 'outage') else ['sound/dl_probe_b.wav']
            for name in names:
                assert (game / name).read_bytes() == peer.files[name], 'wrong file contents: ' + name
            assert not list(game.rglob('*.tmp')), 'leftover temporary download'
            if mode in ('normal', 'cancel', 'fallback', 'fallbackcancel'): assert peer.injected, 'late old-file packet was not injected'
            if mode.endswith('reconnect'): assert peer.connections == 2
            if mode.endswith(('cancel', 'reconnect')): assert not (game / 'sound/dl_probe_a.wav').exists()
            if mode.startswith('fallback'):
                assert peer.reliable_requests > 0 and peer.lost_requests > 0
                assert logfile.read_text().count('switching to reliable chunk requests') == 1
                second_file_unreliable = peer.unreliable_by_file.get('sound/dl_probe_b.wav', 0)
                if mode == 'fallbackreconnect': assert second_file_unreliable > 0, 'fallback survived reconnect'
                else: assert second_file_unreliable == 0, 'fallback was forgotten between files'
                assert max(peer.reliable_batch_sizes) == 8, 'reliable stream never filled its window in a batch'
            if mode == 'fallbackrtt':
                assert peer.reliable_requests / len(peer.reliable_batch_sizes) > 2, 'stop-and-wait batches too small'
            if mode == 'outage':
                assert peer.outage_until and time.monotonic() >= peer.outage_until
                assert not peer.reliable_requests, 'outage switched a working request path to reliable'
                assert 'switching to reliable chunk requests' not in logfile.read_text()
            assert peer.lost > 0
            print(f'PASS: production client UDP {mode}: {len(names)} files verified, {peer.requests} requests, {peer.lost} dropped replies, {peer.lost_requests} dropped request batches, {peer.injected} stale file packets, {time.monotonic()-started:.2f} s', flush=True)
        except Exception:
            print(logfile.read_text()[-4500:]); raise
        finally:
            peer.sock.close(); proc.terminate()
            try: proc.wait(timeout=5)
            except subprocess.TimeoutExpired: proc.kill(); proc.wait()

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bin', type=Path, default=ROOT/'Quake/quakespasm')
    parser.add_argument('--pak', type=Path, required=True)
    parser.add_argument('--mode', choices=['normal','legacy','cancel','reconnect','fallback','fallbackcancel','fallbackreconnect','fallbackrtt','outage','oldclient'])
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='qssm-download-udp-') as tmp:
        # History uses the executable's directory even with -basedir. Keep the
        # production binary's directory and the user's home out of test writes.
        binary = Path(tmp) / 'quakespasm'
        shutil.copy2(args.bin.resolve(), binary)
        for mode in ([args.mode] if args.mode else ['normal','legacy','cancel','reconnect','fallback','fallbackcancel','fallbackreconnect','fallbackrtt','outage']):
            run(binary, args.pak.resolve(), Path(tmp), mode)
