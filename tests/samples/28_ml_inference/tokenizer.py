"""tokenizer.py — Whitespace tokenizer with a tiny built-in vocabulary."""

from __future__ import annotations

VOCAB = {"hello": 1, "world": 2, "polyglot": 3, "compiler": 4}


def encode(text: str):
    if not text:
        return []
    return [VOCAB.get(token.lower(), 0) for token in text.split()]


def vocab_size() -> int:
    return len(VOCAB) + 1

