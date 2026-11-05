# SPDX-License-Identifier: GPL-3.0-or-later
# A public header's doc-commented declarations, nested under their class or namespace.

import re

from cpp_lexer import ParseError, lex
from doc_comments import doc_lines, parse_doc
from signatures import (class_head, classify, drop_init_list, signature_params, strip_attributes, strip_template,
                        tidy)

ACCESS_LABELS = ("public:", "private:", "protected:")
NESTING_KINDS = ("namespace", "class", "struct")


class Scope:
    """A brace block being parsed: the file, a namespace, a class or struct, an enum or any other block."""

    def __init__(self, kind, name, parent, item=None, visible=True, documented=False):
        self.kind = kind
        self.name = name
        self.parent = parent
        self.item = item
        self.visible = visible
        self.documented = documented
        self.access = "private" if kind == "class" else "public"
        self.container = item["children"] if item is not None else (parent.container if parent else None)
        self.qual = (parent.qual if parent else []) + ([name] if name and kind in NESTING_KINDS else [])
        self.opened_item = None  # the item whose declaration opened this block, if one was emitted

    def emittable(self):
        """True when a declaration in this block belongs in the reference."""
        if not self.visible:
            return False
        if self.kind in ("file", "namespace"):
            return True
        return self.kind in ("class", "struct") and self.access == "public"

    def ancestor_documented(self):
        s = self
        while s:
            if s.documented:
                return True
            s = s.parent
        return False


def block_kind(core):
    """(kind, name) of the block a statement opens: namespace, class, struct, enum or block."""
    m = re.match(r"(?:inline\s+)?namespace\b\s*([\w:]*)", core)
    if m:
        return "namespace", m.group(1) or None
    head = class_head(core)
    if head and "(" not in core[head[2]:]:
        return ("class" if head[0] == "class" else "struct"), head[1]
    if re.match(r"enum\b", core):
        return "enum", None
    return "block", None


def banner(text, paragraph):
    """The file's own opening // prose, before the license; the first line or the first paragraph."""
    out = []
    for ln in text.split("\n"):
        s = ln.strip()
        if not s.startswith("//") or s.startswith("// Copyright"):
            break
        s = s[2:].strip()
        if not s:
            if paragraph and out:
                break
            continue
        out.append(s)
        if not paragraph:
            break
    return " ".join(out)


class HeaderParser:
    """Walks a header's tokens: a doc comment is pending until the declaration after it ends with
    ';' or opens a '{', and that declaration becomes an item of the enclosing block."""

    def __init__(self, path, root):
        self.text = path.read_text(encoding="utf-8", errors="replace")
        self.rel = path.relative_to(root).as_posix()
        self.result = {"file": self.rel, "namespaces": [], "description": "", "banner": banner(self.text, True),
                       "items": []}
        self.root = Scope("file", None, None)
        self.root.container = self.result["items"]
        self.scope = self.root
        self.pending = None       # (doc lines, line, token kind) of the doc comment awaiting its subject
        self.stmt = []            # code characters since the last ; { }
        self.sig_start = None     # index into stmt where the pending subject begins
        self.last_doc3_line = -10
        self.paren_depth = 0      # parentheses open in stmt
        self.quote = None         # the quote of the literal being read, if any
        self.escaped = False      # the previous literal character was a backslash

    def parse(self):
        for kind, body, line in lex(self.text):
            if kind in ("doc", "doc3"):
                self.on_doc(kind, body, line)
            elif kind == "comment":
                if self.pending is not None and self.subject_empty():
                    self.pending = None
            elif kind == "pp":
                self.on_preprocessor(body)
            else:
                self.on_code(body, line)
        if self.scope is not self.root:
            raise ParseError("%s: the %s block '%s' is never closed"
                             % (self.rel, self.scope.kind, self.scope.name or ""))
        one_liners(self.result["items"], self.text.split("\n"))
        return self.result

    def subject_empty(self):
        return not "".join(self.stmt[self.sig_start:]).strip()

    def make_item(self, kind, name, sig, doc, line):
        if kind == "function":
            sig = drop_init_list(sig)
        item = {"kind": kind, "name": name, "qname": "::".join(self.scope.qual + [name]) if name else None,
                "signature": sig, "description": doc["description"], "params": doc["params"],
                "returns": doc["returns"], "file": self.rel, "line": line}
        if kind == "function" and doc["style"] == "doxygen":
            # Doxygen @param rows name the parameter only: take its type from the signature.
            types = signature_params(sig)
            for p in item["params"]:
                if p["type"] is None and p["name"] in types:
                    p["type"] = types[p["name"]]
        if kind in NESTING_KINDS:
            item["children"] = []
        return item

    def on_doc(self, kind, body, line):
        lines = doc_lines(kind, body)
        # Consecutive /// lines with nothing between them are one doc comment.
        if kind == "doc3" and self.pending is not None and self.pending[2] == "doc3" \
                and line == self.last_doc3_line + 1 and self.sig_start is not None and self.subject_empty():
            self.pending = (self.pending[0] + lines, self.pending[1], "doc3")
        else:
            self.pending = (lines, line, kind)
        if kind == "doc3":
            self.last_doc3_line = line
        self.sig_start = len(self.stmt)

    def on_preprocessor(self, body):
        if self.pending is None:
            return
        m = re.match(r"#\s*define\s+(\w+)(\([^)]*\))?", body)
        if m and self.scope.emittable():
            doc = parse_doc(self.pending[0])
            item = self.make_item("macro", m.group(1), tidy(m.group(0)), doc, self.pending[1])
            item["qname"] = m.group(1)
            self.scope.container.append(item)
        self.pending = None

    def on_code(self, body, line):
        for ch in body:
            # Braces and semicolons inside a literal or inside parentheses, such as a {} default
            # argument, belong to the statement.
            if self.in_literal(ch):
                self.stmt.append(ch)
            elif self.paren_depth or ch not in "{};":
                self.add_char(ch)
            elif ch == "{":
                self.open_block(line)
            elif ch == "}":
                self.close_block(line)
            else:
                self.end_statement()
            if ch == "\n":
                line += 1
        if self.pending is not None and self.sig_start is None:
            self.sig_start = 0

    def declare_block(self, line):
        """Emits the pending doc's subject, a declaration that opens a block; returns the item, or None,
        and whether the block counts as documented. A namespace's doc describes the file."""
        sig = tidy("".join(self.stmt[self.sig_start:]))
        doc = parse_doc(self.pending[0])
        k, nm = classify(sig)
        item, documented = None, False
        scope = self.scope
        if k == "namespace" and not scope.ancestor_documented() and scope.kind in ("file", "namespace"):
            documented = True
            if doc["description"]:
                self.result["description"] = (self.result["description"] + "\n\n" + doc["description"]).strip()
        elif k != "friend" and scope.emittable() and not (k == "namespace" and nm == "detail"):
            item = self.make_item(k, nm, sig, doc, self.pending[1])
            item["_open_line"] = line
            scope.container.append(item)
            documented = k == "namespace"
        self.pending = None
        return item, documented

    def open_block(self, line):
        core = strip_attributes(strip_template(tidy("".join(self.stmt))))
        kind, name = block_kind(core)
        item, documented = self.declare_block(line) if self.pending is not None else (None, False)
        scope = self.scope
        visible = scope.emittable() and kind in NESTING_KINDS and name != "detail"
        child_item = item if (item is not None and kind in NESTING_KINDS) else None
        new = Scope(kind, name, scope, child_item, visible=visible, documented=documented)
        if kind == "namespace" and name and scope.kind in ("file", "namespace") \
                and name not in self.result["namespaces"] and name.startswith("wxl"):
            self.result["namespaces"].append(name)
        new.opened_item = item
        self.scope = new
        self.stmt = []
        self.sig_start = None

    def close_block(self, line):
        if self.pending is not None and self.sig_start is not None and self.subject_empty():
            self.pending = None
        if self.scope.parent is None:
            raise ParseError("%s:%d: a '}' closes no block" % (self.rel, line))
        it = self.scope.opened_item
        if it is not None and it.get("_open_line") is not None:
            it["_close_line"] = line
        self.scope = self.scope.parent
        self.stmt = []
        self.sig_start = None

    def end_statement(self):
        if self.pending is not None:
            sig = tidy("".join(self.stmt[self.sig_start:]))
            doc = parse_doc(self.pending[0])
            k, nm = classify(sig)
            if sig and k != "friend" and self.scope.emittable():
                self.scope.container.append(self.make_item(k, nm, sig, doc, self.pending[1]))
            self.pending = None
        self.stmt = []
        self.sig_start = None

    def in_literal(self, ch):
        """True when ch belongs to a string or character literal, its quotes included."""
        if self.quote is None:
            if ch not in "\"'":
                return False
            self.quote = ch
        elif self.escaped:
            self.escaped = False
        elif ch == "\\":
            self.escaped = True
        elif ch == self.quote or ch == "\n":
            self.quote = None
        return True

    def add_char(self, ch):
        self.stmt.append(ch)
        if ch == "(":
            self.paren_depth += 1
        elif ch == ")":
            self.paren_depth = max(0, self.paren_depth - 1)
        if ch == ":":
            s = "".join(self.stmt).strip()
            if s in ACCESS_LABELS:
                self.scope.access = s[:-1]
                self.stmt = []
                self.sig_start = 0 if self.pending is not None else None


def one_liners(items, src):
    """A class, struct, enum or namespace defined on one line shows its whole definition; a short
    enum or namespace (12 lines or fewer, no preprocessor line) shows its body."""
    for it in items:
        ol, cl = it.pop("_open_line", None), it.pop("_close_line", None)
        if ol is not None and ol == cl and it["kind"] in ("class", "struct", "enum", "namespace"):
            full = tidy(src[ol - 1].split("//")[0])
            sig = it["signature"]
            if full.startswith(sig):
                it["signature"] = full
            elif sig.endswith(full.split("{")[0].strip()):
                it["signature"] = sig + full[len(full.split("{")[0].rstrip()):]
        elif ol is not None and cl is not None and it["kind"] in ("enum", "namespace") and cl - ol <= 12:
            body = [ln.split("//")[0].rstrip() for ln in src[ol - 1:cl]]
            if not any(ln.lstrip().startswith("#") for ln in body):
                indent = min(len(ln) - len(ln.lstrip()) for ln in body if ln.strip())
                text = "\n".join(ln[indent:] for ln in body if ln.strip())
                head = it["signature"]
                it["signature"] = text if text.startswith(head) else head + "\n" + text
        if it.get("children"):
            one_liners(it["children"], src)


def parse_header(path, root):
    """Every doc-commented public declaration of a header: {file, namespaces, description, banner,
    items}; `root` is the folder file paths are relative to."""
    return HeaderParser(path, root).parse()
