from __future__ import annotations

import asyncio
import unittest

from convbased_vst_bridge.profile import (
    DeviceProfile,
    DeviceProfileError,
    DeviceProfileMonitor,
    parse_device_profile_response,
)


class DeviceProfileParsingTests(unittest.TestCase):
    def test_parses_model_preferences_and_runtime_switch(self) -> None:
        profile = parse_device_profile_response(
            {
                "data": {
                    "myDeviceProfile": {
                        "model_id": " model-1 ",
                        "preferences": {"pitch": 4},
                        "realtime_enabled": True,
                    }
                }
            }
        )
        self.assertEqual(
            profile,
            DeviceProfile(
                model_id="model-1",
                preferences={"pitch": 4},
                realtime_enabled=True,
            ),
        )
        self.assertTrue(profile.runnable)

    def test_missing_profile_is_not_runnable(self) -> None:
        self.assertIsNone(
            parse_device_profile_response({"data": {"myDeviceProfile": None}})
        )

    def test_graphql_error_only_exposes_safe_codes(self) -> None:
        with self.assertRaisesRegex(
            DeviceProfileError, r"FORBIDDEN field=myDeviceProfile"
        ):
            parse_device_profile_response(
                {
                    "errors": [
                        {
                            "message": "sensitive backend detail",
                            "extensions": {
                                "code": "FORBIDDEN",
                                "field": "myDeviceProfile",
                            },
                        }
                    ]
                }
            )


class DeviceProfileMonitorTests(unittest.IsolatedAsyncioTestCase):
    async def test_waits_until_backend_profile_is_runnable(self) -> None:
        configured = DeviceProfile(
            model_id="model-1",
            preferences={"pitch": 2},
            realtime_enabled=True,
        )
        responses = [None, configured]

        async def fetcher(**_kwargs):
            return responses.pop(0) if responses else configured

        monitor = DeviceProfileMonitor(
            graphql_url="https://example.invalid/graphql",
            api_key="test-key-long-enough",
            poll_seconds=0.01,
            fetcher=fetcher,
        )
        await monitor.start()
        try:
            snapshot = await asyncio.wait_for(monitor.wait_for_runnable(), timeout=0.2)
            self.assertEqual(snapshot.profile.model_id, "model-1")
        finally:
            await monitor.close()


if __name__ == "__main__":
    unittest.main()
