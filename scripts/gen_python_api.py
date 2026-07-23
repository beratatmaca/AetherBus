#!/usr/bin/env python3
"""Generate the Python control-API reference markdown from the client source.

The API surface is documented in one place — the docstrings and signatures of
``python/src/aetherbus/client.py`` — and this script renders it to markdown so
the in-app **Help -> Python API** viewer never drifts from the code. A narrative
preamble (install/enabling/protocol prose that doesn't live in code) is merged
in at a ``<!-- GENERATED_API_REFERENCE -->`` marker.

Usage:
    gen_python_api.py <client.py> <preamble.md.in> <output.md>

Parsing is done with ``ast`` (no import), so it runs in any build environment
with a stock Python 3 and never executes the client. Output is deterministic
(no timestamps) so it doesn't dirty incremental rebuilds.
"""

from __future__ import annotations

import ast
import sys

MARKER = "<!-- GENERATED_API_REFERENCE -->"


def _render_signature(func: ast.FunctionDef, *, drop_self: bool) -> str:
    """Reconstruct a readable call signature from a FunctionDef node."""
    a = func.args
    parts: list[str] = []

    posonly = list(a.posonlyargs)
    args = list(a.args)
    # Defaults align to the tail of posonly + args.
    pos = posonly + args
    defaults = list(a.defaults)
    default_for = {}
    if defaults:
        for arg, default in zip(pos[len(pos) - len(defaults):], defaults):
            default_for[arg.arg] = ast.unparse(default)

    if drop_self and pos and pos[0].arg == "self":
        pos = pos[1:]

    for arg in pos:
        if arg.arg in default_for:
            parts.append(f"{arg.arg}={default_for[arg.arg]}")
        else:
            parts.append(arg.arg)

    if a.vararg is not None:
        parts.append(f"*{a.vararg.arg}")
    elif a.kwonlyargs:
        parts.append("*")

    for arg, default in zip(a.kwonlyargs, a.kw_defaults):
        if default is not None:
            parts.append(f"{arg.arg}={ast.unparse(default)}")
        else:
            parts.append(arg.arg)

    if a.kwarg is not None:
        parts.append(f"**{a.kwarg.arg}")

    ret = f" -> {ast.unparse(func.returns)}" if func.returns is not None else ""
    return f"{func.name}({', '.join(parts)}){ret}"


def _entry(func: ast.FunctionDef, *, drop_self: bool) -> str:
    # Name as the heading (a scannable anchor); full signature on its own line as
    # inline code (wraps at spaces, so long signatures never clip); then the doc.
    sig = _render_signature(func, drop_self=drop_self)
    doc = ast.get_docstring(func) or "_(undocumented)_"
    return f"### {func.name}\n\n`{sig}`\n\n{doc.strip()}\n"


def generate_reference(client_src: str) -> str:
    tree = ast.parse(client_src)
    lines: list[str] = []

    # Module-level public functions (e.g. connect()).
    module_funcs = [
        n for n in tree.body
        if isinstance(n, ast.FunctionDef) and not n.name.startswith("_")
    ]
    # Lead with connect() (the entry point); keep the rest in source order.
    module_funcs.sort(key=lambda n: n.name != "connect")
    if module_funcs:
        lines.append("## Module functions\n")
        for func in module_funcs:
            lines.append(_entry(func, drop_self=False))

    # The Client class and its public methods.
    client = next(
        (n for n in tree.body if isinstance(n, ast.ClassDef) and n.name == "Client"),
        None,
    )
    if client is not None:
        lines.append("## Client methods\n")
        class_doc = ast.get_docstring(client)
        if class_doc:
            lines.append(class_doc.strip() + "\n")
        for node in client.body:
            if isinstance(node, ast.FunctionDef) and not node.name.startswith("_"):
                lines.append(_entry(node, drop_self=True))

    return "\n".join(lines).strip() + "\n"


def main(argv: list[str]) -> int:
    if len(argv) != 4:
        sys.stderr.write("usage: gen_python_api.py <client.py> <preamble.md.in> <output.md>\n")
        return 2
    client_path, preamble_path, out_path = argv[1], argv[2], argv[3]

    with open(client_path, "r", encoding="utf-8") as f:
        reference = generate_reference(f.read())
    with open(preamble_path, "r", encoding="utf-8") as f:
        preamble = f.read()

    if MARKER in preamble:
        content = preamble.replace(MARKER, reference)
    else:
        content = preamble.rstrip() + "\n\n" + reference

    with open(out_path, "w", encoding="utf-8") as f:
        f.write(content)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
