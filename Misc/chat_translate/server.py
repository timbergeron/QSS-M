"""HTTP adapter and server-only OpenRouter credentials for QSS-M chat translation."""
from __future__ import annotations

import argparse
import asyncio
import ipaddress
import json
import logging
import os
from pathlib import Path

from aiohttp import ClientSession, ClientTimeout, TCPConnector, web

from service import (DailyCounter, IGNORED_PROVIDERS, LANGUAGES, MAX_BODY, MAX_OUTPUT_TOKENS, MODEL,
                     MODEL_SCHEMA, MockProvider, PROMPT, PROMPT_VERSION,
                     ProviderAnswer, REASONING_ENABLED, ServiceError,
                     TranslationService, strict_json)


class OpenRouterProvider:
    def __init__(self, session, api_key, model=MODEL):
        self.session, self._api_key, self.model = session, api_key, model

    def payload(self, request):
        # Only validated chat data reaches input. Clients cannot select tools,
        # model, instructions, URLs or generation settings. No provider session.
        data = {"target": LANGUAGES[request["target"]], "context": request["context"],
                "message": request["message"]}
        return {"model": self.model, "stream": False,
                "messages": [{"role": "system", "content": PROMPT},
                             {"role": "user", "content": json.dumps(data, ensure_ascii=False)}],
                "reasoning": {"enabled": REASONING_ENABLED},
                "max_tokens": MAX_OUTPUT_TOKENS,
                "response_format": {"type": "json_schema", "json_schema": {
                    "name": "qssm_chat_translation", "strict": True, "schema": MODEL_SCHEMA}},
                "provider": {"data_collection": "deny", "zdr": True,
                             "require_parameters": True, "allow_fallbacks": False,
                             "ignore": list(IGNORED_PROVIDERS),
                             "sort": "latency"}}

    async def translate(self, request):
        if not self._api_key:
            # A disabled deployment may start before its key is installed. Even
            # if the disable file is removed, never dispatch without a key.
            raise ServiceError(503, "unavailable", 60)
        # aiohttp does not retry POST requests. Do not add retries or redirects.
        async with self.session.post(
            "https://openrouter.ai/api/v1/chat/completions",
            headers={"Authorization": "Bearer " + self._api_key,
                     "HTTP-Referer": "https://timbergeron.com",
                     "X-OpenRouter-Title": "QSS-M Chat Translation"}, json=self.payload(request),
            allow_redirects=False,
        ) as response:
            if response.status == 429:
                # A provider quota is different from our per-IP/daily limits.
                # Back off new messages without retrying this request or exposing
                # provider error bodies. Bound untrusted delta-seconds headers.
                delay = response.headers.get("Retry-After", "").strip()
                seconds = min(300, max(5, int(delay))) if (
                    len(delay) <= 10 and delay.isascii() and delay.isdigit()) else 60
                raise ServiceError(503, "upstream_rate_limit", seconds)
            if response.status != 200:
                raise ServiceError(503, "upstream_failure", 5)
            body = bytearray()
            async for chunk in response.content.iter_chunked(4096):
                body.extend(chunk)
                if len(body) > 65536:
                    raise ServiceError(503, "upstream_failure", 5)
            try:
                result = strict_json(bytes(body))
                if type(result) is not dict:
                    return ProviderAnswer({})
                if result.get("error"):
                    raise ServiceError(503, "upstream_failure", 5)
                choices = result.get("choices")
                if type(choices) is not list or len(choices) != 1:
                    return ProviderAnswer({})
                choice = choices[0]
                if type(choice) is not dict or choice.get("finish_reason") != "stop":
                    return ProviderAnswer({})  # truncated/refused output is unusable
                message = choice.get("message")
                if (type(message) is not dict or message.get("role") != "assistant"
                        or message.get("tool_calls") or message.get("function_call")
                        or message.get("refusal")):
                    return ProviderAnswer({})
                text = message.get("content")
                if type(text) is not str:
                    return ProviderAnswer({})
                answer = strict_json(text.encode("utf-8"))
                usage = result.get("usage")
                tokens = {}
                if type(usage) is dict:
                    # Completion tokens include any reasoning. Do not count it
                    # again when estimating the billed output volume.
                    for source, dest in (("prompt_tokens", "total_input_tokens"),
                                         ("completion_tokens", "total_output_tokens")):
                        value = usage.get(source)
                        if type(value) is int and value >= 0:
                            tokens[dest] = value
                    details = usage.get("completion_tokens_details")
                    if type(details) is dict:
                        value = details.get("reasoning_tokens")
                        if type(value) is int and value >= 0:
                            tokens["total_thought_tokens"] = value
                return ProviderAnswer(answer, tokens)
            except ServiceError as error:
                if error.status == 503:
                    raise
                return ProviderAnswer({})
            except (TypeError, AttributeError, UnicodeError):
                return ProviderAnswer({})


def source_ip(peer, forwarded, trusted):
    try:
        address = ipaddress.ip_address(peer)
    except ValueError:
        raise ServiceError(400, "invalid_source") from None
    if forwarded and any(address in network for network in trusted):
        # Walk from the trusted peer, stripping only explicitly trusted hops.
        # A caller-controlled leftmost XFF entry is never automatically trusted.
        hops = forwarded.split(",")
        if len(hops) > 16:
            raise ServiceError(400, "invalid_source")
        while hops and any(address in network for network in trusted):
            try:
                address = ipaddress.ip_address(hops.pop().strip())
            except ValueError:
                raise ServiceError(400, "invalid_source") from None
    return str(address)


def create_app(service, trusted=()):
    networks = tuple(ipaddress.ip_network(item) for item in trusted)

    async def translate(request):
        try:
            if request.content_length and request.content_length > MAX_BODY:
                raise ServiceError(413, "body_too_large")
            # Reading incrementally enforces the bound for chunked requests too.
            raw = bytearray()
            async for chunk in request.content.iter_chunked(1024):
                raw.extend(chunk)
                if len(raw) > MAX_BODY:
                    raise ServiceError(413, "body_too_large")
            ip = source_ip(request.remote or "", request.headers.get("X-Forwarded-For"), networks)
            result = await service.translate(bytes(raw), ip, request.headers.get("Content-Type", ""))
            return web.json_response(result, headers={"Cache-Control": "no-store"})
        except ServiceError as error:
            headers = {"Cache-Control": "no-store"}
            if error.retry_after:
                headers["Retry-After"] = str(error.retry_after)
            return web.json_response({"error": error.code}, status=error.status, headers=headers)
        except asyncio.CancelledError:
            raise
        except Exception:
            # No exception text, provider error, request body or key in logs.
            return web.json_response({"error": "unavailable"}, status=503,
                                     headers={"Cache-Control": "no-store", "Retry-After": "30"})

    app = web.Application(client_max_size=MAX_BODY)
    app.router.add_post("/qssm/translate", translate)
    return app


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mock", action="store_true", help="fixture responses; no provider key or traffic")
    parser.add_argument("--mock-delay", type=float, default=0)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8787)
    parser.add_argument("--counter", required=True, help="persistent SQLite file, outside the served directory")
    parser.add_argument("--daily-limit", type=int, default=1000)
    parser.add_argument("--disabled-file", help="create this file to disable uploads immediately")
    parser.add_argument("--trusted-proxy", action="append", default=[], help="explicit proxy IP/CIDR; repeatable")
    args = parser.parse_args()
    key = os.environ.get("OPENROUTER_API_KEY", "").strip()
    disabled = args.disabled_file and Path(args.disabled_file).is_file()
    if not args.mock and not key and not disabled:
        parser.error("Set OPENROUTER_API_KEY in the server environment, or use --mock")
    counter = DailyCounter(args.counter, args.daily_limit)

    async def application():
        session = ClientSession(timeout=ClientTimeout(total=3), connector=TCPConnector(limit=4))
        provider = MockProvider(delay=args.mock_delay) if args.mock else OpenRouterProvider(session, key)
        service = TranslationService(provider, counter, disabled_file=args.disabled_file)
        app = create_app(service, args.trusted_proxy)

        async def report_metrics():
            while True:
                await asyncio.sleep(60)
                print(json.dumps({"translation_metrics": service.metrics()}), flush=True)

        async def startup(app):
            app["metrics_task"] = asyncio.create_task(report_metrics())

        async def cleanup(app):
            app["metrics_task"].cancel()
            await asyncio.gather(app["metrics_task"], return_exceptions=True)
            await session.close()
            counter.close()

        app.on_startup.append(startup)
        app.on_cleanup.append(cleanup)
        return app

    # No request/body access log or chat inspector. Reverse-proxy logging is a
    # separate hosting decision; never configure it to include POST bodies.
    logging.getLogger("aiohttp.server").setLevel(logging.CRITICAL)
    web.run_app(application(), host=args.host, port=args.port, access_log=None,
                handler_cancellation=True)


if __name__ == "__main__":
    main()
