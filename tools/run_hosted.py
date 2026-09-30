#!/usr/bin/env python3
"""Compile and run the portable, nonprivileged part of the new coverage.

Use --cc to select a native GCC/Clang compiler. This is an additional reference
check, not a replacement for booting the 32-bit image on the target machine.
"""
import argparse
import os
from pathlib import Path
import subprocess
import re
from coverage_report import ROWS, HOSTED_CASES_PER_PASS


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default=os.environ.get("CC", "cc"))
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    build = root / "build" / "hosted"
    build.mkdir(parents=True, exist_ok=True)
    executable = build / ("hosted-tests.exe" if os.name == "nt" else "hosted-tests")
    sources = ["tools/hosted_runner.c", "src/testfw.c"]
    sources += [row[2] for row in ROWS if row[3]]
    command = [args.cc, "-std=gnu11", "-O2", "-ffreestanding", "-mno-sse",
               "-mno-mmx", "-mno-80387", "-Wall", "-Wextra", "-Werror", "-Iinclude"]
    subprocess.run(command + sources + ["-o", str(executable)], cwd=root, check=True)
    result = subprocess.run([str(executable)], cwd=root, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    print(result.stdout, end="")
    result.check_returncode()
    match = re.search(r"HOSTED total=(\d+) pass=(\d+) fail=0 exec=0 skip=0", result.stdout)
    expected = HOSTED_CASES_PER_PASS * 2
    if not match or tuple(map(int, match.groups())) != (expected, expected):
        raise SystemExit(f"Hosted assertion count drifted; expected {expected} across two passes")


if __name__ == "__main__":
    main()
