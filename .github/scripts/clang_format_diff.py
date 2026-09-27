#!/usr/bin/env python3
import collections
import os
import pathlib
import re
import subprocess
import sys

EXTENSIONS = {
    ".c", ".C", ".cc", ".cpp", ".cxx", ".c++",
    ".h", ".H", ".hh", ".hpp", ".hxx", ".h++",
    ".ino", ".pde", ".proto", ".cu",
}
HUNK = re.compile(r"@@ -\d+(?:,\d+)? \+(\d+)(?:,(\d+))? @@")


def eligible(path: str) -> bool:
    return (
        path.startswith("OptiScaler/")
        and not path.startswith("OptiScaler/include/")
        and pathlib.PurePosixPath(path).suffix in EXTENSIONS
    )


def merge_ranges(items):
    merged = []
    for start, end in sorted(items):
        if merged and start <= merged[-1][1] + 1:
            merged[-1] = (merged[-1][0], max(merged[-1][1], end))
        else:
            merged.append((start, end))
    return merged


def changed_ranges(base: str, head: str):
    diff = subprocess.check_output(
        [
            "git", "diff", "--unified=0", "--diff-filter=ACMR",
            base, head, "--", "OptiScaler",
        ],
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    ranges = collections.defaultdict(list)
    current = None
    for line in diff.splitlines():
        if line.startswith("+++ "):
            raw = line[4:]
            current = raw[2:] if raw.startswith("b/") else raw
            if current == "/dev/null" or not eligible(current):
                current = None
            continue
        if current is None or not line.startswith("@@ "):
            continue
        match = HUNK.search(line)
        if not match:
            continue
        start = int(match.group(1))
        count = int(match.group(2) or "1")
        if count:
            ranges[current].append((start, start + count - 1))
    return {path: merge_ranges(items) for path, items in ranges.items()}


def run_format(path: str, ranges):
    image = "ghcr.io/jidicula/clang-format:20"
    cwd = os.getcwd()
    args = [
        "docker", "run", "--rm",
        "-v", f"{cwd}:{cwd}",
        "-w", cwd,
        image,
        "--dry-run", "--Werror",
        "--style=file", "--fallback-style=llvm",
    ]
    for start, end in ranges:
        args.append(f"--lines={start}:{end}")
    args.append(path)
    return subprocess.run(args).returncode


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: clang_format_diff.py <base> <head>")
    base, head = sys.argv[1:3]
    ranges = changed_ranges(base, head)
    if not ranges:
        print("No changed C/C++/Protobuf/CUDA lines under OptiScaler; nothing to check.")
        return 0
    image = "ghcr.io/jidicula/clang-format:20"
    subprocess.run(["docker", "pull", image], check=True, stdout=subprocess.DEVNULL)
    subprocess.run(["docker", "run", "--rm", image, "--version"], check=True)

    failed = False
    for path in sorted(ranges):
        line_ranges = ranges[path]
        pretty = ", ".join(f"{start}:{end}" for start, end in line_ranges)
        print(f"Checking {path} changed lines {pretty}", flush=True)
        if run_format(path, line_ranges) != 0:
            failed = True

    if failed:
        print("clang-format failed on one or more changed-line ranges.", file=sys.stderr)
        return 1
    print("clang-format passed on all changed-line ranges.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
