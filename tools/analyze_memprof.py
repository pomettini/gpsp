#!/usr/bin/env python3
"""Summarize a gpsp MEMPROFILE=1 trace."""

import argparse
import collections
import struct
import sys


HEADER = struct.Struct("<8sIIIII")
RECORD = struct.Struct("<III")
KINDS = (
    "store8", "store16", "store32", "store32-safe",
    "load8", "load-s8", "load16", "load-s16", "load32",
)
REGIONS = (
    "BIOS", "unused", "EWRAM", "IWRAM", "I/O", "palette", "VRAM",
    "OAM", "ROM0", "ROM1", "ROM2", "ROM3", "ROM4", "EEPROM",
    "backup", "invalid",
)


def percent(count, total):
    return 100.0 * count / total if total else 0.0


def print_counter(title, counter, total, limit=None, formatter=str):
    print(f"\n{title}")
    items = counter.most_common(limit)
    for key, count in items:
        print(f"  {formatter(key):<34} {count:7d}  {percent(count, total):6.2f}%")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", help="memprof.bin copied from Playdate Data")
    parser.add_argument("--top", type=int, default=30,
                        help="number of guest-PC rows to show (default: 30)")
    args = parser.parse_args()

    with open(args.trace, "rb") as trace:
        raw_header = trace.read(HEADER.size)
        if len(raw_header) != HEADER.size:
            raise SystemExit("trace is shorter than its header")
        magic, version, period, count, dropped, capacity = HEADER.unpack(raw_header)
        if magic != b"GPSPMEM1" or version != 1:
            raise SystemExit(f"unsupported trace magic/version: {magic!r}/{version}")
        payload = trace.read()

    expected = count * RECORD.size
    if len(payload) != expected:
        raise SystemExit(f"record payload is {len(payload)} bytes, expected {expected}")

    by_kind = collections.Counter()
    by_region = collections.Counter()
    by_pc = collections.Counter()
    by_pc_kind_region = collections.Counter()
    by_page = collections.Counter()

    for pc, address, kind in RECORD.iter_unpack(payload):
        region = min(address >> 24, 15)
        by_kind[kind] += 1
        by_region[region] += 1
        by_pc[pc] += 1
        by_pc_kind_region[(pc, kind, region)] += 1
        by_page[address >> 12] += 1

    print(f"samples={count} dropped={dropped} capacity={capacity} period={period}")
    print(f"sampled-operation span~{count * period:,}")
    print_counter("Operation kinds", by_kind, count,
                  formatter=lambda k: KINDS[k] if k < len(KINDS) else f"kind-{k}")
    print_counter("Address regions", by_region, count,
                  formatter=lambda r: REGIONS[r])
    print_counter("Top guest PCs", by_pc, count, args.top,
                  formatter=lambda pc: f"0x{pc:08x}")
    print_counter("Top PC / operation / region tuples", by_pc_kind_region,
                  count, args.top,
                  formatter=lambda x: (f"0x{x[0]:08x} "
                                       f"{KINDS[x[1]] if x[1] < len(KINDS) else x[1]} "
                                       f"{REGIONS[x[2]]}"))
    print_counter("Top 4KB target pages", by_page, count, args.top,
                  formatter=lambda page: f"0x{page << 12:08x}")


if __name__ == "__main__":
    try:
        main()
    except BrokenPipeError:
        sys.exit(0)
