#!/usr/bin/env python3
"""Audit the complete textbook against its bilingual editorial contract.

The audit is intentionally conservative. It ignores code, commands, link targets,
machine fields, and approved product names, but rejects ordinary English terms in
Chinese prose when the terminology dictionary gives a canonical Chinese form. It
also checks that a Chinese explanatory block is followed by an English block
before the next Chinese explanation or section heading.
"""

from __future__ import annotations

import argparse
import math
import re
import sys
from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_TUTORIAL = ROOT / "docs" / "POLYGLOT_COMPILER_COMPLETE_TUTORIAL.md"
DEFAULT_DICTIONARY = ROOT / "docs" / "POLYGLOT_COMPILER_BILINGUAL_DICTIONARY.md"

HAN_RE = re.compile(r"[\u3400-\u4dbf\u4e00-\u9fff]")
LATIN_WORD_RE = re.compile(r"\b[A-Za-z](?:[A-Za-z-]*[A-Za-z])\b")
INLINE_CODE_RE = re.compile(r"`[^`]*`")
LINK_TARGET_RE = re.compile(r"\]\([^)]+\)")
BILINGUAL_LABEL_RE = re.compile(r"\*\*[^*]+\s/\s[A-Za-z][^*]+\*\*")

# Product names, language names, formats, and established acronyms may remain in
# Chinese prose. Exact source identifiers are separately protected by backticks.
ALLOWED_WORDS = {
    "PolyglotCompiler", "Ploy", "PolyUI", "Typora", "CMake", "Ninja",
    "LLVM", "GitHub", "Qt", "Python", "Rust", "Java", "JavaScript",
    "TypeScript", "Ruby", "Go", "Linux", "Windows", "macOS", "Mach-O",
    "ELF", "COFF", "PE", "WebAssembly", "WASM", "JSON", "YAML", "TOML",
    "Markdown", "Unicode", "ASCII", "UTF", "NDJSON", "SQLite", "C",
    "API", "ABI", "FFI", "IR", "SSA", "CFG", "CLI", "IDE", "LSP",
    "AST", "GC", "EH", "JIT", "AOT", "PGO", "LTO", "DCE", "CSE",
    "GVN", "URI", "URL", "RPC", "SDK", "JDK", "JVM", "GIL", "CI",
    "CPU", "GPU", "MVP", "HTTP", "CSV", "EBNF", "POSIX", "MSVC",
    "NET", "Phi", "MachineIR", "DWARF", "PDB", "CTest", "Catch2",
    "PowerShell", "QSS", "NUL", "NaN", "JSON-RPC", "Node", "V8",
    "FNV", "README", "LICENSE", "NSIS", "JNI", "SCC", "TCO", "LICM",
    "SRA", "BFS", "MPMC", "GUID", "PIC", "WAT", "XDG", "APPDATA",
    "CodeMirror", "CSS", "MIME", "CommonMark", "GFM", "ESM", "CommonJS",
    "RFC", "PNG", "JPEG", "WebP", "GIF", "SVG", "BMP", "clang-format",
    "clang-tidy", "clangd", "pyright", "jdtls", "OmniSharp", "Cargo",
    "Maven", "Gradle", "NuGet", "Bundler", "gem", "npm", "pip", "Conda",
    "Poly", "Lua", "Apple", "Bash", "CRLF", "POBJ", "BSS", "CIE", "FDE",
    "GOT", "PLT", "PRE", "ThinLTO", "SCCP", "ASan", "UBSan", "LSan",
    "DDL", "DML", "PostgreSQL", "MySQL", "TODO", "FIXME", "cgo", "UI",
    "JS", "TS", "VM", "OS", "Hz", "Polyglot", "RAII", "QProcess", "QSettings",
    "LIFO", "gccgo", "CPython", "UEDGE", "SQL", "CodeView", "GiB", "KiB",
    "RGBA", "GCC", "ECMAScript", "HTML", "tree-sitter", "DLL", "ICU", "DOT",
    "Graphviz", "Kahn", "CRuby", "CoreCLR", "CLR", "Unix", "NumPy", "RPATH",
    "RUNNABLE",
}
ALLOWED_LOWER = {word.lower() for word in ALLOWED_WORDS}

# These ordinary English terms all have normative Chinese forms in the external
# dictionary. Keeping the list explicit makes failures actionable and prevents a
# newly added paragraph from silently returning to the old mixed-language style.
FORBIDDEN_TERMS = {
    "lexer", "lexing", "token", "tokens", "tokenize", "tokenization",
    "parser", "parse", "parsing", "sema", "lowering", "lowerer",
    "frontend", "backend", "runtime", "bridge", "adapter", "linker",
    "verifier", "optimizer", "optimiser", "driver", "toolchain",
    "grammar", "spelling", "identifier", "identifiers", "keyword",
    "keywords", "literal", "literals", "expression", "expressions",
    "statement", "statements", "declaration", "declarations", "binding",
    "bindings", "scope", "symbol", "symbols", "function", "functions",
    "parameter", "parameters", "argument", "arguments", "typed", "type",
    "types", "inference", "pattern", "patterns", "guard", "guards",
    "exhaustiveness", "truthiness", "shape", "opaque", "dispatch",
    "basic", "block", "blocks", "terminator", "predecessor", "successor",
    "target", "object", "executable", "container", "relocation", "layout",
    "alignment", "width", "entry", "artifact", "host", "foreign",
    "marshalling", "marshal", "descriptor", "handle", "pointer",
    "ownership", "lifetime", "borrowed", "owned", "retain", "release",
    "cleanup", "rollback", "diagnostic", "diagnostics", "warning",
    "warnings", "severity", "strict", "fallback", "exception", "exceptions",
    "throw", "rethrow", "async", "await", "future", "module", "package",
    "import", "export", "alias", "namespace", "path", "registry",
    "settings", "workspace", "version", "compatibility", "migration",
    "test", "tests", "fixture", "sample", "output", "schema", "payload",
    "metadata", "profile", "profiling", "pipeline", "topology", "plugin",
    "extension", "capability", "manifest", "callback", "thread", "session",
    "builder", "visitor", "reader", "writer", "serializer", "codec",
    "renderer", "handler", "runner", "manager", "provider", "service",
    "services", "store", "storage", "pool", "arena", "deque", "map",
    "mapping", "mappings", "array", "arrays", "scalar", "scalars",
    "field", "fields", "property", "properties", "getter", "widget",
    "widgets", "model", "models", "hook", "hooks", "protocol",
    "transport", "framing", "baseline", "framework", "template",
    "templates", "helper", "helpers", "invariant", "invariants",
    "deterministic", "reentrant", "non-destructive", "collision",
    "conflict", "detection", "discovery", "opcode", "opcodes",
    "operand", "operands", "liveness", "scheduling", "emission",
    "direct", "indirect", "observable", "effect", "effects", "before",
    "after", "idempotence", "invalidation", "lossless", "widening",
    "bitcast", "signedness", "boxing", "boxed", "unboxing",
    "contiguous", "stride", "spill", "shadow", "threshold", "feature",
    "features", "platform", "exact", "implicit", "explicit", "mandatory",
    "optional", "fatal", "success", "failure", "null", "commit", "date",
    "report", "reports", "lint", "skip", "quickstart", "workflow",
    "wiring", "major", "minor", "conservative", "pair", "pairs",
}


@dataclass(frozen=True)
class Block:
    start_line: int
    kind: str
    text: str


def protected_prose(text: str) -> str:
    text = INLINE_CODE_RE.sub("", text)
    text = LINK_TARGET_RE.sub("]()", text)
    text = BILINGUAL_LABEL_RE.sub("", text)
    return text


def chinese_side_of_bilingual_line(text: str) -> str:
    """Remove an explicit English counterpart before auditing Chinese wording.

    Markdown headings and TOC entries deliberately use ``Chinese / English`` on
    one line.  The English half is not code-switching inside Chinese prose and
    must not be reported as such.
    """
    if " / " not in text:
        return text
    chinese, english = text.split(" / ", 1)
    if HAN_RE.search(chinese) and LATIN_WORD_RE.search(english):
        return chinese
    return text


def classify(text: str) -> str:
    prose = protected_prose(text)
    has_han = bool(HAN_RE.search(prose))
    has_latin = bool(LATIN_WORD_RE.search(prose))
    raw_lines = [line for line in text.splitlines() if line.strip()]
    paired_lines = sum(
        1
        for line in raw_lines
        if " / " in line and HAN_RE.search(line) and LATIN_WORD_RE.search(line.split(" / ", 1)[1])
    )
    if has_han and paired_lines and paired_lines >= math.ceil(len(raw_lines) * 0.7):
        return "bilingual"
    if has_han:
        return "zh"
    if has_latin:
        return "en"
    return "neutral"


def parse_blocks(lines: list[str]) -> list[Block]:
    blocks: list[Block] = []
    current: list[str] = []
    start_line = 0
    in_fence = False

    def flush() -> None:
        nonlocal current, start_line
        if current:
            text = "\n".join(current).strip()
            if text:
                blocks.append(Block(start_line, classify(text), text))
        current = []
        start_line = 0

    for line_number, raw in enumerate(lines, 1):
        stripped = raw.strip()
        if stripped.startswith("```"):
            flush()
            in_fence = not in_fence
            continue
        if in_fence:
            continue
        if not stripped:
            flush()
            continue
        if stripped.startswith("#"):
            flush()
            blocks.append(Block(line_number, "heading", stripped))
            continue
        if stripped.startswith("|") or re.match(r"^\s*[-:]+\s*$", stripped):
            flush()
            continue
        if stripped == "---" or stripped.startswith("<"):
            flush()
            continue
        if not current:
            start_line = line_number
        current.append(stripped)
    flush()
    return blocks


def mixed_language_findings(lines: list[str]) -> list[tuple[int, str]]:
    findings: list[tuple[int, str]] = []
    in_fence = False
    for line_number, raw in enumerate(lines, 1):
        stripped = raw.strip()
        if stripped.startswith("```"):
            in_fence = not in_fence
            continue
        if in_fence or not HAN_RE.search(raw):
            continue
        if stripped.startswith("#") or stripped.startswith("|"):
            continue
        prose = protected_prose(chinese_side_of_bilingual_line(raw))
        words = {word.lower() for word in LATIN_WORD_RE.findall(prose)}
        bad = sorted(words - ALLOWED_LOWER)
        if bad:
            canonical = sorted(set(bad) & FORBIDDEN_TERMS)
            unclassified = sorted(set(bad) - FORBIDDEN_TERMS)
            details: list[str] = []
            if canonical:
                details.append(f"ordinary English terms: {', '.join(canonical)}")
            if unclassified:
                details.append(
                    "Latin spellings absent from the dictionary allowlist: "
                    + ", ".join(unclassified)
                )
            findings.append((line_number, "; ".join(details)))
    return findings


def pairing_findings(blocks: list[Block]) -> list[tuple[int, str]]:
    findings: list[tuple[int, str]] = []
    for index, block in enumerate(blocks):
        if block.kind != "zh":
            continue
        # Very short labels and bilingual blockquote/table introductions are not
        # explanatory paragraphs. Headings and machine tables are audited by
        # human review because duplicating them can reduce readability.
        prose = protected_prose(block.text)
        if len(HAN_RE.findall(prose)) < 8:
            continue
        next_kind = None
        for following in blocks[index + 1 :]:
            if following.kind == "neutral":
                continue
            next_kind = following.kind
            break
        if next_kind != "en":
            preview = re.sub(r"\s+", " ", block.text)[:90]
            findings.append((block.start_line, f"Chinese block has no following English counterpart: {preview}"))
    return findings


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("tutorial", nargs="?", type=Path, default=DEFAULT_TUTORIAL)
    parser.add_argument("--dictionary", type=Path, default=DEFAULT_DICTIONARY)
    parser.add_argument("--max-findings", type=int, default=80)
    parser.add_argument("--report-only", action="store_true")
    args = parser.parse_args()

    tutorial = args.tutorial.resolve()
    dictionary = args.dictionary.resolve()
    if not tutorial.is_file() or not dictionary.is_file():
        print("tutorial or terminology dictionary is missing", file=sys.stderr)
        return 2

    text = tutorial.read_text(encoding="utf-8")
    lines = text.splitlines()
    findings: list[tuple[int, str]] = []
    dictionary_name = dictionary.name
    if dictionary_name not in text:
        findings.append((1, f"tutorial does not link the normative dictionary {dictionary_name}"))
    findings.extend(mixed_language_findings(lines))
    findings.extend(pairing_findings(parse_blocks(lines)))
    findings.sort(key=lambda item: (item[0], item[1]))

    for line_number, message in findings[: args.max_findings]:
        print(f"{tutorial}:{line_number}: {message}")
    if len(findings) > args.max_findings:
        print(f"... {len(findings) - args.max_findings} more finding(s)")
    print(f"bilingual audit: {len(findings)} finding(s)")
    return 0 if args.report_only or not findings else 1


if __name__ == "__main__":
    raise SystemExit(main())
