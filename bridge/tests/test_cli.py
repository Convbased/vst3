from __future__ import annotations

import unittest

from convbased_vst_bridge.cli import build_parser, options_from_args, redact_secrets


class CliTests(unittest.TestCase):
    def test_bridge_service_starts_without_environment_credentials(self) -> None:
        parser = build_parser()
        options = options_from_args(parser.parse_args([]), parser)

        self.assertEqual(options.host, "127.0.0.1")
        self.assertEqual(options.port, 43_127)
        self.assertEqual(options.jitter_prefetch, 3)
        self.assertFalse(hasattr(options, "api_key"))
        self.assertFalse(hasattr(options, "model_id"))

    def test_invalid_port_is_rejected(self) -> None:
        parser = build_parser()
        with self.assertRaises(SystemExit):
            options_from_args(parser.parse_args(["--port", "0"]), parser)

    def test_invalid_jitter_prefetch_is_rejected(self) -> None:
        parser = build_parser()
        with self.assertRaises(SystemExit):
            options_from_args(parser.parse_args(["--jitter-prefetch", "0"]), parser)

    def test_verbose_log_secrets_are_redacted(self) -> None:
        secret = "sk_sensitive-value"
        message = (
            f"GET /ws?api_key={secret}&mode=live HTTP/1.1 "
            f"x-api-key: {secret} "
            f'''{{"api_key": "{secret}"}}'''
        )

        redacted = redact_secrets(message)

        self.assertNotIn(secret, redacted)
        self.assertEqual(redacted.count("<redacted>"), 3)


if __name__ == "__main__":
    unittest.main()
