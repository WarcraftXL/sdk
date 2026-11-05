#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# check_docgen.py <sdk dir> <out dir>: generates the API reference into <out dir> and checks what it holds.

import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path

sys.dont_write_bytecode = True

# A header in the shapes the parser must read: attributes between the class-key and the name, an
# explicit specialization, a {} default argument followed by another declaration.
FIXTURE = """namespace wxl
{
    /// A kept class.
    class [[nodiscard]] Kept
    {
    public:
        /// A member.
        void Run();
    };
    /// An aligned struct.
    struct alignas(16) [[deprecated("old")]] Aligned
    {
        /// A field.
        alignas(16) float values[4];
    };
    /// A specialization.
    template <>
    class [[nodiscard]] Kept<void> {};
    /// A brace default.
    void On(int subject = {});
    /// After the default.
    void After();
}
"""

# A class-key followed by an attribute, as a class's declaration opens.
CLASS_HEAD = re.compile(r"\b(?:class|struct|union)\s+(?:\[\[|alignas\b)")

# What the reference must list from the extension headers, as (kind, qualified name, file).
REQUIRED = [
    ("class", "wxl::Interceptor", "include/wxl/Intercept.hpp"),
    ("class", "wxl::Interception", "include/wxl/Intercept.hpp"),
    ("function", "wxl::Intercept", "include/wxl/Intercept.hpp"),
    ("function", "wxl::Patch", "include/wxl/Patch.hpp"),
    ("class", "wxl::Signal", "include/wxl/Signal.hpp"),
    ("class", "wxl::Connection", "include/wxl/Signal.hpp"),
    ("function", "wxl::SignalOf", "include/wxl/Signal.hpp"),
    ("class", "wxl::Extension", "include/wxl/Extension.hpp"),
    ("class", "wxl::Export", "include/wxl/Export.hpp"),
    ("function", "wxl::On", "include/wxl/Listener.hpp"),
    ("function", "wxl::TryOn", "include/wxl/Listener.hpp"),
    ("class", "wxl::Log", "include/wxl/Log.hpp"),
    ("class", "wxl::Service", "include/wxl/Service.hpp"),
    ("class", "wxl::Result", "include/wxl/Result.hpp"),
    ("class", "wxl::Result<void>", "include/wxl/Result.hpp"),
]


def walk(items):
    for it in items:
        yield it
        yield from walk(it.get("children", []))


def all_items(api):
    """Every item of the model, at every depth."""
    groups = [api["eventTypes"], api["framework"]["items"], api["objects"]]
    groups += [b["items"] for b in api["bindings"]]
    for group in groups:
        yield from walk(group)


def check_fixture(sdk):
    sys.path.insert(0, str(sdk / "tools" / "docgen"))
    from headers import parse_header
    with tempfile.TemporaryDirectory() as tmp:
        path = Path(tmp) / "Fixture.hpp"
        path.write_text(FIXTURE, encoding="utf-8")
        items = list(walk(parse_header(path, Path(tmp))["items"]))
    found = {(it["kind"], it["qname"]) for it in items}
    expected = {("class", "wxl::Kept"), ("function", "wxl::Kept::Run"), ("struct", "wxl::Aligned"),
                ("field", "wxl::Aligned::values"), ("class", "wxl::Kept<void>"), ("function", "wxl::On"),
                ("function", "wxl::After")}
    errors = ["fixture: %s %s not found" % pair for pair in sorted(expected - found)]
    errors += ["fixture: unexpected %s %s" % pair for pair in sorted(found - expected)]
    on = [it for it in items if it["qname"] == "wxl::On"]
    if on and not on[0]["signature"].endswith("= {})"):
        errors.append("fixture: wxl::On reads as '%s'" % on[0]["signature"])
    return errors


def check_output(sdk, out):
    gen = subprocess.run([sys.executable, str(sdk / "tools" / "docgen" / "generate.py"), "--out", str(out)])
    if gen.returncode:
        return ["generate.py failed (%d)" % gen.returncode]
    text = (out / "data.js").read_text(encoding="utf-8")
    api = json.loads(text[text.index("=") + 1:].rstrip().rstrip(";"))
    errors = []
    for it in all_items(api):
        if it["kind"] == "field" and CLASS_HEAD.search(it["signature"]):
            errors.append("%s:%s: a class read as a field: %s" % (it["file"], it["line"], it["signature"]))
    framework = {(it["kind"], it["qname"], it["file"]) for it in walk(api["framework"]["items"])}
    errors += ["extension API: no %s %s from %s" % row for row in REQUIRED if row not in framework]
    return errors


def main():
    if len(sys.argv) != 3:
        sys.exit("usage: check_docgen.py <sdk dir> <out dir>")
    sdk, out = Path(sys.argv[1]).resolve(), Path(sys.argv[2])
    errors = check_fixture(sdk) + check_output(sdk, out)
    for e in errors:
        print(e)
    sys.exit(1 if errors else 0)


if __name__ == "__main__":
    main()
