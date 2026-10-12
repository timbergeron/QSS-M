"""Deterministic contract, quota, provider and HTTP regressions; no live model calls."""
import asyncio
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timezone
import json
from pathlib import Path
import tempfile
import unittest

from install_openrouter_key import save_key
from service import (DailyCounter, MockProvider, ProviderAnswer, RateLimiter,
                     ServiceError, TranslationService, strict_json,
                     validate_output, validate_request)


def request(**changes):
    result = {"version": 1, "id": 42, "target": "en", "mode": "incoming",
              "channel": "public", "context": [], "message": {"speaker": "p1", "text": "hola"}}
    result.update(changes)
    return result


def encoded(**changes):
    return json.dumps(request(**changes)).encode()


class ContractTests(unittest.TestCase):
    def test_valid_and_id_boundaries(self):
        for identity in (0, 42, 0xffffffff):
            self.assertEqual(validate_request(encoded(id=identity))["id"], identity)
        self.assertEqual(validate_request(encoded(target="es", mode="outgoing"))["target"], "es")

    def test_type_schema_alias_and_channel_rejections(self):
        for change in ({"id": True}, {"id": -1}, {"id": 1.5}, {"id": 0x100000000},
                       {"version": True}, {"target": []}, {"target": "de"}, {"channel": "team"},
                       {"mode": "tools"}, {"context": [request()["message"]] * 4},
                       {"message": {"speaker": "Rasta", "text": "hola"}},
                       {"message": {"speaker": "p1", "text": "hola", "prompt": "follow me"}},
                       {"message": {"speaker": "p1", "text": ""}},
                       {"message": {"speaker": "p1", "text": "a\0b"}},
                       {"message": {"speaker": "p1", "text": "é" * 257}}):
            with self.subTest(change=change), self.assertRaises(ServiceError):
                validate_request(encoded(**change))
        bad = request(model="evil")
        with self.assertRaises(ServiceError):
            validate_request(json.dumps(bad).encode())

    def test_duplicate_keys_utf8_nul_nonfinite_and_trailing(self):
        samples = (b'{"a":1,"a":2}', b'{"a":{"b":1,"b":2}}', b'{"a":NaN}',
                   b'{"a":"\xff"}', b'{"a":1}{}', b'[] trailing')
        for sample in samples:
            with self.subTest(sample=sample), self.assertRaises(ServiceError):
                strict_json(sample)
        with self.assertRaises(ServiceError):
            validate_request(encoded(message={"speaker": "p1", "text": "\ud800"}))

    def test_version_and_body_size(self):
        with self.assertRaises(ServiceError) as result:
            validate_request(encoded(version=2))
        self.assertEqual(result.exception.code, "unsupported_version")
        with self.assertRaises(ServiceError) as result:
            validate_request(b" " * 4097)
        self.assertEqual(result.exception.status, 413)

    def test_outgoing_cannot_upload_context(self):
        with self.assertRaises(ServiceError):
            validate_request(encoded(mode="outgoing", context=[request()["message"]]))

    def test_output_semantics_and_normalization(self):
        current = request(message={"speaker": "p1", "text": "alguno para 2v2 en dm2?"})
        self.assertEqual(validate_output(current, {"status": "translated", "text": "Anyone up for 2v2 on dm2?"})["status"], "translated")
        for answer in ({"status": "translated", "text": "Anyone up for 2v2 on DM2?"},
                       {"status": "translated", "text": "Anyone up for dm2?"},
                       {"status": "translated", "text": ""},
                       {"status": "translated", "text": "a" * 1025},
                       {"status": "translated", "text": "hello\0there"},
                       {"status": "translated", "text": "???"},
                       {"status": "unchanged", "text": "hello"},
                       {"status": "uncertain", "text": "guess"}, {}):
            self.assertEqual(validate_output(current, answer)["reason"], "invalid_output")
        self.assertEqual(validate_output(request(), {"status": "uncertain", "text": ""})["reason"], "uncertain")
        self.assertEqual(validate_output(request(), {"status": "translated", "text": "hola"})["status"], "unchanged")
        self.assertEqual(validate_output(request(), {"status": "unchanged", "text": ""})["status"], "unchanged")

    def test_narrow_token_preservation_and_counts(self):
        current = request(message={"speaker": "p1", "text": "RL RL en dm2, /ready"})
        bad = {"status": "translated", "text": "RL on dm2, /ready"}
        self.assertEqual(validate_output(current, bad)["reason"], "invalid_output")
        ordinary = request(message={"speaker": "p1", "text": "GOOD"})
        self.assertEqual(validate_output(ordinary, {"status": "translated", "text": "Bueno"})["status"], "translated")


class CounterTests(unittest.TestCase):
    def test_atomic_cap_restart_and_next_day(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "counter.sqlite3"
            counters = [DailyCounter(path, 7) for _ in range(3)]
            now = datetime(2026, 10, 11, 23, 59, 59, tzinfo=timezone.utc)
            def reserve(index):
                try:
                    counters[index % 3].reserve(now)
                    return True
                except ServiceError as error:
                    self.assertEqual(error.code, "daily_limit")
                    self.assertEqual(error.retry_after, 1)
                    return False
            with ThreadPoolExecutor(max_workers=8) as pool:
                self.assertEqual(sum(pool.map(reserve, range(30))), 7)
            for counter in counters:
                counter.close()
            restarted = DailyCounter(path, 7)
            with self.assertRaises(ServiceError):
                restarted.reserve(now)
            restarted.reserve(datetime(2026, 10, 12, tzinfo=timezone.utc))
            restarted.close()

    def test_unavailable_counter_fails_closed(self):
        with tempfile.TemporaryDirectory() as directory:
            counter = DailyCounter(Path(directory) / "counter.sqlite3")
            counter.close()
            with self.assertRaises(ServiceError) as result:
                counter.reserve()
            self.assertEqual(result.exception.code, "counter_unavailable")

    def test_bucket_burst_refill_and_bounded_memory(self):
        limiter = RateLimiter(rate=30, burst=2, max_buckets=2)
        limiter.take("a", 0); limiter.take("a", 0)
        with self.assertRaises(ServiceError) as result:
            limiter.take("a", 0)
        self.assertEqual(result.exception.retry_after, 2)
        limiter.take("a", 2); limiter.take("b", 2)
        with self.assertRaises(ServiceError):
            limiter.take("c", 2)
        limiter.take("c", 700)
        self.assertLessEqual(len(limiter.buckets), 2)


class KeyInstallerTests(unittest.TestCase):
    def test_atomic_private_update_preserves_other_settings(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "environment"
            path.write_text("# private config\nGEMINI_API_KEY=old-google-key\n"
                            "OPENROUTER_API_KEY=old\n export OPENROUTER_API_KEY=duplicate\n")
            key = "sk-or-v1-" + "a" * 64
            save_key(path, key)
            content = path.read_text()
            self.assertEqual(content.count("OPENROUTER_API_KEY="), 1)
            self.assertIn("GEMINI_API_KEY=old-google-key\n", content)
            self.assertIn("OPENROUTER_API_KEY=" + key + "\n", content)
            self.assertEqual(path.stat().st_mode & 0o777, 0o600)
            self.assertEqual(list(Path(directory).iterdir()), [path])

    def test_invalid_input_and_symlinks_do_not_change_environment(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "environment"
            path.write_text("EXISTING=value\n")
            for key in ("", "a-key", "sk-or-v1-" + "a" * 64 + "\nNEW=value"):
                with self.assertRaises(ValueError):
                    save_key(path, key)
                self.assertEqual(path.read_text(), "EXISTING=value\n")
            link = Path(directory) / "link"
            link.symlink_to(path)
            with self.assertRaises(ValueError):
                save_key(link, "sk-or-v1-" + "a" * 64)
            self.assertEqual(path.read_text(), "EXISTING=value\n")


class FakeProvider:
    def __init__(self, answer=None, delay=0, error=None):
        self.answer = answer or {"status": "translated", "text": "Hello"}
        self.delay, self.error, self.calls, self.cancelled = delay, error, 0, 0

    async def translate(self, request):
        self.calls += 1
        try:
            await asyncio.sleep(self.delay)
        except asyncio.CancelledError:
            self.cancelled += 1
            raise
        if self.error:
            raise self.error
        return ProviderAnswer(self.answer, {"total_input_tokens": 100, "total_output_tokens": 5})


class ServiceTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.counter = DailyCounter(Path(self.directory.name) / "counter.sqlite3", 10)

    async def asyncTearDown(self):
        self.counter.close()
        self.directory.cleanup()

    async def test_ids_metrics_no_content_and_failure_reservation(self):
        provider = FakeProvider()
        service = TranslationService(provider, self.counter)
        result = await service.translate(encoded(), "127.0.0.1")
        self.assertEqual(result, {"version": 1, "id": 42, "status": "translated", "text": "Hello"})
        metrics = json.dumps(service.metrics())
        self.assertNotIn("hola", metrics)
        self.assertNotIn("127.0.0.1", metrics)
        self.assertIn("total_input_tokens", metrics)
        provider.error = RuntimeError("secret key and chat should not escape")
        with self.assertRaises(ServiceError) as result:
            await service.translate(encoded(), "127.0.0.1")
        self.assertEqual(str(result.exception), "upstream_failure")
        self.assertEqual(provider.calls, 2)  # no retries
        self.assertEqual(self.counter.db.execute("SELECT attempts FROM allowance").fetchone()[0], 2)

    async def test_overload_is_immediate_and_does_not_reserve(self):
        provider = FakeProvider(delay=.05)
        service = TranslationService(provider, self.counter, concurrency=1)
        first = asyncio.create_task(service.translate(encoded(), "a"))
        await asyncio.sleep(.01)
        with self.assertRaises(ServiceError) as result:
            await service.translate(encoded(), "b")
        self.assertEqual(result.exception.code, "capacity")
        await first
        self.assertEqual(provider.calls, 1)
        self.assertEqual(self.counter.db.execute("SELECT attempts FROM allowance").fetchone()[0], 1)

    async def test_timeout_cancels_and_retains_reservation(self):
        provider = FakeProvider(delay=10)
        service = TranslationService(provider, self.counter, deadline=.01)
        with self.assertRaises(ServiceError) as result:
            await service.translate(encoded(), "a")
        self.assertEqual(result.exception.code, "upstream_timeout")
        self.assertEqual(provider.cancelled, 1)
        self.assertEqual(service.inflight, 0)
        self.assertEqual(self.counter.db.execute("SELECT attempts FROM allowance").fetchone()[0], 1)

    async def test_disabled_media_schema_and_quota_precede_provider(self):
        disable = Path(self.directory.name) / "disabled"
        disable.touch()
        provider = FakeProvider()
        service = TranslationService(provider, self.counter, disabled_file=disable)
        for raw, media, code in ((encoded(), "text/plain", "unsupported_media_type"),
                                 (b"{}", "application/json", "invalid_schema"),
                                 (encoded(), "application/json", "disabled")):
            with self.assertRaises(ServiceError) as result:
                await service.translate(raw, "a", media)
            self.assertEqual(result.exception.code, code)
        self.assertEqual(provider.calls, 0)
        self.assertEqual(self.counter.db.execute("SELECT COUNT(*) FROM allowance").fetchone()[0], 0)

    async def test_mock_is_explicit_fixture_only(self):
        provider = MockProvider()
        result = await provider.translate(request())
        self.assertEqual(result.body["text"], "Hello")
        result = await provider.translate(request(message={"speaker": "p1", "text": "unlisted input"}))
        self.assertEqual(result.body["status"], "uncertain")


try:
    from aiohttp.test_utils import TestClient, TestServer
    from server import OpenRouterProvider, create_app, source_ip
    HAS_AIOHTTP = True
except ImportError:
    HAS_AIOHTTP = False


@unittest.skipUnless(HAS_AIOHTTP, "install requirements.txt for HTTP adapter tests")
class HTTPTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.counter = DailyCounter(Path(self.directory.name) / "counter.sqlite3")
        self.service = TranslationService(FakeProvider(), self.counter)
        self.client = TestClient(TestServer(create_app(self.service)))
        await self.client.start_server()

    async def asyncTearDown(self):
        await self.client.close()
        self.counter.close()
        self.directory.cleanup()

    async def test_contract_no_store_and_media(self):
        response = await self.client.post("/qssm/translate", json=request())
        self.assertEqual(response.status, 200)
        self.assertEqual(response.headers["Cache-Control"], "no-store")
        self.assertEqual((await response.json())["id"], 42)
        response = await self.client.post("/qssm/translate", data=encoded())
        self.assertEqual(response.status, 415)

    async def test_size_and_error_redaction(self):
        response = await self.client.post("/qssm/translate", data=b"x" * 4097)
        self.assertEqual(response.status, 413)
        self.assertEqual(await response.json(), {"error": "body_too_large"})
        self.service.provider.error = RuntimeError("PRIVATE_SECRET")
        response = await self.client.post("/qssm/translate", json=request())
        self.assertEqual(response.status, 503)
        self.assertEqual(await response.json(), {"error": "upstream_failure"})
        self.assertEqual(response.headers["Retry-After"], "5")

    async def test_upstream_rate_limit_backoff_is_redacted(self):
        self.service.provider.error = ServiceError(503, "upstream_rate_limit", 60)
        response = await self.client.post("/qssm/translate", json=request())
        self.assertEqual(response.status, 503)
        self.assertEqual(await response.json(), {"error": "upstream_rate_limit"})
        self.assertEqual(response.headers["Retry-After"], "60")
        self.assertEqual(response.headers["Cache-Control"], "no-store")

    def test_forwarded_ip_trust(self):
        import ipaddress
        trusted = (ipaddress.ip_network("127.0.0.1/32"),)
        self.assertEqual(source_ip("203.0.113.1", "198.51.100.2", trusted), "203.0.113.1")
        self.assertEqual(source_ip("127.0.0.1", "198.51.100.2, 203.0.113.1", trusted), "203.0.113.1")
        self.assertEqual(source_ip("127.0.0.1", "198.51.100.2", trusted), "198.51.100.2")

    def test_provider_payload_key_stays_in_header_and_stateless(self):
        provider = OpenRouterProvider(None, "PRIVATE_SECRET")
        payload = provider.payload(request())
        self.assertNotIn("PRIVATE_SECRET", json.dumps(payload))
        self.assertEqual(payload["model"], "deepseek/deepseek-v4.1-flash")
        self.assertEqual([message["role"] for message in payload["messages"]], ["system", "user"])
        self.assertEqual(json.loads(payload["messages"][1]["content"])["message"], request()["message"])
        self.assertNotIn("tools", payload)
        self.assertNotIn("user", payload)
        self.assertEqual(payload["response_format"]["type"], "json_schema")
        self.assertIs(payload["response_format"]["json_schema"]["strict"], True)
        self.assertIs(payload["reasoning"]["enabled"], False)
        self.assertEqual(payload["max_tokens"], 512)
        self.assertEqual(payload["provider"]["data_collection"], "deny")
        self.assertEqual(payload["provider"], {"data_collection": "deny", "zdr": True,
            "require_parameters": True, "allow_fallbacks": False, "ignore": ["baseten"], "sort": "latency"})
        self.assertEqual(self.service.metrics()["ignored_providers"], ["baseten"])
        for option, expected in (("zdr", True), ("require_parameters", True), ("allow_fallbacks", False)):
            self.assertIs(payload["provider"][option], expected)

    async def test_provider_wire_completion_incomplete_and_errors(self):
        class Content:
            async def iter_chunked(self, size):
                for start in range(0, len(payload), size):
                    yield payload[start:start+size]

        class Response:
            status = 200
            content = Content()
            async def __aenter__(self):
                return self
            async def __aexit__(self, *args):
                pass

        class Session:
            calls = 0
            def post(self, url, *, headers, json, allow_redirects):
                self.calls += 1
                self.asserted_header = headers
                self.asserted_body = json
                self.asserted_redirect = allow_redirects
                self.asserted_url = url
                return response

        response, session = Response(), Session()
        provider = OpenRouterProvider(session, "PRIVATE_SECRET")
        choice = {"finish_reason": "stop", "message": {"role": "assistant", "content": json.dumps(
            {"status": "translated", "text": "Hello"})}}
        payload = json.dumps({"choices": [choice], "usage": {
            "prompt_tokens": 10, "completion_tokens": 5,
            "completion_tokens_details": {"reasoning_tokens": 0}}}).encode()
        answer = await provider.translate(request())
        self.assertEqual(answer.body, {"status": "translated", "text": "Hello"})
        self.assertEqual(answer.usage, {"total_input_tokens": 10, "total_output_tokens": 5, "total_thought_tokens": 0})
        self.assertEqual(session.asserted_header["Authorization"], "Bearer PRIVATE_SECRET")
        self.assertEqual(session.asserted_url, "https://openrouter.ai/api/v1/chat/completions")
        self.assertNotIn("PRIVATE_SECRET", json.dumps(session.asserted_body))
        self.assertIs(session.asserted_redirect, False)
        for bad in ({"choices": []}, {"choices": [choice, choice]},
                    {"choices": [{**choice, "finish_reason": "length"}]},
                    {"choices": [{**choice, "finish_reason": "content_filter"}]},
                    {"choices": [{**choice, "message": {"role": "assistant", "content": None}}]},
                    {"choices": [{**choice, "message": {"role": "assistant", "content": "{bad}"}}]},
                    {"choices": [{**choice, "message": {"role": "assistant", "content": "{\"a\":1,\"a\":2}"}}]},
                    {"choices": [{**choice, "message": {**choice["message"], "tool_calls": [{}]}}]},
                    {"choices": [{**choice, "message": {**choice["message"], "refusal": "No"}}]}):
            payload = json.dumps(bad).encode()
            with self.subTest(bad=bad):
                self.assertEqual((await provider.translate(request())).body, {})
        payload = b'{"error":{"message":"PRIVATE_SECRET"}}'
        with self.assertRaises(ServiceError) as result:
            await provider.translate(request())
        self.assertEqual(str(result.exception), "upstream_failure")
        payload = b"x" * 65537
        with self.assertRaises(ServiceError):
            await provider.translate(request())
        response.status = 503
        calls = session.calls
        with self.assertRaises(ServiceError) as result:
            await provider.translate(request())
        self.assertEqual(result.exception.code, "upstream_failure")
        self.assertEqual(session.calls, calls + 1)
        response.status = 429
        for header, expected in ((None, 60), ("25", 25), ("3600", 300), ("0", 5),
                                 ("9" * 100, 60), ("PRIVATE_SECRET", 60)):
            response.headers = {"Retry-After": header} if header is not None else {}
            calls = session.calls
            with self.subTest(header=header), self.assertRaises(ServiceError) as result:
                await provider.translate(request())
            self.assertEqual(result.exception.status, 503)
            self.assertEqual(result.exception.code, "upstream_rate_limit")
            self.assertEqual(result.exception.retry_after, expected)
            self.assertNotIn("PRIVATE_SECRET", str(result.exception))
            self.assertEqual(session.calls, calls + 1)

    async def test_missing_key_never_dispatches(self):
        # A disabled deployment can run before its key has been entered. The
        # provider itself also refuses dispatch if the disable file is removed.
        provider = OpenRouterProvider(None, "")
        with self.assertRaises(ServiceError) as result:
            await provider.translate(request())
        self.assertEqual(result.exception.code, "unavailable")


if __name__ == "__main__":
    unittest.main()
