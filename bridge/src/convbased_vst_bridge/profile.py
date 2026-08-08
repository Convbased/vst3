from __future__ import annotations

import asyncio
import contextlib
import logging
import math
from collections.abc import Awaitable, Callable
from dataclasses import dataclass
from typing import Any

import aiohttp

_log = logging.getLogger("convbased-vst-bridge.profile")
_DEVICE_PROFILE_QUERY = (
    "query { myDeviceProfile { model_id preferences realtime_enabled label } }"
)
_ACCOUNT_BALANCE_QUERY = "query { me { wallet_balance } }"


class DeviceProfileError(RuntimeError):
    """A credential-safe device-profile resolution failure."""


class AccountBalanceError(RuntimeError):
    """A credential-safe account-balance resolution failure."""


@dataclass(frozen=True, slots=True)
class DeviceProfile:
    model_id: str | None
    preferences: dict[str, Any]
    realtime_enabled: bool
    label: str | None = None

    @property
    def runnable(self) -> bool:
        return self.realtime_enabled and bool(self.model_id)


@dataclass(frozen=True, slots=True)
class ProfileSnapshot:
    version: int
    profile: DeviceProfile | None


def parse_device_profile_response(payload: Any) -> DeviceProfile | None:
    if not isinstance(payload, dict):
        raise DeviceProfileError("device profile response is not a JSON object")

    errors = payload.get("errors")
    if isinstance(errors, list) and errors:
        summaries: list[str] = []
        for error in errors[:3]:
            extensions = error.get("extensions") if isinstance(error, dict) else None
            code = extensions.get("code") if isinstance(extensions, dict) else None
            field = extensions.get("field") if isinstance(extensions, dict) else None
            summary = str(code or "ERROR")
            if field:
                summary += f" field={field}"
            summaries.append(summary)
        raise DeviceProfileError(
            f"device profile query rejected ({'; '.join(summaries)})"
        )

    data = payload.get("data")
    if not isinstance(data, dict) or "myDeviceProfile" not in data:
        raise DeviceProfileError("device profile response is missing data")

    raw_profile = data["myDeviceProfile"]
    if raw_profile is None:
        return None
    if not isinstance(raw_profile, dict):
        raise DeviceProfileError("device profile has an invalid shape")

    raw_model_id = raw_profile.get("model_id")
    model_id = raw_model_id.strip()[:128] if isinstance(raw_model_id, str) else None
    raw_label = raw_profile.get("label")
    label = " ".join(raw_label.split())[:128] if isinstance(raw_label, str) else None
    preferences = raw_profile.get("preferences")
    if preferences is None:
        preferences = {}
    if not isinstance(preferences, dict):
        raise DeviceProfileError("device profile preferences must be an object")

    return DeviceProfile(
        model_id=model_id or None,
        preferences=dict(preferences),
        # Profiles created before this switch existed remain enabled.
        realtime_enabled=raw_profile.get("realtime_enabled") is not False,
        label=label or None,
    )


def parse_account_balance_response(payload: Any) -> float:
    if not isinstance(payload, dict):
        raise AccountBalanceError("account balance response is not a JSON object")
    errors = payload.get("errors")
    if isinstance(errors, list) and errors:
        summaries: list[str] = []
        for error in errors[:3]:
            extensions = error.get("extensions") if isinstance(error, dict) else None
            code = extensions.get("code") if isinstance(extensions, dict) else None
            field = extensions.get("field") if isinstance(extensions, dict) else None
            summary = str(code or "ERROR")
            if field:
                summary += f" field={field}"
            summaries.append(summary)
        raise AccountBalanceError(
            f"account balance query rejected ({'; '.join(summaries)})"
        )
    data = payload.get("data")
    me = data.get("me") if isinstance(data, dict) else None
    raw_balance = me.get("wallet_balance") if isinstance(me, dict) else None
    if isinstance(raw_balance, bool) or not isinstance(raw_balance, (int, float)):
        raise AccountBalanceError("account balance response is missing wallet_balance")
    balance = float(raw_balance)
    if not math.isfinite(balance):
        raise AccountBalanceError("account balance response is not finite")
    return balance


async def fetch_device_profile(
    *, graphql_url: str, api_key: str, timeout: float = 10.0
) -> DeviceProfile | None:
    if not graphql_url:
        raise ValueError("graphql_url is required")
    if not api_key:
        raise ValueError("api_key is required")

    client_timeout = aiohttp.ClientTimeout(total=timeout)
    try:
        async with (
            aiohttp.ClientSession(timeout=client_timeout) as session,
            session.post(
                graphql_url,
                json={"query": _DEVICE_PROFILE_QUERY},
                headers={"content-type": "application/json", "x-api-key": api_key},
            ) as response,
        ):
            response.raise_for_status()
            payload = await response.json()
    except (aiohttp.ClientError, asyncio.TimeoutError, ValueError) as exc:
        raise DeviceProfileError("device profile request failed") from exc

    return parse_device_profile_response(payload)


async def fetch_account_balance(
    *, graphql_url: str, api_key: str, timeout: float = 5.0
) -> float:
    if not graphql_url:
        raise ValueError("graphql_url is required")
    if not api_key:
        raise ValueError("api_key is required")

    client_timeout = aiohttp.ClientTimeout(total=timeout)
    try:
        async with (
            aiohttp.ClientSession(timeout=client_timeout) as session,
            session.post(
                graphql_url,
                json={"query": _ACCOUNT_BALANCE_QUERY},
                headers={"content-type": "application/json", "x-api-key": api_key},
            ) as response,
        ):
            response.raise_for_status()
            payload = await response.json()
    except (aiohttp.ClientError, asyncio.TimeoutError, ValueError) as exc:
        raise AccountBalanceError("account balance request failed") from exc

    return parse_account_balance_response(payload)


ProfileFetcher = Callable[..., Awaitable[DeviceProfile | None]]


class DeviceProfileMonitor:
    def __init__(
        self,
        *,
        graphql_url: str,
        api_key: str,
        poll_seconds: float = 20.0,
        fetcher: ProfileFetcher = fetch_device_profile,
    ) -> None:
        if poll_seconds <= 0:
            raise ValueError("poll_seconds must be positive")
        self._graphql_url = graphql_url
        self._api_key = api_key
        self._poll_seconds = poll_seconds
        self._fetcher = fetcher
        self._snapshot = ProfileSnapshot(version=0, profile=None)
        self._condition = asyncio.Condition()
        self._task: asyncio.Task[None] | None = None

    @property
    def snapshot(self) -> ProfileSnapshot:
        return self._snapshot

    async def start(self) -> None:
        if self._task is not None:
            return
        await self.refresh()
        self._task = asyncio.create_task(self._poll(), name="device-profile-monitor")

    async def close(self) -> None:
        task, self._task = self._task, None
        if task is None:
            return
        task.cancel()
        with contextlib.suppress(asyncio.CancelledError):
            await task

    async def refresh(self) -> ProfileSnapshot:
        profile = await self._fetcher(
            graphql_url=self._graphql_url,
            api_key=self._api_key,
        )
        if profile == self._snapshot.profile and self._snapshot.version > 0:
            return self._snapshot

        async with self._condition:
            self._snapshot = ProfileSnapshot(
                version=self._snapshot.version + 1,
                profile=profile,
            )
            self._condition.notify_all()
            return self._snapshot

    async def wait_for_runnable(self) -> ProfileSnapshot:
        async with self._condition:
            await self._condition.wait_for(
                lambda: (
                    self._snapshot.profile is not None
                    and self._snapshot.profile.runnable
                )
            )
            return self._snapshot

    async def wait_for_change(self, version: int) -> ProfileSnapshot:
        async with self._condition:
            await self._condition.wait_for(lambda: self._snapshot.version != version)
            return self._snapshot

    async def _poll(self) -> None:
        while True:
            await asyncio.sleep(self._poll_seconds)
            try:
                await self.refresh()
            except DeviceProfileError as exc:
                # A transient failed read cannot silently alter a working session.
                _log.warning("profile refresh failed; keeping current profile: %s", exc)
