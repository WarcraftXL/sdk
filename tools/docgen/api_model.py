# SPDX-License-Identifier: GPL-3.0-or-later
# The API model the site and the text files are built from: events, overrides, extension API, objects, bindings.

from def_tables import parse_table, row_args
from doc_comments import EMPTY_DOC
from headers import banner, parse_header
from signatures import tidy

OBJECT_BASE_FILES = ["Object.hpp", "Unit.hpp", "Player.hpp"]  # the base chain, documented in order
# The extension-authoring surface, one top-level header each.
PLUMBING_FILES = ["Extension.hpp", "Export.hpp", "Listener.hpp", "Log.hpp", "Core.hpp", "Hook.hpp", "Intercept.hpp",
                  "Patch.hpp", "Signal.hpp", "Service.hpp", "Storage.hpp", "Profiles.hpp", "AllSettings.hpp",
                  "Settings.hpp", "Result.hpp"]

FRAMEWORK_DESCRIPTION = ("What an extension is built out of: the Extension class and Export, On and its handlers, "
                         "the log, a typed hook, interceptors, byte patches, signals, the services and the "
                         "core's own, the per-extension settings and the Result its calls answer.")


def build_events(root, include):
    """The rows of events/Events.def, each with its args struct from Events.hpp, and the other
    declarations of Events.hpp."""
    path = include / "events" / "Events.def"
    rel = path.relative_to(root).as_posix()
    rows, _ = parse_table(path, "WXL_EVENT", rel)
    header = parse_header(include / "Events.hpp", root)
    structs = {it["name"]: it for it in header["items"] if it["kind"] == "struct"}
    events, used = [], set()
    for doc, call, line in rows:
        name, eid, args = row_args(call, 3, "%s:%d" % (rel, line))
        doc = doc or EMPTY_DOC
        st = structs.get(args)
        used.add(args)
        events.append({"name": name, "id": int(eid, 0), "args": args, "description": doc["description"],
                       "params": st["params"] if st else [], "argsDescription": st["description"] if st else "",
                       "argsSignature": st["signature"] if st else None,
                       "call": tidy(call), "file": rel, "line": line})
    others = [it for it in header["items"] if not (it["kind"] == "struct" and it["name"] in used)]
    return events, others


def build_overrides(root, include, framework):
    """The Extension class with one virtual per row of Overrides.def: OnTag, the On<Tag> event."""
    classes = {it["name"]: it for it in framework["items"] if it["kind"] == "class"}
    path = include / "Overrides.def"
    rel = path.relative_to(root).as_posix()
    rows, text = parse_table(path, "WXL_OVERRIDE", rel)
    hooks = []
    for doc, call, line in rows:
        tag, params, _ = row_args(call, 3, "%s:%d" % (rel, line))
        doc = doc or EMPTY_DOC
        hooks.append({"name": "On" + tag, "tag": tag, "params_tuple": tidy(params),
                      "description": doc["description"], "params": doc["params"],
                      "returns": doc["returns"], "call": tidy(call), "file": rel, "line": line})
    cls = classes.get("Extension")
    return [{"name": "Extension", "file": rel, "banner": banner(text, True),
             "description": cls["description"] if cls else "",
             "signature": cls["signature"] if cls else None, "hooks": hooks}]


def build_objects(root, include):
    """The classes and structs of include/wxl/objects/, the base chain first."""
    names = [p.name for p in sorted((include / "objects").glob("*.hpp"))]
    ordered = OBJECT_BASE_FILES + [n for n in names if n not in OBJECT_BASE_FILES]
    objects = []
    for f in ordered:
        h = parse_header(include / "objects" / f, root)
        objects.extend(it for it in h["items"] if it["kind"] in ("class", "struct"))
    return objects


def build_bindings(root, include):
    """One entry per header of include/wxl/game/."""
    bindings = []
    for p in sorted((include / "game").glob("*.hpp")):
        h = parse_header(p, root)
        ns = ", ".join(h["namespaces"])
        bindings.append({"file": h["file"], "name": p.name, "namespace": ns,
                         "label": p.stem + (" (" + ns + ")" if ns else ""),
                         "description": h["description"] or h["banner"], "items": h["items"]})
    return bindings


def build_api(root):
    """The whole model, as data.js holds it, from the headers under `root` (the SDK's checkout)."""
    include = root / "include" / "wxl"
    events, event_types = build_events(root, include)
    scripts = build_overrides(root, include, parse_header(include / "Extension.hpp", root))
    framework_items = []
    for f in PLUMBING_FILES:
        framework_items.extend(parse_header(include / f, root)["items"])
    return {"events": events, "eventTypes": event_types, "scripts": scripts,
            "framework": {"description": FRAMEWORK_DESCRIPTION, "items": framework_items},
            "objects": build_objects(root, include), "bindings": build_bindings(root, include)}


def count_items(items):
    """Items and their members, at every depth."""
    return sum(1 + count_items(it.get("children", [])) for it in items)
