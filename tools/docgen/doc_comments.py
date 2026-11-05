# SPDX-License-Identifier: GPL-3.0-or-later
# Doc comments as data: a description, @param rows and @return rows, in Eluna or Doxygen style.

import re

from signatures import SINGLE_COLON

QUALIFIERS = {"const", "unsigned", "signed", "volatile", "struct", "class", "enum"}

EMPTY_DOC = {"description": "", "params": [], "returns": []}


def doc_lines(kind, body):
    """The text lines of a doc token, without the comment markers."""
    if kind == "doc3":
        return [body[1:] if body.startswith(" ") else body]
    body = body[3:]
    if body.endswith("*/"):
        body = body[:-2]
    out = []
    for raw in body.split("\n"):
        s = raw.strip()
        if s.startswith("*"):
            s = s[1:]
            if s.startswith(" "):
                s = s[1:]
        out.append(s.rstrip())
    while out and not out[0].strip():
        out.pop(0)
    while out and not out[-1].strip():
        out.pop()
    return out


def split_typed(rest):
    """'TYPE NAME : meaning' -> (type, name, meaning); the colon split is on the first single colon."""
    m = SINGLE_COLON.search(rest)
    head, meaning = (rest[:m.start()], rest[m.end():].strip()) if m else (rest, None)
    toks = head.split()
    typ = []
    while toks and toks[0] in QUALIFIERS and len(toks) > 1:
        typ.append(toks.pop(0))
    if toks:
        typ.append(toks.pop(0))
    return (" ".join(typ) or None), (" ".join(toks) or None), (meaning or None)


def _param_row(rest, doxygen):
    """An @param row: Doxygen names the parameter only, Eluna gives its type as well."""
    if doxygen:
        pm = re.match(r"(\S+)\s*(.*)$", rest)
        name, meaning = (pm.group(1), pm.group(2).strip()) if pm else (rest, "")
        return {"type": None, "name": name, "meaning": meaning or None}
    typ, name, meaning = split_typed(rest)
    if name is None:
        typ, name = None, typ
    return {"type": typ, "name": name, "meaning": meaning}


def _return_row(rest, doxygen):
    """An @return row; an Eluna row without a colon whose 'name' is several words is all meaning."""
    if doxygen:
        return {"type": None, "name": None, "meaning": rest or None}
    typ, name, meaning = split_typed(rest)
    if meaning is None and name and len(name.split()) > 1:
        name, meaning = None, name
    return {"type": typ, "name": name, "meaning": meaning}


def parse_doc(lines):
    """Normalizes an Eluna or a Doxygen block into {description, params, returns, style}."""
    doxygen = "@brief" in "\n".join(lines)
    paras, cur = [], []
    params, returns = [], []
    target = None  # the param/return row a continuation line extends

    def end_para():
        if cur:
            paras.append(" ".join(cur))
            cur.clear()

    for ln in lines:
        s = ln.strip()
        if not s:
            target = None
            end_para()
            continue
        m = re.match(r"@(\w+)\s*(.*)$", s)
        tag, rest = (m.group(1), m.group(2).strip()) if m else (None, None)
        if tag == "brief":
            target = None
            cur.append(rest)
        elif tag == "param":
            end_para()
            target = _param_row(rest, doxygen)
            params.append(target)
        elif tag in ("return", "returns"):
            end_para()
            target = _return_row(rest, doxygen)
            returns.append(target)
        elif target is not None:
            target["meaning"] = ((target["meaning"] or "") + " " + s).strip()
        else:
            cur.append(s)
    end_para()
    return {"description": "\n\n".join(paras), "params": params, "returns": returns,
            "style": "doxygen" if doxygen else "eluna"}
