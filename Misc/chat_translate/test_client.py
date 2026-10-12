"""Compile and exercise the production client against a private loopback mock.

Requires a C compiler, SDL3/libcurl pkg-config packages and requirements.txt.
No game files, window, user config or live model calls are involved.
"""
import asyncio
import argparse
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

from aiohttp import web
from server import create_app
from service import DailyCounter, MockProvider, ProviderAnswer, RateLimiter, TranslationService

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent


def function(source, declaration):
    start = source.index(declaration)
    return source[start:source.index("\n}", start) + 2] + "\n"


class ClientMock(MockProvider):
    def __init__(self):
        super().__init__()
        self.requests = []

    async def translate(self, request):
        self.requests.append(request)
        if request["message"]["text"] == "delay":
            await asyncio.sleep(.8)
            return ProviderAnswer({"status": "translated", "text": "Delayed message"})
        if request["message"]["text"] == "Send %h; quit":
            return ProviderAnswer({"status": "translated", "text": "Enviar %h; quit"})
        return await super().translate(request)


async def main(sanitize=False):
    common = (ROOT / "Quake/common.c").read_text()
    # The Unicode fallback is production code too, not a test approximation.
    unicode = '#include "quakedef.h"\n'
    unicode += function(common, "size_t UTF8_WriteCodePoint(")
    start = common.index("static uint32_t UTF8_ReadCodePoint")
    unicode += common[start:common.index("\nvoid SetChatInfo", start)]
    with tempfile.TemporaryDirectory(prefix="qssm-translate-") as directory:
        directory = Path(directory)
        source = directory / "unicode.c"
        source.write_text(unicode)
        menu = (ROOT / "Quake/menu.c").read_text()
        start = menu.index("/* Chat translation menu:")
        end = menu.index("/*\n==================\nM_LeaveMenuState", start)
        (directory / "translation-menu.inc").write_text(menu[start:end])
        binary = directory / "translation-client"
        flags = shlex.split(subprocess.check_output(
            ["pkg-config", "--cflags", "--libs", "sdl3", "libcurl"], text=True))
        compiler = shlex.split(os.environ.get("CC", "cc"))
        sanitizers = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"] if sanitize else []
        subprocess.run([*compiler, "-std=gnu11", "-g", "-Wall", "-Wextra", "-Werror",
                        "-Wno-missing-field-initializers", "-DQSSM_TRANSLATE_TEST",
                        "-I" + str(ROOT / "Quake"), "-I" + str(directory), str(HERE / "client_harness.c"), str(source),
                        "-o", str(binary), *sanitizers, *flags, "-lm"], check=True)
        provider = ClientMock()
        counter = DailyCounter(directory / "counter.sqlite3")
        service = TranslationService(provider, counter, limiter=RateLimiter(rate=1000, burst=100))
        app = create_app(service)
        runner = web.AppRunner(app, access_log=None, handler_cancellation=True)
        await runner.setup()
        site = web.TCPSite(runner, "127.0.0.1", 0)
        await site.start()
        port = site._server.sockets[0].getsockname()[1]
        env = {**os.environ, "QSSM_TRANSLATE_TEST_URL": f"http://127.0.0.1:{port}/qssm/translate"}
        try:
            child = await asyncio.create_subprocess_exec(str(binary), env=env)
            result = await asyncio.wait_for(child.wait(), 15)
            if result:
                raise RuntimeError(f"Translation client harness exited {result}")
            assert len(provider.requests) >= 8
            for item in provider.requests:
                serialized = json.dumps(item)
                assert "private context" not in serialized and "Rasta" not in serialized
                assert "Other Player" not in serialized and "Replacement" not in serialized
                if item["mode"] == "outgoing":
                    assert item["context"] == []
            # Catch-up includes useful same-speaker context, exactly once.
            dale = next(item for item in provider.requests if item["message"]["text"] == "dale")
            assert [line["text"] for line in dale["context"]] == ["hola", "cambiamos mapa?"]
            print(f"Verified {len(provider.requests)} mock requests; no unrelated context or live model traffic.")
        finally:
            await runner.cleanup()
            counter.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sanitize", action="store_true", help="enable ASan and UBSan")
    asyncio.run(main(parser.parse_args().sanitize))
