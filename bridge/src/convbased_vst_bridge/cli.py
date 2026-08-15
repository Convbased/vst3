from __future__ import annotations

import argparse
import asyncio
import logging
import os
import re
from collections.abc import Sequence

from .runtime import BridgeOptions, BridgeServer

_SECRET_PATTERNS = (
    (re.compile(r"(?i)([?&](?:api_key|token|ticket)=)[^&\s]+"), r"\1<redacted>"),
    (re.compile(r"(?i)(\bx-api-key\s*:\s*)[^\s,;]+"), r"\1<redacted>"),
    (re.compile(r"(?i)(\bbearer\s+)[A-Za-z0-9._-]+"), r"\1<redacted>"),
    (
        re.compile(
            r'''(?i)(["'](?:api_key|access_token|token|ticket)["']\s*:\s*["'])[^"']+'''
        ),
        r"\1<redacted>",
    ),
    (re.compile(r"\bst_[A-Za-z0-9_-]{43}\b"), "<redacted>"),
    (re.compile(r"\bsk_[A-Za-z0-9._:-]{8,}\b"), "<redacted>"),
)

_LOG_FORMAT = "%(asctime)s %(levelname)s %(name)s: %(message)s"


def redact_secrets(message: str) -> str:
    for pattern, replacement in _SECRET_PATTERNS:
        message = pattern.sub(replacement, message)
    return message


class SecretRedactingFormatter(logging.Formatter):
    def format(self, record: logging.LogRecord) -> str:
        return redact_secrets(super().format(record))


def configure_logging(verbose: bool) -> None:
    logging.basicConfig(
        level=logging.INFO,
        format=_LOG_FORMAT,
    )
    detail_level = logging.DEBUG if verbose else logging.INFO
    logging.getLogger("convbased-vst-bridge").setLevel(detail_level)
    logging.getLogger("convbased").setLevel(detail_level)
    for handler in logging.getLogger().handlers:
        handler.setFormatter(SecretRedactingFormatter(_LOG_FORMAT))


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Convbased VST3 SDK bridge")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument(
        "--port", type=int, default=int(os.getenv("CONVBASED_VST_BRIDGE_PORT", "43127"))
    )
    parser.add_argument("--signaling-url", default=os.getenv("CONVBASED_SIGNALING_URL"))
    parser.add_argument("--graphql-url", default=os.getenv("CONVBASED_GRAPHQL_URL"))
    parser.add_argument(
        "--jitter-prefetch",
        type=int,
        default=int(os.getenv("CONVBASED_JITTER_PREFETCH", "3")),
    )
    parser.add_argument("--verbose", action="store_true")
    return parser


def options_from_args(
    args: argparse.Namespace, parser: argparse.ArgumentParser
) -> BridgeOptions:
    if not 1 <= args.port <= 65_535:
        parser.error("--port must be between 1 and 65535")
    if not 1 <= args.jitter_prefetch <= 16:
        parser.error("--jitter-prefetch must be between 1 and 16")

    return BridgeOptions(
        signaling_url=args.signaling_url,
        graphql_url=args.graphql_url,
        host=args.host,
        port=args.port,
        jitter_prefetch=args.jitter_prefetch,
    )


async def async_main(argv: Sequence[str] | None = None) -> None:
    parser = build_parser()
    args = parser.parse_args(argv)
    configure_logging(args.verbose)
    await BridgeServer(options_from_args(args, parser)).serve()


def main() -> None:
    try:
        asyncio.run(async_main())
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
