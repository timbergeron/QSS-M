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
        if request["message"]["text"] == "language label test":
            return ProviderAnswer({"status": "translated", "text": f"Mock {request['target']} translation"})
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
        # Exercise the same list navigation, scrollbar math and ticker used by
        # Bookmarks; only rendering and unrelated engine input are stubbed.
        types_start = menu.rfind("typedef struct", 0, menu.index("} listsearch_t;"))
        types_end = menu.index("} menulist_t;", types_start) + len("} menulist_t;")
        ticker_start = menu.rfind("typedef struct", 0, menu.index("} menuticker_t;"))
        ticker_end = menu.index("} menuticker_t;", ticker_start) + len("} menuticker_t;")
        (directory / "translation-list-types.inc").write_text(menu[types_start:types_end] + "\n" + menu[ticker_start:ticker_end])
        lists_start = menu.index("void M_List_AutoScroll(")
        lists_end = menu.index("void M_DeletePrevWord(", lists_start)
        (directory / "translation-list.inc").write_text(menu[lists_start:lists_end] +
            function(menu, "static void M_Ticker_Init(") + function(menu, "static void M_Ticker_Update("))
        start = menu.index("/* Chat translation menu:")
        end = menu.index("/*\n==================\nM_LeaveMenuState", start)
        layout = "\n".join(line for line in menu.splitlines()
                           if line.startswith(("#define MENU_VALUE_X ", "#define MENU_CURSOR_X ",
                                               "#define CONTROLLER_DIM_ALPHA")))
        (directory / "translation-menu.inc").write_text(layout + "\n" +
            function(menu, "static void M_DrawCountHeader(") + menu[start:end])
        commands = (ROOT / "Quake/cmd.c").read_text()
        completion = function(commands, "cmd_function_t* Cmd_FindCommand (")
        (directory / "translation-completion.inc").write_text(completion)
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
        env = {**os.environ, "QSSM_TRANSLATE_TEST_URL": f"http://127.0.0.1:{port}/qssm/translate",
               "QSSM_TRANSLATE_TEST_DIR": str(directory)}
        try:
            child = await asyncio.create_subprocess_exec(str(binary), env=env)
            result = await asyncio.wait_for(child.wait(), 15)
            if result:
                raise RuntimeError(f"Translation client harness exited {result}")
            assert len(provider.requests) >= 8
            new_targets = {item["target"] for item in provider.requests if item["message"]["text"] == "language label test"}
            assert {"de", "fil", "fr"} <= new_targets
            assert any(item["channel"] == "team" for item in provider.requests)
            for item in provider.requests:
                serialized = json.dumps(item)
                assert "private context" not in serialized and "Rasta" not in serialized
                assert "Other Player" not in serialized and "Replacement" not in serialized
                if item["mode"] == "outgoing":
                    assert item["context"] == []
                if item["channel"] == "team":
                    assert item["mode"] == "outgoing" and item["context"] == []
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
