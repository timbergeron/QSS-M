"""Exercise production browser RTT sampling against controlled UDP servers.

Run: python Misc/stress/test_browser_ping.py
Requires cc + SDL3 on Unix, or Visual Studio + bundled SDL3 on Windows.
All network traffic is loopback; no game assets or internet access are needed.
"""

from contextlib import contextmanager
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import socket
import socketserver
import struct
import subprocess
import tempfile
import threading
import time
import unittest

from test_quit_udp import ROOT, QUERY_SOURCE, compile_query_test


SOURCE = QUERY_SOURCE.replace("static SDL_AtomicInt server_queries_abort;", r'''
/* Inject the otherwise rare socket-allocation failure; all other I/O is real. */
static int fail_third_socket, socket_attempts;
static sys_socket_t test_socket(int family, int type, int protocol) {
    if (++socket_attempts == 3 && fail_third_socket) {
#ifdef _WIN32
        WSASetLastError(WSAENOBUFS);
#else
        errno = ENOBUFS;
#endif
        return INVALID_SOCKET;
    }
    return socket(family, type, protocol);
}
#define socket test_socket
static SDL_AtomicInt server_queries_abort;
''') + r'''
int main(int argc, char **argv) {
    assert(argc == 2 || argc == 3);
    fail_third_socket = argc == 3;
#ifdef _WIN32
    WSADATA winsock;
    assert(WSAStartup(MAKEWORD(2, 2), &winsock) == 0);
#endif
    assert(SDL_Init(0));
    char resolved[256];
    double start = Sys_DoubleTime();
    int ping = UDP_Ping_HostResolved(argv[1], resolved, sizeof(resolved));
    printf("RESULT %d %.6f %s\n", ping, Sys_DoubleTime() - start, resolved);
    SDL_Quit();
    return 0;
}
'''


def nq_reply():
    body = b"\x83fixture:26000\0fixture\0start\0" + bytes([0, 16, 3])
    return struct.pack("!I", 0x80000000 | (len(body) + 4)) + body


REPLIES = {
    "netquake": nq_reply(),
    "getinfo": b"\xff\xff\xff\xffinfoResponse\n\\hostname\\fixture",
    "status": b"\xff\xff\xff\xffn\\hostname\\fixture\n",
    "dp_challenge": b"\xff\xff\xff\xffchallenge 123QSS\0",
    "qw_challenge": b"\xff\xff\xff\xffc123\0",
}


class Server(socketserver.ThreadingUDPServer):
    daemon_threads = False

    def __init__(self, *, protocol="netquake", delays=(0.04, 0.04, 0.04),
                 drop=0, invalid=False, wrong_source=False, duplicate=False, ipv6=False,
                 oversized=False):
        self.address_family = socket.AF_INET6 if ipv6 else socket.AF_INET
        super().__init__(("::1" if ipv6 else "127.0.0.1", 0), Handler)
        self.protocol, self.delays, self.drop = protocol, delays, drop
        self.invalid, self.wrong_source, self.duplicate = invalid, wrong_source, duplicate
        self.oversized = oversized
        self.clients = {}
        self.handled = set()
        self.queries = []
        self.first_query = None
        self.lock = threading.Lock()

    def server_close(self):
        # Let delayed fixture replies finish before closing their sending socket.
        self._threads.join()
        super().server_close()


class Handler(socketserver.BaseRequestHandler):
    def handle(self):
        packet, sock = self.request
        server = self.server
        kind = ("netquake" if len(packet) >= 5 and packet[4] == 2 else
                packet[4:].strip(b"\n\0").decode("ascii", errors="replace"))
        wanted = "getchallenge" if server.protocol.endswith("challenge") else server.protocol
        with server.lock:
            now = time.perf_counter()
            if server.first_query is None:
                server.first_query = now
            index = server.clients.setdefault(self.client_address, len(server.clients))
            server.queries.append((index, kind))
            if (kind != wanted or self.client_address in server.handled):
                return
            if now - server.first_query < server.drop * 0.5 - 0.05:
                return
            server.handled.add(self.client_address)
            delay = server.delays[min(index, len(server.delays) - 1)]
            if delay is None:
                return
        if server.invalid:
            sock.sendto(b"unexpected reply", self.client_address)
            sock.sendto(b"\x80\0\0\x07\x83", self.client_address)  # wrong length
            sock.sendto(b"\xff\xff\xff\xffinfo", self.client_address)
        if server.oversized:
            sock.sendto(b"x" * 9000, self.client_address)
        if server.wrong_source:
            with socket.socket(server.address_family, socket.SOCK_DGRAM) as other:
                other.sendto(REPLIES[server.protocol], self.client_address)
        time.sleep(delay)
        sock.sendto(REPLIES[server.protocol], self.client_address)
        if server.duplicate:
            time.sleep(0.005)
            sock.sendto(REPLIES[server.protocol], self.client_address)


@contextmanager
def fixture(**kwargs):
    with Server(**kwargs) as server:
        thread = threading.Thread(target=lambda: server.serve_forever(poll_interval=0.01), daemon=True)
        thread.start()
        try:
            yield server
        finally:
            server.shutdown()
            thread.join()


class BrowserPingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        build_root = ROOT / ".codex-build"
        build_root.mkdir(exist_ok=True)
        cls.temp = tempfile.TemporaryDirectory(prefix="browser-ping-", dir=build_root)
        path = Path(cls.temp.name).resolve()
        assert path.is_relative_to(build_root.resolve())
        cls.addClassCleanup(cls.temp.cleanup)
        cls.binary = compile_query_test(SOURCE, path)

    def probe(self, server, *, fail_third=False):
        address, port = server.server_address[:2]
        endpoint = f"[{address}]:{port}" if ":" in address else f"{address}:{port}"
        command = [str(self.binary), endpoint]
        if fail_third:
            command.append("fail-third-socket")
        output = subprocess.check_output(command, text=True, timeout=4)
        _, ping, wall, resolved = output.strip().split()
        self.assertEqual(resolved, endpoint)
        return int(ping), float(wall)

    def test_retry_wait_is_excluded_from_rtt(self):
        with fixture(drop=1) as server:
            ping, wall = self.probe(server)
            self.assertGreater(wall, 0.45)
            self.assertTrue(35 <= ping < 150, f"retry wait must not be counted as RTT: {ping}")
            self.assertGreaterEqual(len(server.clients), 3)

    def test_second_retry_wait_is_excluded_from_rtt(self):
        with fixture(drop=2) as server:
            ping, wall = self.probe(server)
            self.assertGreater(wall, 0.95)
            self.assertTrue(35 <= ping < 150, ping)

    def test_uses_lowest_of_several_samples(self):
        with fixture(delays=(0.18, 0.04, 0.08)) as server:
            ping, _ = self.probe(server)
            self.assertTrue(30 <= ping < 100, ping)
            self.assertEqual(len(server.clients), 3)

    def test_reports_measured_rtt_without_fixed_subtraction(self):
        with fixture() as server:
            ping, _ = self.probe(server)
            self.assertTrue(35 <= ping < 100, ping)

    def test_late_original_reply_keeps_its_own_send_time(self):
        with fixture(delays=(0.55, 0.20, 0.20)) as server:
            ping, _ = self.probe(server)
            self.assertTrue(170 <= ping < 320, ping)

    def test_duplicate_reply_cannot_be_a_new_sample(self):
        with fixture(delays=(0.08, 0.10, 0.12), duplicate=True) as server:
            ping, _ = self.probe(server)
            self.assertTrue(70 <= ping < 180, ping)
            self.assertEqual(len(server.clients), 3)

    def test_high_latency_server_still_responds(self):
        with fixture(delays=(0.65, 0.65, 0.65)) as server:
            ping, _ = self.probe(server)
            self.assertTrue(620 <= ping < 850, ping)

    def test_socket_failure_does_not_discard_a_faster_pending_sample(self):
        with fixture(delays=(0.70, 0.30)) as server:
            ping, _ = self.probe(server, fail_third=True)
            self.assertTrue(250 <= ping < 420, ping)

    def test_invalid_packets_do_not_finish_a_sample(self):
        with fixture(invalid=True) as server:
            ping, _ = self.probe(server)
            self.assertTrue(35 <= ping < 100, ping)

    def test_reply_from_another_port_is_ignored(self):
        with fixture(wrong_source=True) as server:
            ping, _ = self.probe(server)
            self.assertTrue(35 <= ping < 100, ping)

    def test_oversized_packet_does_not_discard_a_valid_reply(self):
        with fixture(oversized=True) as server:
            ping, _ = self.probe(server)
            self.assertTrue(35 <= ping < 100, ping)

    def test_one_good_sample_survives_lost_followups(self):
        with fixture(delays=(0.04, None, None)) as server:
            ping, wall = self.probe(server)
            self.assertTrue(35 <= ping < 100, ping)
            self.assertLess(wall, 2)

    def test_silent_server_keeps_existing_deadline(self):
        with fixture(delays=(None,)) as server:
            ping, wall = self.probe(server)
            self.assertEqual(ping, -1)
            self.assertTrue(1.4 <= wall < 2, wall)
            self.assertLessEqual(len(server.queries), 12)

    def test_four_concurrent_queries_keep_independent_samples(self):
        with fixture() as server, ThreadPoolExecutor(max_workers=4) as workers:
            results = list(workers.map(lambda _: self.probe(server), range(4)))
            self.assertTrue(all(35 <= ping < 150 for ping, _ in results), results)
            self.assertEqual(len(server.clients), 12)

    def test_protocol_fallbacks_and_followup_traffic(self):
        for protocol in REPLIES:
            with self.subTest(protocol=protocol), fixture(protocol=protocol) as server:
                ping, _ = self.probe(server)
                self.assertTrue(35 <= ping < 100, ping)
                self.assertEqual(len(server.clients), 3)
                # Once detected, followups need only the working protocol.
                self.assertEqual(len([kind for index, kind in server.queries if index > 0]), 2)

    def test_ipv6_source_and_resolved_address(self):
        try:
            server = Server(ipv6=True)
        except OSError as error:
            self.skipTest(f"IPv6 loopback unavailable: {error}")
        with server:
            thread = threading.Thread(target=lambda: server.serve_forever(poll_interval=0.01), daemon=True)
            thread.start()
            try:
                ping, _ = self.probe(server)
                self.assertTrue(35 <= ping < 100, ping)
            finally:
                server.shutdown()
                thread.join()


if __name__ == "__main__":
    unittest.main(verbosity=2)
