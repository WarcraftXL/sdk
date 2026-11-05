# SPDX-License-Identifier: GPL-3.0-or-later
# Declarations as text: what a signature declares, its parameters, and the parts that are not its name.

import re

# A single colon, not half of a "::".
SINGLE_COLON = re.compile(r"(?<!:):(?!:)")


def tidy(s):
    """Whitespace runs as one space, ends trimmed."""
    return re.sub(r"\s+", " ", s).strip()


def split_args(inner):
    """The comma-separated parts of an argument list, split outside brackets and trimmed."""
    parts, depth, buf = [], 0, ""
    for ch in inner:
        if ch in "(<[{":
            depth += 1
        elif ch in ")>]}":
            depth -= 1
        if ch == "," and depth == 0:
            parts.append(buf.strip())
            buf = ""
        else:
            buf += ch
    parts.append(buf.strip())
    return parts


def _skip_balanced(s, start, open_ch, close_ch):
    """The index just past the bracket group opening at or after start, or None when it never closes."""
    depth = 0
    for k in range(start, len(s)):
        if s[k] == open_ch:
            depth += 1
        elif s[k] == close_ch:
            depth -= 1
            if depth == 0:
                return k + 1
    return None


def strip_template(s):
    """Drops a leading template <...> prefix (balanced angle brackets)."""
    s = s.strip()
    while s.startswith("template"):
        j = s.find("<")
        if j < 0:
            break
        end = _skip_balanced(s, j, "<", ">")
        if end is None:
            break
        s = s[end:].strip()
    return s


# The keyword of an alignas(...) specifier, not the tail of a longer name.
ALIGNAS = re.compile(r"(?<!\w)alignas\s*(?=\()")


def drop_attribute_specifiers(s):
    """Drops every [[attr]] and alignas(...), wherever they stand: before a declaration, between a
    class-key and its name, before a member."""
    out, i = [], 0
    while i < len(s):
        end = None
        if s.startswith("[[", i):
            end = _skip_balanced(s, i, "[", "]")
        else:
            m = ALIGNAS.match(s, i)
            if m:
                end = _skip_balanced(s, m.end(), "(", ")")
        if end is None:
            out.append(s[i])
            i += 1
        else:
            out.append(" ")
            i = end
    return "".join(out)


def strip_attributes(s):
    """Drops attribute specifiers ([[attr]], alignas(...)) and leading ALL_CAPS(...) attribute macros
    such as WXL_SETTINGS(...)."""
    s = drop_attribute_specifiers(s)
    while True:
        s = s.strip()
        m = re.match(r"[A-Z][A-Z0-9_]+\s*\(", s)
        if not m:
            return s
        end = _skip_balanced(s, m.end() - 1, "(", ")")
        if end is None:
            return s
        s = s[end:]


def strip_decltype(s):
    """Drops a leading decltype(...) return type, whose parens are not a parameter list."""
    s = s.strip()
    while s.startswith("decltype("):
        end = _skip_balanced(s, len("decltype"), "(", ")")
        if end is None:
            return s
        s = s[end:].strip()
    return s


def _function_name(core, p):
    """The name of a function whose parameter list opens at core[p]."""
    head = core[:p].rstrip()
    # operator() and operator[] carry their own brackets, so the first '(' is part of the name.
    if head.endswith("operator"):
        for sym in ("()", "[]"):
            if core[p:].startswith(sym):
                return "operator" + sym
    m = re.search(r"(operator\s*(?:[\w:]+(?:\s*[*&])*|[^\w\s(]+))$", head)
    if m:
        return re.sub(r"\s+", " ", m.group(1))
    m = re.search(r"(~?\w+)$", head)
    return m.group(1) if m else head


def class_head(core):
    """(key, name, end) of a class, struct or union head, the name with a specialization's <args>
    (Result<void>), end the index past it; None when core opens no such head."""
    m = re.match(r"(class|struct|union)\s+(\w+)\s*", core)
    if not m:
        return None
    name, end = m.group(2), m.end()
    if core.startswith("<", end):
        close = _skip_balanced(core, end, "<", ">")
        if close is not None:
            name, end = name + tidy(core[end:close]), close
    return m.group(1), name, end


def classify(sig):
    """(kind, name) of a declaration: namespace, class, struct, enum, alias, function, field or friend."""
    core = strip_decltype(strip_attributes(strip_template(sig)))
    if core.startswith("friend"):
        return "friend", None
    m = re.match(r"namespace\s+([\w:]+)", core)
    if m:
        return "namespace", m.group(1)
    head = class_head(core)
    if head and "(" not in core[head[2]:].split(":")[0]:
        return ("class" if head[0] == "class" else "struct"), head[1]
    m = re.match(r"enum(?:\s+(?:class|struct))?\s+(\w+)", core)
    if m:
        return "enum", m.group(1)
    m = re.match(r"using\s+(\w+)", core)
    if m:
        return "alias", m.group(1)
    m = re.match(r"typedef\b.*?(\w+)\s*;?$", core)
    if m:
        return "alias", m.group(1)
    p = core.find("(")
    # A '(' before any '=' opens a parameter list; one after it is part of an initializer.
    if p >= 0 and "=" not in core[:p].replace("operator=", "").replace("==", "").replace("!=", ""):
        return "function", _function_name(core, p)
    m = re.match(r"([^=\[{;]*?)(\w+)\s*(?:\[[^\]]*\]\s*)?(?:=|\{|;|$)", core)
    return "field", (m.group(2) if m else core)


def signature_params(sig):
    """{name: type} from the outermost parameter list of a function signature."""
    core = strip_attributes(strip_template(sig))
    p = core.find("(")
    if p < 0:
        return {}
    depth, end = 0, None
    for k in range(p, len(core)):
        if core[k] in "(<[":
            depth += 1
        elif core[k] in ")>]":
            depth -= 1
            if depth == 0 and core[k] == ")":
                end = k
                break
    if end is None:
        return {}
    out = {}
    for part in split_args(core[p + 1:end]):
        part = part.split("=")[0].strip()
        m = re.match(r"^(.*?)(\w+)\s*(\[[^\]]*\])?\s*$", part)
        if m and m.group(1).strip():
            out[m.group(2)] = (m.group(1).strip() + (m.group(3) or "")).strip()
    return out


def drop_init_list(sig):
    """Cuts a constructor's member-initializer list: 'X(void* p) : p_(p)' -> 'X(void* p)'."""
    p = sig.find("(")
    if p < 0:
        return sig
    end = _skip_balanced(sig, p, "(", ")")
    if end is None:
        return sig
    m = SINGLE_COLON.search(sig, end)
    return sig[:m.start()].rstrip() if m else sig


def param_tuple(sig):
    """The parameter list closing a signature, found by walking back from the last ')' before any
    trailing requires-clause: taking the first '(' instead would pick up a decltype(...) return type.
    None when there is none."""
    end = drop_requires_clause(sig).rfind(")")
    if end < 0:
        return None
    depth = 0
    for i in range(end, -1, -1):
        if sig[i] == ")":
            depth += 1
        elif sig[i] == "(":
            depth -= 1
            if depth == 0:
                return sig[i:end + 1]
    return None


def drop_requires_clause(sig):
    """Cuts a requires-clause that follows a parameter list and its qualifiers."""
    for m in re.finditer(r"\brequires\b", sig):
        head = sig[:m.start()]
        if re.search(r"\)\s*(?:(?:const|volatile|noexcept|&&|&)\s*)*$", head):
            return head.rstrip()
    return sig
