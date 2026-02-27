#!/usr/bin/env python3
"""Generate library interconnection schematics for libtpa.

Outputs Graphviz DOT files that can be opened by common viewer software.
"""

from __future__ import annotations

import re
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterable, List, Set

REPO_ROOT = Path(__file__).resolve().parents[2]
SRC_ROOTS = [REPO_ROOT / "src"]
HEADER_ROOTS = [REPO_ROOT / "include"]
OUTPUT_DIR = REPO_ROOT / "doc" / "schematics"

CONTROL_WORDS = {"if", "for", "while", "switch", "return", "sizeof"}
CALL_RE = re.compile(r"\b([A-Za-z_]\w*)\s*\(")


@dataclass
class FunctionInfo:
    name: str
    file: Path
    body: str


@dataclass
class StructInfo:
    name: str
    file: Path
    body: str


def iter_files(roots: Iterable[Path], suffixes: Set[str]) -> Iterable[Path]:
    for root in roots:
        for path in root.rglob("*"):
            if path.suffix in suffixes:
                yield path


def extract_structs(text: str, path: Path) -> List[StructInfo]:
    structs: List[StructInfo] = []

    typedef_re = re.compile(r"typedef\s+struct(?:\s+(\w+))?\s*\{", re.M)
    named_re = re.compile(r"\bstruct\s+(\w+)\s*\{", re.M)

    for match in typedef_re.finditer(text):
        start = match.end() - 1
        end = find_matching_brace(text, start)
        if end == -1:
            continue
        tail = text[end + 1 : end + 200]
        alias_match = re.match(r"\s*([A-Za-z_]\w*)\s*;", tail)
        if not alias_match:
            continue
        name = alias_match.group(1)
        body = text[start + 1 : end]
        structs.append(StructInfo(name=name, file=path, body=body))

    for match in named_re.finditer(text):
        name = match.group(1)
        start = match.end() - 1
        end = find_matching_brace(text, start)
        if end == -1:
            continue
        body = text[start + 1 : end]
        structs.append(StructInfo(name=name, file=path, body=body))

    uniq: Dict[str, StructInfo] = {}
    for s in structs:
        uniq.setdefault(s.name, s)
    return list(uniq.values())


def find_matching_brace(text: str, open_index: int) -> int:
    depth = 0
    for idx in range(open_index, len(text)):
        ch = text[idx]
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                return idx
    return -1


def extract_functions(text: str, path: Path) -> List[FunctionInfo]:
    functions: List[FunctionInfo] = []
    idx = 0
    while True:
        brace_index = text.find("{", idx)
        if brace_index == -1:
            break
        prefix = text[max(0, brace_index - 400) : brace_index]
        paren_close = prefix.rfind(")")
        if paren_close == -1:
            idx = brace_index + 1
            continue
        line_start = prefix.rfind("\n", 0, paren_close)
        sig = prefix[line_start + 1 :].strip()

        if not sig or ";" in sig or "=" in sig:
            idx = brace_index + 1
            continue
        if any(sig.startswith(word + " ") for word in CONTROL_WORDS):
            idx = brace_index + 1
            continue

        name_match = re.search(r"([A-Za-z_]\w*)\s*\([^()]*\)\s*$", sig)
        if not name_match:
            idx = brace_index + 1
            continue

        name = name_match.group(1)
        if not any(ch.islower() for ch in name):
            idx = brace_index + 1
            continue
        end = find_matching_brace(text, brace_index)
        if end == -1:
            idx = brace_index + 1
            continue

        body = text[brace_index + 1 : end]
        functions.append(FunctionInfo(name=name, file=path, body=body))
        idx = end + 1

    return functions


def write_dot(
    output: Path,
    graph_name: str,
    nodes: Iterable[str],
    edges: Iterable[tuple[str, str]],
    node_shape: str,
) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("w", encoding="utf-8") as f:
        f.write(f"digraph {graph_name} {{\n")
        f.write("  rankdir=LR;\n")
        f.write(f"  node [shape={node_shape}, fontsize=10];\n")
        for n in sorted(set(nodes)):
            f.write(f'  "{n}";\n')
        for src, dst in sorted(set(edges)):
            f.write(f'  "{src}" -> "{dst}";\n')
        f.write("}\n")


def main() -> int:
    header_files = list(iter_files(HEADER_ROOTS, {".h"}))
    source_files = list(iter_files(SRC_ROOTS, {".c"}))

    structs: Dict[str, StructInfo] = {}
    for file in header_files + source_files:
        text = file.read_text(encoding="utf-8", errors="ignore")
        for s in extract_structs(text, file.relative_to(REPO_ROOT)):
            structs.setdefault(s.name, s)

    functions: Dict[str, FunctionInfo] = {}
    for file in source_files:
        text = file.read_text(encoding="utf-8", errors="ignore")
        for fn in extract_functions(text, file.relative_to(REPO_ROOT)):
            functions.setdefault(fn.name, fn)

    fn_names = set(functions)
    struct_names = set(structs)

    call_edges: Set[tuple[str, str]] = set()
    fn_struct_edges: Set[tuple[str, str]] = set()

    for fn in functions.values():
        for call in CALL_RE.findall(fn.body):
            if call in fn_names and call not in CONTROL_WORDS and call != fn.name:
                call_edges.add((fn.name, call))
        for name in struct_names:
            if re.search(rf"\b{name}\b", fn.body) or re.search(rf"\bstruct\s+{name}\b", fn.body):
                fn_struct_edges.add((fn.name, name))

    struct_edges: Set[tuple[str, str]] = set()
    for s in structs.values():
        for other in struct_names:
            if other == s.name:
                continue
            if re.search(rf"\b{other}\b", s.body) or re.search(rf"\bstruct\s+{other}\b", s.body):
                struct_edges.add((s.name, other))

    write_dot(
        OUTPUT_DIR / "function_call_graph.dot",
        "function_call_graph",
        fn_names,
        call_edges,
        "box",
    )
    write_dot(
        OUTPUT_DIR / "struct_relationship_graph.dot",
        "struct_relationship_graph",
        struct_names,
        struct_edges,
        "ellipse",
    )
    write_dot(
        OUTPUT_DIR / "function_struct_usage_graph.dot",
        "function_struct_usage_graph",
        list(fn_names) + list(struct_names),
        fn_struct_edges,
        "box",
    )

    print(
        f"Generated {len(fn_names)} functions, {len(struct_names)} structs, "
        f"{len(call_edges)} function-call edges, {len(struct_edges)} struct edges, "
        f"{len(fn_struct_edges)} function-struct edges."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
