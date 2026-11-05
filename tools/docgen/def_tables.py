# SPDX-License-Identifier: GPL-3.0-or-later
# The rows of a .def table (WXL_EVENT(...), WXL_SCRIPT_HOOK(...)) with the doc comment above each.

import re

from cpp_lexer import ParseError, lex
from doc_comments import doc_lines, parse_doc
from signatures import split_args


def _call_end(body, open_paren, where):
    """The index of the ')' closing the call whose '(' is at body[open_paren]."""
    depth = 0
    for k in range(open_paren, len(body)):
        if body[k] == "(":
            depth += 1
        elif body[k] == ")":
            depth -= 1
            if depth == 0:
                return k
    raise ParseError("%s: a table row's '(' is never closed" % where)


def parse_table(path, macro, rel):
    """Every MACRO(...) row of a .def table with the doc block above it: ([(doc, call text, line)], text).
    A row with no doc comment right above it has doc None; `rel` names the table in errors."""
    text = path.read_text(encoding="utf-8", errors="replace")
    rows = []
    pending = None
    call_re = re.compile(r"\b" + macro + r"\s*\(")
    for kind, body, line in lex(text):
        if kind in ("doc", "doc3"):
            pending = parse_doc(doc_lines(kind, body))
            continue
        if kind in ("comment", "pp"):
            pending = None
            continue
        pos = 0
        while True:
            m = call_re.search(body, pos)
            if not m:
                if body[pos:].strip():
                    pending = None
                break
            if body[pos:m.start()].strip():
                pending = None
            row_line = line + body.count("\n", 0, m.start())
            k = _call_end(body, m.end() - 1, "%s:%d" % (rel, row_line))
            rows.append((pending, body[m.start():k + 1], row_line))
            pending = None
            pos = k + 1
    return rows, text


def row_args(call, count, where):
    """The `count` arguments of a table row; a row with another count is a parse error at `where`."""
    args = split_args(call[call.index("(") + 1:-1])
    if len(args) != count:
        raise ParseError("%s: %s takes %d arguments" % (where, call.split("(")[0].strip(), count))
    return args
