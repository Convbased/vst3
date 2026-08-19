from __future__ import annotations

import argparse
import asyncio
import logging
import os
from collections.abc import Sequence

from .runtime import BridgeOptions, BridgeServer


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Convbased VST3 SDK bridge")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument(
        "--port", type=int, default=int(os.getenv("CONVBASED_VST_BRIDGE_PORT", "43127"))
    )
    parser.add_argument("--signaling-url", default=os.getenv("CONVBASED_SIGNALING_URL"))
    parser.add_argument("--graphql-url", default=os.getenv("CONVBASED_GRAPHQL_URL"))
    parser.add_argument("--verbose", action="store_true")
    return parser


def options_from_args(
    args: argparse.Namespace, parser: argparse.ArgumentParser
) -> BridgeOptions:
    if not 1 <= args.port <= 65_535:
        parser.error("--port must be between 1 and 65535")

    return BridgeOptions(
        signaling_url=args.signaling_url,
        graphql_url=args.graphql_url,
        host=args.host,
        port=args.port,
    )


async def async_main(argv: Sequence[str] | None = None) -> None:
    parser = build_parser()
    args = parser.parse_args(argv)
    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format="%(asctime)s %(levelname)s %(name)s: %(message)s",
    )
    await BridgeServer(options_from_args(args, parser)).serve()


def main() -> None:
    try:
        asyncio.run(async_main())
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
