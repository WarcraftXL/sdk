# SPDX-License-Identifier: GPL-3.0-or-later
# Splits a C++ source into doc comments, plain comments, preprocessor lines and code.


class ParseError(Exception):
    """A header or table the reference cannot be built from; the message names the file and line."""


def lex(text):
    """The tokens of a C++ source, as (kind, text, line): kind is doc (/** */), doc3 (///),
    comment, pp (a whole preprocessor line, continuations included) or code."""
    toks = []
    i, n, line = 0, len(text), 1
    code, code_line = [], 1
    at_line_start = True

    def flush():
        nonlocal code
        if code:
            toks.append(("code", "".join(code), code_line))
            code = []

    while i < n:
        c = text[i]
        if c == "\n":
            if not code:
                code_line = line
            code.append(c)
            line += 1
            i += 1
            at_line_start = True
            continue
        if at_line_start and c in " \t":
            if not code:
                code_line = line
            code.append(c)
            i += 1
            continue
        if at_line_start and c == "#":
            flush()
            start, start_line = i, line
            while i < n:
                if text[i] == "\n":
                    if text[i - 1] == "\\" or (text[i - 1] == "\r" and text[i - 2] == "\\"):
                        line += 1
                        i += 1
                        continue
                    break
                i += 1
            toks.append(("pp", text[start:i], start_line))
            continue
        at_line_start = False
        if text.startswith("//", i):
            flush()
            end = text.find("\n", i)
            end = n if end < 0 else end
            body = text[i:end]
            if body.startswith("///") and not body.startswith("////") and not body.startswith("///<"):
                toks.append(("doc3", body[3:], line))
            else:
                toks.append(("comment", body, line))
            i = end
            continue
        if text.startswith("/*", i):
            flush()
            end = text.find("*/", i + 2)
            end = n if end < 0 else end + 2
            body = text[i:end]
            is_doc = body.startswith("/**") and not body.startswith("/**/") and not body.startswith("/**<")
            toks.append(("doc" if is_doc else "comment", body, line))
            line += body.count("\n")
            i = end
            continue
        if c in "\"'":
            # A string or character literal is code, kept whole so a // or { inside it is not read.
            if not code:
                code_line = line
            j = i + 1
            while j < n and text[j] != c and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            code.append(text[i:j + 1])
            i = j + 1
            continue
        if not code:
            code_line = line
        code.append(c)
        i += 1
    flush()
    return toks
