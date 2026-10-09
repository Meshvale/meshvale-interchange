# SPDX-License-Identifier: Apache-2.0
"""Check project C++ sources with the pinned Google-style formatter."""
import argparse
from pathlib import Path
import re
import subprocess
import sys


FORMATTER_VERSION = "23.1.3"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--formatter", default="clang-format")
    options = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    try:
        version = subprocess.run(
            [options.formatter, "--version"], cwd=root, check=True,
            capture_output=True, text=True,
        ).stdout
        match = re.search(r"\bclang-format version (\d+\.\d+\.\d+)\b", version)
        if match is None or match.group(1) != FORMATTER_VERSION:
            print(f"C++ format check requires clang-format {FORMATTER_VERSION}.", file=sys.stderr)
            return 2
        listed = subprocess.run(
            ["git", "ls-files", "-z", "--cached", "--others", "--exclude-standard"],
            cwd=root, check=True, capture_output=True,
        ).stdout.decode("utf-8", errors="surrogateescape")
        files = sorted({name for name in listed.split("\0") if name and
                        Path(name).suffix in {".h", ".hpp", ".cpp"} and
                        (root / name).is_file()})
        if files:
            subprocess.run(
                [options.formatter, "--style=file", "--dry-run", "--Werror", "--", *files],
                cwd=root, check=True,
            )
    except (OSError, subprocess.CalledProcessError) as error:
        print(f"C++ format check failed ({type(error).__name__}).", file=sys.stderr)
        return 1
    print(f"C++ format check passed: {len(files)} project files; clang-format {FORMATTER_VERSION}.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
