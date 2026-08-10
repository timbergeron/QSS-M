#!/usr/bin/env python3
"""Focused unit tests for stress-harness provenance and RCON oracles."""

import hashlib
import json
import os
import socket
import struct
import sys
import tempfile
import threading
import unittest
from unittest import mock
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import qssm_stress as S
import rcon_probe as R


def reply_packet(text, declared=None):
    payload = bytes([S.CCREP_RCON]) + text + b"\x00"
    actual = len(payload) + 4
    header = 0x80000000 | (actual if declared is None else declared)
    return struct.pack(">I", header) + payload


class BinaryProvenanceTests(unittest.TestCase):
    def test_discovery_prefers_stress_build_over_newer_production_build(self):
        with tempfile.TemporaryDirectory() as tmp:
            stress = Path(tmp) / "stress-QSS-M"
            production = Path(tmp) / "production-QSS-M"
            stress.write_bytes(b"STRESS_READY\x00_stress_status\x00")
            production.write_bytes(b"production")
            os.utime(stress, (100, 100))
            os.utime(production, (200, 200))

            with mock.patch.object(S, "DEFAULT_BIN_GLOB", str(Path(tmp) / "*")):
                self.assertEqual(S.find_binary(), stress)

    def test_content_identity_and_manifest(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / "engine"
            binary.write_bytes(b"abc")
            provenance = S.binary_provenance(binary)

            self.assertEqual(provenance["path"], str(binary.resolve()))
            self.assertEqual(provenance["size"], 3)
            self.assertEqual(provenance["sha256"], hashlib.sha256(b"abc").hexdigest())
            self.assertRegex(provenance["mtime_utc"],
                             r"^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z$")

            results = Path(tmp) / "results"
            runner = S.Runner(S.Config(binary=binary, results=results,
                                       require_stress_hooks=False))
            try:
                manifest = json.loads((results / "binary.json").read_text())
                self.assertEqual(manifest, runner.binary_provenance)
                self.assertEqual(manifest["sha256"], provenance["sha256"])
            finally:
                runner.close()

    def test_non_stress_binary_fails_before_launch(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / "production-engine"
            binary.write_bytes(b"no test command channel")
            with self.assertRaisesRegex(S.StressBinaryError,
                                        "lacks QSSM_STRESS hooks"):
                S.Runner(S.Config(binary=binary,
                                  results=Path(tmp) / "results"))
            manifest = json.loads(
                (Path(tmp) / "results" / "binary.json").read_text())
            self.assertFalse(manifest["stress_hooks"])


class RconReplyTests(unittest.TestCase):
    def test_complete_reply(self):
        text, meta = S.decode_rcon_reply(reply_packet(b"Player pos"))
        self.assertEqual(text, "Player pos")
        self.assertFalse(meta["truncated"])
        self.assertFalse(meta["malformed"])
        self.assertFalse(meta["redirect_saturated"])
        self.assertEqual(meta["received"], meta["declared"])

    def test_transport_truncation_is_visible(self):
        text, meta = S.decode_rcon_reply(reply_packet(b"partial", declared=100))
        self.assertEqual(text, "partial")
        self.assertTrue(meta["truncated"])
        self.assertTrue(meta["malformed"])

    def test_engine_redirect_saturation_is_distinct(self):
        text, meta = S.decode_rcon_reply(
            reply_packet(b"x" * S.RCON_REDIRECT_TEXT_LIMIT))
        self.assertEqual(len(text), S.RCON_REDIRECT_TEXT_LIMIT)
        self.assertTrue(meta["redirect_saturated"])
        self.assertFalse(meta["truncated"])
        self.assertFalse(meta["malformed"])

    def test_rcon_socket_receives_entire_redirect_packet(self):
        server = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        server.bind(("127.0.0.1", 0))
        server.settimeout(2)
        errors = []

        def serve_once():
            try:
                _, address = server.recvfrom(2048)
                server.sendto(
                    reply_packet(b"x" * S.RCON_REDIRECT_TEXT_LIMIT), address)
            except Exception as exc:  # surfaced in the test thread below
                errors.append(exc)

        thread = threading.Thread(target=serve_once)
        thread.start()
        client = S.Rcon(server.getsockname()[1], password="test")
        try:
            reply = client.send("edicts", want_reply=True)
        finally:
            client.close()
            thread.join(timeout=3)
            server.close()

        self.assertFalse(thread.is_alive())
        self.assertEqual(errors, [])
        self.assertEqual(len(reply), S.RCON_REDIRECT_TEXT_LIMIT)
        self.assertEqual(client.last_reply_meta["received"],
                         client.last_reply_meta["declared"])
        self.assertFalse(client.last_reply_meta["truncated"])
        self.assertTrue(client.last_reply_meta["redirect_saturated"])


class StateOracleTests(unittest.TestCase):
    def test_viewpos_round_trip_parser(self):
        actual = R.parse_viewpos("noise\nPlayer pos: (-520 800 88) 0 270 0\n")
        self.assertEqual(actual, (-520.0, 800.0, 88.0, 0.0, 270.0, 0.0))
        self.assertTrue(R.position_matches(actual, (-520.5, 800, 88), 0.5))
        self.assertFalse(R.position_matches(actual, (-522, 800, 88), 0.5))
        self.assertIsNone(R.parse_viewpos("no position printed"))

    def test_fully_spawned_requires_local_server(self):
        self.assertTrue(R.is_fully_spawned(
            {"state": "2", "signon": "4", "sv": "1"}))
        self.assertFalse(R.is_fully_spawned(
            {"state": "2", "signon": "3", "sv": "1"}))
        self.assertFalse(R.is_fully_spawned(
            {"state": "2", "signon": "4", "sv": "0"}))


if __name__ == "__main__":
    unittest.main()
