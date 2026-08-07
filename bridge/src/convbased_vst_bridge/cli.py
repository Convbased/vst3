from __future__ import annotations

import argparse
import asyncio
import logging
import os
import re
from collections.abc import Sequence

from .runtime import BridgeOptions, BridgeServer


_SECRET_PATTERNS = (
    re.compile(r"(?i)([?&]api_key=)[^&\s]+"),
    re.compile(r"(?i)(\bx-api-key\s*:\s*)[^\s,;]+"),
    re.compile(r'''(?i)(["']api_key["']\s*:\s*["'])[^"']+'''),
)


def redact_secrets(message: str) -> str:
    for pattern in _SECRET_PATTERNS:
        message = pattern.sub(r"\1<redacted>", message)
    return message


class SecretRedactionFilter(logging.Filter):
    def filter(self, record: logging.LogRecord) -> bool:
        record.msg = redact_secrets(record.getMessage())
        record.args = ()
        return True


def configure_logging(verbose: bool) -> None:
    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s %(levelname)s %(name)s: %(message)s",
    )
    detail_level = logging.DEBUG if verbose else logging.INFO
    logging.getLogger("convbased-vst-bridge").setLevel(detail_level)
    logging.getLogger("convbased").setLevel(detail_level)
    for handler in logging.getLogger().handlers:
        handler.addFilter(SecretRedactionFilter())


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
