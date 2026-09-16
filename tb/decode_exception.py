#!/usr/bin/env python3
"""Decode the 12 devmem words from read_exception_mailbox.sh (no writes)."""
import argparse
import json
from pathlib import Path

MAGIC = 0x4E58544449414731


def decode(text):
    words = [int(line.strip(), 16) for line in text.splitlines() if line.strip()]
    if len(words) != 12 or any(not 0 <= word < 1 << 64 for word in words):
        raise ValueError("expected twelve unsigned 64-bit devmem words")
    if words[:2] != words[10:]:
        raise ValueError("header changed during read; capture again without resetting")
    if words[0] != MAGIC:
        raise ValueError("diagnostic mailbox is not initialized (wrong RBF or reset in progress)")
    if words[1] == 0:
        return {"status": "armed; no qualifying diagnostic event published"}
    if words[1] != 1:
        raise ValueError("unknown capture status")
    w = words[2:10]
    h32 = lambda value: f"0x{value & 0xffffffff:08x}"
    h16 = lambda value: f"0x{value & 0xffff:04x}"
    version = w[7] >> 48
    if version == 3:
        reason = (w[7] >> 44) & 3
        match = (w[7] >> 32) & 15
        reasons = {1: "kernel unhandled trace", 2: "kernel breakpoint delivery",
                   3: "SIGTRAP exit fallback; origin not captured"}
        matches = {1: "matched original exception frame", 2: "frame already returned",
                   3: "no matching frame in retained history", 4: "exit fallback has no frame key"}
        if ((w[7] >> 46) & 3) != 3 or reason not in reasons or match not in matches:
            raise ValueError("invalid delivery record header")
        if any(w[4:7]) or w[7] & 0x00000FF00000F000:
            raise ValueError("reserved delivery payload fields are nonzero")
        if (reason == 3) != (match == 4):
            raise ValueError("invalid exit fallback match status")
        if match != 1 and (w[0] or w[1] & 0xffffffff or w[7] & 0xfff):
            raise ValueError("unmatched record must not claim user exception fields")
        return {
            "record_version": 3,
            "status": reasons[reason],
            "context": matches[match],
            "kernel_pc": h32(w[3]),
            "kernel_sp": h32(w[3] >> 32),
            "kernel_d2_low16": h16(w[7] >> 16),
            "frame_address": h32(w[1] >> 32) if reason != 3 else None,
            "urp": h32(w[2] >> 32),
            "usp": h32(w[2]),
            "exception": {
                "vector": w[7] & 0xff,
                "frame_format": (w[7] >> 8) & 15,
                "stacked_pc": h32(w[0]),
                "exception_address": h32(w[0] >> 32),
                "saved_sr": h16(w[1]),
                "ir": h16(w[1] >> 16),
            } if match == 1 else None,
        }
    if version != 2:
        raise ValueError("unsupported record version")
    if any(w[3:7]):
        raise ValueError("reserved payload words are nonzero")
    return {
        "status": "first qualifying user-mode exception captured",
        "vector": w[7] & 0xff,
        "frame_format": (w[7] >> 8) & 0xf,
        "stacked_pc": h32(w[0]),
        "exception_address": h32(w[0] >> 32),
        "saved_sr": h16(w[1]),
        "ir": h16(w[1] >> 16),
        "usp": h32(w[2]),
        "urp": h32(w[2] >> 32),
        "last_rte": {
            "valid": bool(w[7] & (1 << 32)),
            "target_pc": h32(w[1] >> 32),
            "restored_sr": h16(w[7] >> 16),
        },
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path)
    args = parser.parse_args()
    try:
        print(json.dumps(decode(args.capture.read_text()), indent=2))
    except (OSError, ValueError) as error:
        parser.exit(1, f"{error}\n")


if __name__ == "__main__":
    main()
