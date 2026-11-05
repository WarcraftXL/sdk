#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Generates the offline API reference (docs/site/, or --out) from the doc comments of the public headers.

import argparse
import sys
from pathlib import Path

# The modules beside this script are imported from the source tree: leave no __pycache__ in it.
sys.dont_write_bytecode = True

from api_model import build_api, count_items
from events_md import render as render_events_md
from cpp_lexer import ParseError
from llms_text import render_full, render_index
from site_files import write_site

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent


def parse_args():
    parser = argparse.ArgumentParser(description="Generates the API reference from the SDK's public headers.")
    parser.add_argument("--out", type=Path, default=ROOT / "docs" / "site",
                        help="the folder to write (emptied first); default docs/site")
    parser.add_argument("--events-md", action="store_true",
                        help="write docs/events.md from the event table instead of the site")
    return parser.parse_args()


def summary(api, index_txt, full_txt):
    """What was generated, in two lines of counts."""
    scripts, framework_items = api["scripts"], api["framework"]["items"]
    objects, bindings = api["objects"], api["bindings"]
    return [
        "events: %d, hooks: %s, framework items: %d, objects: %d (%d members), binding files: %d (%d items)" % (
            len(api["events"]), ", ".join("%s %d" % (s["name"], len(s["hooks"])) for s in scripts),
            count_items(framework_items), len(objects), count_items(objects) - len(objects),
            len(bindings), sum(count_items(b["items"]) for b in bindings)),
        "llms.txt: %d KiB, llms-full.txt: %d KiB" % (
            len(index_txt.encode("utf-8")) // 1024, len(full_txt.encode("utf-8")) // 1024),
    ]


def main():
    args = parse_args()
    if not (ROOT / "include" / "wxl").is_dir():
        sys.exit("include/wxl not found under " + str(ROOT))
    try:
        api = build_api(ROOT)
        if args.events_md:
            (ROOT / "docs" / "events.md").write_text(render_events_md(api), encoding="utf-8", newline="\n")
            print("wrote " + str(ROOT / "docs" / "events.md"))
            return
        index_txt, full_txt = render_index(api), render_full(api)
        write_site(args.out, api, HERE / "site", index_txt, full_txt)
    except (ParseError, OSError) as e:
        sys.exit("generate.py: %s" % e)
    for line in summary(api, index_txt, full_txt):
        print(line)
    print("wrote " + str(args.out))


if __name__ == "__main__":
    main()
