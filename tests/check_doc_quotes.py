#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# check_doc_quotes.py <sdk dir>: docs/ quotes its examples exactly, its links resolve, events.md is rendered from Events.def.

import re
import sys
from pathlib import Path

QUOTE = re.compile(r"<!-- quote: (\S+) -->")
FENCE = re.compile(r"^\s*```")
LINK = re.compile(r"\]\(([^)#:]+\.md)\)")
EXAMPLE = re.compile(r"`(examples/[\w-]+)")


def lines_of(path):
    return [line.rstrip() for line in path.read_text(encoding="utf-8").splitlines()]


def fenced_block(lines, start):
    """The lines of the fenced block opening at or after `start`, or None."""
    i = start
    while i < len(lines) and not lines[i].strip():
        i += 1
    if i >= len(lines) or not FENCE.match(lines[i]):
        return None
    end = i + 1
    while end < len(lines) and not FENCE.match(lines[end]):
        end += 1
    return lines[i + 1:end]


def contains(haystack, needle):
    n = len(needle)
    return any(haystack[i:i + n] == needle for i in range(len(haystack) - n + 1))


def check(root):
    errors = []
    for doc in sorted((root / "docs").glob("*.md")):
        lines = lines_of(doc)
        for number, line in enumerate(lines, 1):
            for match in QUOTE.finditer(line):
                source = root / match.group(1)
                block = fenced_block(lines, number)
                if not source.is_file():
                    errors.append(f"{doc.name}:{number}: {match.group(1)} does not exist")
                elif not block:
                    errors.append(f"{doc.name}:{number}: no code block after the quote marker")
                elif not contains(lines_of(source), block):
                    errors.append(f"{doc.name}:{number}: the block is not in {match.group(1)}; copy it again")
            for match in LINK.finditer(line):
                if not (doc.parent / match.group(1)).is_file():
                    errors.append(f"{doc.name}:{number}: link to a missing {match.group(1)}")
            for match in EXAMPLE.finditer(line):
                if not (root / match.group(1)).is_dir():
                    errors.append(f"{doc.name}:{number}: no {match.group(1)} folder")
    return errors


def check_event_list(root):
    """docs/events.md is what docgen renders from the event table."""
    sys.path.insert(0, str(root / "tools" / "docgen"))
    sys.dont_write_bytecode = True
    from api_model import build_api
    from events_md import render
    expected = render(build_api(root))
    if (root / "docs/events.md").read_text(encoding="utf-8").replace("\r\n", "\n") == expected:
        return []
    return ["events.md: not what the event table gives; run python3 tools/docgen/generate.py --events-md"]


def main():
    root = Path(sys.argv[1])
    errors = check(root) + check_event_list(root)
    for error in errors:
        print(error)
    print(f"{len(errors)} error(s)")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
