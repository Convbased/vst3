from __future__ import annotations

import inspect
import unittest

from convbased import ConvbasedClient
from convbased.rtc_servers import fetch_rtc_servers
from convbased.signaling import SignalingChannel


class SdkAuthenticationTests(unittest.TestCase):
    def test_public_sdk_requires_api_key(self) -> None:
        for callable_object in (
            ConvbasedClient,
            SignalingChannel,
            fetch_rtc_servers,
        ):
            parameters = inspect.signature(callable_object).parameters
            self.assertIs(parameters["api_key"].default, inspect.Parameter.empty)

    def test_signaling_rejects_blank_api_key(self) -> None:
        with self.assertRaisesRegex(ValueError, "requires api_key"):
            SignalingChannel("wss://example.invalid/signaling/ws", api_key="   ")

    def test_signaling_uses_only_api_key_query_parameter(self) -> None:
        channel = SignalingChannel(
            "wss://example.invalid/signaling/ws",
            api_key="  sk_test  ",
        )
        self.assertIn("api_key=sk_test", channel._url)
        self.assertNotIn("token=", channel._url)


if __name__ == "__main__":
    unittest.main()
