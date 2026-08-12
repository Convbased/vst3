"""Minimal Opus SDP munging (mirrors packages/sdk-js/src/sdp.ts).

Sets the average bitrate and stereo flags on the Opus ``a=fmtp`` line of an offer.
"""

from __future__ import annotations

import re


def apply_opus_sdp_options(sdp: str, *, bitrate_kbps: int = 64, stereo: bool = False) -> str:
    rtpmap = re.search(r"a=rtpmap:(\d+) opus/", sdp)
    if not rtpmap:
        return sdp
    pt = rtpmap.group(1)
    wanted = {
        "maxaveragebitrate": str(bitrate_kbps * 1000),
        "stereo": "1" if stereo else "0",
        "sprop-stereo": "1" if stereo else "0",
        "useinbandfec": "1",
    }

    fmtp_re = re.compile(rf"^a=fmtp:{pt} (.*)$", re.MULTILINE)
    existing = fmtp_re.search(sdp)
    if existing:
        params: dict[str, str] = {}
        for kv in existing.group(1).split(";"):
            kv = kv.strip()
            if "=" in kv:
                key, value = kv.split("=", 1)
                params[key] = value
        params.update(wanted)
        merged = ";".join(f"{k}={v}" for k, v in params.items())
        return fmtp_re.sub(f"a=fmtp:{pt} {merged}", sdp, count=1)

    line = ";".join(f"{k}={v}" for k, v in wanted.items())
    return re.sub(
        rf"(a=rtpmap:{pt} opus/[^\r\n]*)",
        rf"\1\r\na=fmtp:{pt} {line}",
        sdp,
        count=1,
    )
