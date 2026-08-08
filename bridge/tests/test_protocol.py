from __future__ import annotations

import unittest

from convbased_vst_bridge.protocol import (
    AccountStatus,
    Hello,
    MessageType,
    ProtocolError,
    decode_auth,
    decode_account_status,
    decode_frame,
    decode_hello,
    encode_hello,
    encode_account_status,
    encode_message,
)


class ProtocolTests(unittest.TestCase):
    def test_message_round_trip(self) -> None:
        encoded = encode_message(MessageType.AUDIO_INPUT, 42, b"pcm")
        decoded = decode_frame(encoded)
        self.assertEqual(decoded.type, MessageType.AUDIO_INPUT)
        self.assertEqual(decoded.sequence, 42)
        self.assertEqual(decoded.payload, b"pcm")

    def test_hello_round_trip(self) -> None:
        hello = Hello(sample_rate=48_000, channels=2, max_block_frames=512)
        self.assertEqual(decode_hello(encode_hello(hello)), hello)

    def test_auth_payload_is_decoded_without_logging_or_shape_changes(self) -> None:
        self.assertEqual(decode_auth(b"sk_test-123"), "sk_test-123")

    def test_account_status_round_trip(self) -> None:
        status = AccountStatus(
            account_checked=True,
            authenticated=True,
            balance_checked=True,
            balance_available=True,
            realtime_enabled=True,
            model_configured=True,
            balance=16.25,
            label="录音间",
            model_id="model-1",
        )
        self.assertEqual(decode_account_status(encode_account_status(status)), status)

    def test_auth_payload_rejects_whitespace_and_non_ascii(self) -> None:
        for payload in (b"", b"test key", "密钥".encode()):
            with self.subTest(payload=payload), self.assertRaises(ProtocolError):
                decode_auth(payload)

    def test_invalid_magic_is_rejected(self) -> None:
        encoded = bytearray(encode_message(MessageType.STATUS, 0))
        encoded[:4] = b"FAIL"
        with self.assertRaises(ProtocolError):
            decode_frame(bytes(encoded))


if __name__ == "__main__":
    unittest.main()
