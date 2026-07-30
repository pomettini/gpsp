#!/usr/bin/env python3
"""Summarize a gpsp BLOCKPROFILE=1 translated-block trace."""

import argparse
import collections
import struct
import sys


HEADER = struct.Struct("<8sIIIII")
RECORD = struct.Struct("<I")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", help="blockprof.bin copied from Playdate Data")
    parser.add_argument("--top", type=int, default=50,
                        help="number of guest-PC rows to show (default: 50)")
    args = parser.parse_args()

    with open(args.trace, "rb") as trace:
        raw_header = trace.read(HEADER.size)
        if len(raw_header) != HEADER.size:
            raise SystemExit("trace is shorter than its header")
        magic, version, period, count, dropped, capacity = HEADER.unpack(raw_header)
        if magic != b"GPSPBLK1" or version != 1:
            raise SystemExit(f"unsupported trace magic/version: {magic!r}/{version}")
        payload = trace.read()

    expected = count * RECORD.size
    if len(payload) != expected:
        raise SystemExit(f"record payload is {len(payload)} bytes, expected {expected}")

    by_pc = collections.Counter(pc for (pc,) in RECORD.iter_unpack(payload))
    print(f"samples={count} dropped={dropped} capacity={capacity} period={period}")
    print(f"sampled-block span~{count * period:,}")
    print("\nTop guest block PCs")
    for pc, hits in by_pc.most_common(args.top):
        share = 100.0 * hits / count if count else 0.0
        label = "RAM/unknown" if pc == 0 else f"0x{pc & ~1:08x}"
        mode = "Thumb" if pc & 1 else "ARM"
        print(f"  {label:<12} {mode:<5} {hits:7d}  {share:6.2f}%")


if __name__ == "__main__":
    try:
        main()
    except BrokenPipeError:
        sys.exit(0)
