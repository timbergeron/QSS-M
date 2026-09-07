"""Exercise shutdown cancellation against a real, deliberately stalled HTTP server.

Run with python3 Misc/stress/test_quit_http.py (requires cc, SDL2 and libcurl).
Compiles the engine's transfer helper; no Quake assets or external network needed.
"""

from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import os
import shlex
import subprocess
import tempfile
import threading


ROOT = Path(__file__).resolve().parents[2]
net = (ROOT / "Quake/net_main.c").read_text()
start = net.index("static SDL_atomic_t net_web_shutting_down;")
helper = net[start:net.index("qsocket_t\t*net_activeSockets", start)]

SOURCE = r'''
#include <SDL.h>
#include <curl/curl.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
''' + helper + r'''
typedef struct {
    const char *url;
    SDL_atomic_t received;
    CURLcode result;
    int baseline;
    char body[32];
    size_t size;
} request_t;

static size_t receive(char *data, size_t size, size_t count, void *opaque)
{
    request_t *request = opaque;
    size_t bytes = size * count;
    if (request->size + bytes < sizeof(request->body)) {
        memcpy(request->body + request->size, data, bytes);
        request->size += bytes;
        request->body[request->size] = 0;
    }
    SDL_AtomicSet(&request->received, 1);
    return bytes;
}

static int progress(void *unused, curl_off_t a, curl_off_t b,
    curl_off_t c, curl_off_t d)
{
    (void)unused; (void)a; (void)b; (void)c; (void)d;
    return SDL_AtomicGet(&net_web_shutting_down);
}

static int abort_progress(void *unused, curl_off_t a, curl_off_t b,
    curl_off_t c, curl_off_t d)
{
    (void)unused; (void)a; (void)b; (void)c; (void)d;
    return 1;
}

static CURL *new_request(request_t *request)
{
    CURL *curl = curl_easy_init();
    assert(curl);
    curl_easy_setopt(curl, CURLOPT_URL, request->url);
    curl_easy_setopt(curl, CURLOPT_NOPROXY, "*");
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 5000L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, receive);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, request);
    return curl;
}

static int transfer(void *opaque)
{
    request_t *request = opaque;
    CURL *curl = new_request(request);
    if (request->baseline) {
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress);
    }
    request->result = request->baseline ? curl_easy_perform(curl) : NET_CurlEasyPerform(curl);
    curl_easy_cleanup(curl);
    return 0;
}

static void normal_transfers(const char *base)
{
    const char *paths[] = {"/ok", "/redirect", "/missing", "/post", "/head", "/stall", "/ok"};
    for (int i = 0; i < 7; ++i) {
        char url[256];
        request_t request = {0};
        long code = 0;
        snprintf(url, sizeof(url), "%s%s", base, paths[i]);
        request.url = url;
        CURL *curl = new_request(&request);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        if (i == 3) curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "payload");
        if (i == 4) curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
        if (i == 5) curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 150L);
        if (i == 6) {
            curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
            curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, abort_progress);
        }
        CURLcode result = NET_CurlEasyPerform(curl);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
        if (i == 5) assert(result == CURLE_OPERATION_TIMEDOUT);
        else if (i == 6) assert(result == CURLE_ABORTED_BY_CALLBACK);
        else {
            assert(result == CURLE_OK);
            assert(code == (i == 2 ? 404 : 200));
            assert(!strcmp(request.body, i == 3 ? "payload" : i == 4 ? "" : "ok"));
        }
        curl_easy_cleanup(curl);
    }
}

static double cancel_transfers(const char *base, int baseline)
{
    enum { COUNT = 4 };
    request_t requests[COUNT] = {0};
    SDL_Thread *threads[COUNT];
    char url[256];
    snprintf(url, sizeof(url), "%s/stall", base);
    SDL_AtomicSet(&net_web_shutting_down, 0);
    for (int i = 0; i < COUNT; ++i) {
        requests[i].url = url;
        requests[i].baseline = baseline;
        threads[i] = SDL_CreateThread(transfer, "http-test", &requests[i]);
        assert(threads[i]);
    }
    Uint32 start = SDL_GetTicks();
    for (int i = 0; i < COUNT; ++i) {
        while (!SDL_AtomicGet(&requests[i].received)) {
            assert(SDL_GetTicks() - start < 4000);
            SDL_Delay(1);
        }
    }
    SDL_Delay(100); /* each worker is now waiting for the rest of the response */
    Uint64 before = SDL_GetPerformanceCounter();
    NET_CancelWebRequests();
    NET_CancelWebRequests(); /* repeated shutdown paths are safe */
    for (int i = 0; i < COUNT; ++i) {
        SDL_WaitThread(threads[i], NULL);
        assert(requests[i].result == CURLE_ABORTED_BY_CALLBACK);
    }
    double elapsed = (SDL_GetPerformanceCounter() - before) * 1000.0 / SDL_GetPerformanceFrequency();
    if (!baseline) assert(elapsed < 750.0);
    return elapsed;
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    assert(SDL_Init(0) == 0);
    assert(curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK);
    normal_transfers(argv[1]);
    double baseline = cancel_transfers(argv[1], 1);
    double updated = cancel_transfers(argv[1], 0);
    request_t late = {0};
    late.url = argv[1];
    transfer(&late); /* queued/retried requests must not start after shutdown */
    assert(late.result == CURLE_ABORTED_BY_CALLBACK);
    assert(!SDL_AtomicGet(&late.received));
    curl_global_cleanup();
    SDL_Quit();
    printf("PASS: GET, redirect, HTTP status, POST, HEAD, timeout, progress abort, late request\n");
    printf("Four stalled transfers: progress-only cancellation %.1f ms; shutdown polling %.1f ms\n", baseline, updated);
    return 0;
}
'''


stop = threading.Event()


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *args):
        pass

    def do_GET(self):
        if self.path == "/redirect":
            self.send_response(302)
            self.send_header("Location", "/ok")
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        body = b"ok"
        if self.command == "POST":
            body = self.rfile.read(int(self.headers["Content-Length"]))
        self.send_response(404 if self.path == "/missing" else 200)
        self.send_header("Content-Length", "10000" if self.path == "/stall" else str(len(body)))
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(body)
            self.wfile.flush()
        if self.path == "/stall":
            stop.wait(10)
            self.close_connection = True

    do_HEAD = do_GET
    do_POST = do_GET


def main():
    flags = shlex.split(subprocess.check_output(
        ["pkg-config", "--cflags", "--libs", "sdl2", "libcurl"], text=True))
    with tempfile.TemporaryDirectory(prefix="qssm-quit-http-") as tmp:
        path = Path(tmp)
        source = path / "test.c"
        binary = path / "test"
        source.write_text(SOURCE)
        subprocess.run([os.environ.get("CC", "cc"), "-std=gnu11", "-Wall", "-Wextra",
                        "-Werror", str(source), "-o", str(binary), *flags], check=True)
        server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            subprocess.run([str(binary), f"http://127.0.0.1:{server.server_port}"],
                           check=True, timeout=20)
        finally:
            stop.set()
            server.shutdown()
            server.server_close()
            thread.join()


if __name__ == "__main__":
    main()
