# SPDX-License-Identifier: GPL-3.0-or-later
# The plain-text renderings of the API model: llms.txt (one line per symbol) and llms-full.txt.

import re

from signatures import param_tuple

INTRO = [
    "WarcraftXL client-modding framework: the API an extension is written against.",
    "",
    "Generated from the doc comments of the SDK's public headers by",
    "tools/docgen/generate.py. Regenerate it rather than editing it.",
    "",
    "An extension is a DLL with one class derived from wxl::Extension: it overrides the",
    "events it needs and OnStart, and Export makes it the DLL's entry point. Its name and",
    "version come from its wxl.json. Everything else -- events, handles, bindings -- is",
    "reached from there.",
    "",
    "    #include <wxl/wxl.hpp>",
    "    using namespace wxl;",
    "",
    "    class MyExtension : public Extension {",
    "        void OnStart() override { On<WorldEnter>([](auto& e) { Log::Info(\"map {}\", e.mapId); }); }",
    "        void OnEndScene(void* device) override { /* draw, read the world, edit */ }",
    "    };",
    "",
    "    Export<MyExtension> extension;   // writes WXL_Query and WXL_Load",
    "",
    "wxl/offsets/ is opt-in (WarcraftXL::offsets): an extension reaches engine addresses",
    "through the bindings in wxl::game rather than by including it.",
    "",
]


def first_sentence(text, cap=140):
    """The first sentence of a description, on one line, shortened past cap characters."""
    if not text:
        return ""
    head = re.sub(r"\s+", " ", re.split(r"\n\s*\n", text.strip())[0])
    m = re.match(r"(.+?\.)(\s|$)", head)
    out = m.group(1) if m else head
    return out if len(out) <= cap else out[:cap - 3].rstrip() + "..."


def call_form(item, name):
    """The qualified name with its parameter list, e.g. wxl::Object::Is(uint32_t typeMask)."""
    params = param_tuple(item.get("signature") or "")
    return name if params is None else name + re.sub(r"\s+", " ", params)


def param_lines(params, returns, indent="    "):
    """The @param and @return rows as flat text, one per line."""
    out = []
    for p in params or []:
        label = " ".join(x for x in (p.get("type"), p.get("name")) if x)
        out.append(indent + label + ((" : " + p["meaning"]) if p.get("meaning") else ""))
    for r in returns or []:
        label = " ".join(x for x in (r.get("type"), r.get("name")) if x)
        out.append(indent + "-> " + label + ((" : " + r["meaning"]) if r.get("meaning") else ""))
    return out


def flatten(items, prefix=""):
    """Every item and nested member, depth-first, as (item, qualified display name)."""
    for it in items or []:
        name = it.get("qname") or ((prefix + "::" + it["name"]) if prefix else it["name"])
        yield it, name
        yield from flatten(it.get("children"), name)


def entry_lines(heading, it):
    """One item of llms-full.txt: heading, declaration, description, parameters, source."""
    out = [heading]
    if it.get("signature"):
        out.append(it["signature"])
    out.append(it["description"] or "(undocumented)")
    out += param_lines(it.get("params"), it.get("returns"))
    out.append("source: %s:%s" % (it["file"], it["line"]))
    out.append("")
    return out


def render_index(api):
    """llms.txt: one line per event, hook and symbol."""
    hook_total = sum(len(s["hooks"]) for s in api["scripts"])
    object_members = sum(1 for _ in flatten(api["objects"])) - len(api["objects"])
    binding_total = sum(1 for b in api["bindings"] for _ in flatten(b["items"]))

    index = ["# WarcraftXL API index", ""] + INTRO
    index += [
        "Sections: events (%d), overrides (%d class, %d virtuals), extension API (%d), object"
        % (len(api["events"]), len(api["scripts"]), hook_total, sum(1 for _ in flatten(api["framework"]["items"]))),
        "handles (%d classes, %d members), game bindings (%d headers, %d symbols)."
        % (len(api["objects"]), object_members, len(api["bindings"]), binding_total),
        "",
        "The full text of every entry below, with its parameters, is in llms-full.txt.",
        "",
        "## Events",
        "",
    ]
    for e in api["events"]:
        index.append("%-3d %-24s %-26s %s" % (e["id"], e["name"], e["args"], first_sentence(e["description"])))

    index += ["", "## Overrides", ""]
    for s in api["scripts"]:
        index.append("### wxl::" + s["name"])
        for h in s["hooks"]:
            index.append("    void %s%s" % (h["name"], h["params_tuple"]))
        index.append("")

    index += ["## Extension API", ""]
    for it, name in flatten(api["framework"]["items"]):
        index.append("%-56s %s" % (call_form(it, name), first_sentence(it["description"])))

    index += ["", "## Object handles", ""]
    for it, name in flatten(api["objects"]):
        index.append("%-56s %s" % (call_form(it, name), first_sentence(it["description"])))

    index += ["", "## Game bindings", ""]
    for b in api["bindings"]:
        index.append("### %s  (%s)" % (b["file"], b["namespace"] or "-"))
        for it, name in flatten(b["items"]):
            index.append("%-64s %s" % (call_form(it, name), first_sentence(it["description"])))
        index.append("")
    return "\n".join(index).rstrip() + "\n"


def _events_full(api):
    out = ["## Events", "",
           "An extension connects a handler with On<Tag>, or overrides the Extension virtual of",
           "the event's name, which gets the same args unpacked. Ids are the ABI and are never",
           "renumbered.", ""]
    for e in api["events"]:
        out.append("### %s (id %d)" % (e["name"], e["id"]))
        out.append(e["description"] or "(undocumented)")
        if e.get("argsSignature"):
            out.append("args: " + e["argsSignature"])
        # The args struct's description opens by naming the events it serves, which the entry has
        # already said; anything after that is its own and worth keeping.
        extra = re.sub(r"^Args of [^.]*\.\s*", "", e.get("argsDescription") or "")
        if extra:
            out.append(extra)
        out += param_lines(e.get("params"), None)
        out.append("source: %s:%s" % (e["file"], e["line"]))
        out.append("")
    return out


def _scripts_full(api):
    out = ["## Overrides", ""]
    for s in api["scripts"]:
        out.append("### wxl::%s" % s["name"])
        out.append(s.get("banner") or "")
        if s.get("description") and s["description"] != s.get("banner"):
            out.append(s["description"])
        out.append("")
        for h in s["hooks"]:
            out.append("#### %s::%s" % (s["name"], h["name"]))
            out.append("void %s%s override" % (h["name"], h["params_tuple"]))
            out.append(h["description"] or "(undocumented)")
            out += param_lines(h.get("params"), h.get("returns"))
            out.append("source: %s:%s" % (h["file"], h["line"]))
            out.append("")
    return out


def _group_full(title, blurb, items):
    out = ["## " + title, ""]
    if blurb:
        out += [blurb, ""]
    for it, name in flatten(items):
        out += entry_lines("### " + name + "   [" + it["kind"] + "]", it)
    return out


def _bindings_full(api):
    out = ["## Game bindings", "",
           "Typed calls into engine functions and typed readers of engine objects, one header per area.", ""]
    for b in api["bindings"]:
        out.append("### %s  (%s)" % (b["file"], b["namespace"] or "-"))
        out.append(b["description"] or "")
        out.append("")
        for it, name in flatten(b["items"]):
            out += entry_lines("#### " + name + "   [" + it["kind"] + "]", it)
    return out


def render_full(api):
    """llms-full.txt: every entry with its declaration, description and parameters."""
    full = ["# WarcraftXL API reference", ""] + INTRO
    full += _events_full(api)
    full += _scripts_full(api)
    full += _group_full("Extension API", api["framework"]["description"], api["framework"]["items"])
    full += _group_full("Object handles",
                        "Handles over raw client pointers: one pointer, no ownership, methods over "
                        "the bindings. The free functions they call stay available.", api["objects"])
    full += _bindings_full(api)
    return "\n".join(full).rstrip() + "\n"
