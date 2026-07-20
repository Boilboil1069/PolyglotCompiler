#!/usr/bin/env node
"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const installPolyMode = require("../poly.js");

const CodeMirror = {
  modes: Object.create(null),
  mimeModes: Object.create(null),
  modeInfo: [],
  defineMode(name, factory) {
    this.modes[name] = factory;
  },
  defineMIME(mime, mode) {
    this.mimeModes[mime] = mode;
  },
  getMode(config, spec) {
    const name = typeof spec === "string" ? spec : spec.name;
    return this.modes[name](config, spec);
  }
};

class StringStream {
  constructor(text) {
    this.string = text;
    this.pos = 0;
    this.start = 0;
  }

  sol() { return this.pos === 0; }
  eol() { return this.pos >= this.string.length; }
  peek() { return this.string.charAt(this.pos) || undefined; }
  next() { return this.pos < this.string.length ? this.string.charAt(this.pos++) : undefined; }

  eat(match) {
    const ch = this.peek();
    const ok = typeof match === "string" ? ch === match : match.test(ch || "");
    if (ok) {
      this.pos += 1;
      return ch;
    }
    return undefined;
  }

  eatWhile(match) {
    const before = this.pos;
    while (!this.eol() && this.eat(match)) {}
    return this.pos > before;
  }

  eatSpace() { return this.eatWhile(/[\s\u00a0]/); }
  skipToEnd() { this.pos = this.string.length; }
  current() { return this.string.slice(this.start, this.pos); }

  match(pattern, consume = true, caseFold = false) {
    const rest = this.string.slice(this.pos);
    let result;
    if (typeof pattern === "string") {
      const source = caseFold ? rest.toLowerCase() : rest;
      const target = caseFold ? pattern.toLowerCase() : pattern;
      if (!source.startsWith(target)) return false;
      result = true;
      if (consume) this.pos += pattern.length;
      return result;
    }

    result = rest.match(pattern);
    if (!result || result.index !== 0) return false;
    if (consume) this.pos += result[0].length;
    return result;
  }
}

function tokenize(mode, lines) {
  const state = mode.startState();
  const tokens = [];
  for (let lineNumber = 0; lineNumber < lines.length; ++lineNumber) {
    const stream = new StringStream(lines[lineNumber]);
    while (!stream.eol()) {
      stream.start = stream.pos;
      const style = mode.token(stream, state);
      assert.ok(stream.pos > stream.start, `mode stalled on line ${lineNumber + 1}`);
      tokens.push({line: lineNumber + 1, text: stream.current(), style});
    }
  }
  return {tokens, state};
}

function hasToken(tokens, text, style) {
  assert.ok(
    tokens.some(token => token.text === text && token.style === style),
    `expected ${JSON.stringify(text)} to use style ${JSON.stringify(style)}`
  );
}

assert.equal(installPolyMode(CodeMirror), "1.0.0");
assert.equal(CodeMirror.mimeModes["text/x-poly"], "poly");
assert.equal(CodeMirror.mimeModes["text/x-ploy"], "poly");
assert.equal(CodeMirror.modeInfo[0].name, "Poly");
assert.deepEqual(CodeMirror.modeInfo[0].ext, ["poly", "ploy"]);
assert.deepEqual(CodeMirror.modeInfo[0].alias, ["poly", "ploy"]);
assert.deepEqual(CodeMirror.polyModeContextualKeywords, ["ATTR", "CLASS", "HANDLE"]);

// Fail when the real compiler gains or loses a global keyword without a
// matching editor update. Restrict extraction to the initializer so comments
// elsewhere in lexer.cpp cannot affect the comparison.
const lexerPath = path.resolve(__dirname, "../../../../frontends/ploy/src/lexer/lexer.cpp");
const lexerSource = fs.readFileSync(lexerPath, "utf8");
const keywordStart = lexerSource.indexOf("kCanonicalKeywords = {");
const keywordEnd = lexerSource.indexOf("};", keywordStart);
assert.ok(keywordStart >= 0 && keywordEnd > keywordStart, "compiler keyword set not found");
const compilerKeywords = [...lexerSource.slice(keywordStart, keywordEnd)
  .matchAll(/"([A-Z][A-Z0-9_]*)"/g)]
  .map(match => match[1])
  .sort();
assert.equal(new Set(compilerKeywords).size, 82);
assert.deepEqual(CodeMirror.polyModeKeywords, compilerKeywords);

// Typora loads the injected file as a browser script, not through CommonJS.
// Exercise that UMD branch as well as the direct installer used above.
const browserCodeMirror = {
  modes: Object.create(null),
  mimeModes: Object.create(null),
  modeInfo: [],
  defineMode: CodeMirror.defineMode,
  defineMIME: CodeMirror.defineMIME,
  getMode: CodeMirror.getMode
};
const modePath = path.resolve(__dirname, "../poly.js");
vm.runInNewContext(fs.readFileSync(modePath, "utf8"), {CodeMirror: browserCodeMirror});
assert.equal(browserCodeMirror.polyModeVersion, "1.0.0");
assert.equal(browserCodeMirror.mimeModes["text/x-poly"], "poly");
assert.equal(browserCodeMirror.mimeModes["text/x-ploy"], "poly");

const mode = CodeMirror.getMode({indentUnit: 2}, "poly");
const aliasMode = CodeMirror.getMode({indentUnit: 2}, "ploy");
assert.equal(typeof aliasMode.token, "function");

const source = [
  "@inline pub async func greet(name: STRING) -> STRING {",
  "  /// A documentation comment.",
  "  //// An ordinary comment, not documentation.",
  "  let answer: i32 = 0x2A;",
  "  let raw = r#\"contains \\\" and \"quotes\"\"#;",
  "  let message = f\"\"\"answer = {answer}",
  "on two lines\"\"\";",
  "  if TRUE and answer >= 0 { return Some(message); }",
  "}",
  "CLASS python::pkg::Widget { ATTR name: STRING; METHOD run() -> VOID; }",
  "let handle: HANDLE<python::pkg::Widget> = None;",
  "/* block",
  "   comment */"
];

const result = tokenize(mode, source);
hasToken(result.tokens, "@inline", "meta");
hasToken(result.tokens, "pub", "keyword");
hasToken(result.tokens, "func", "keyword");
hasToken(result.tokens, "greet", "def");
hasToken(result.tokens, "STRING", "variable-3");
hasToken(result.tokens, "/// A documentation comment.", "comment meta");
hasToken(result.tokens, "//// An ordinary comment, not documentation.", "comment");
hasToken(result.tokens, "0x2A", "number");
hasToken(result.tokens, "TRUE", "atom");
hasToken(result.tokens, "Some", "builtin");
hasToken(result.tokens, "CLASS", "keyword");
hasToken(result.tokens, "ATTR", "keyword");
hasToken(result.tokens, "HANDLE", "variable-3");
hasToken(result.tokens, "None", "builtin");
hasToken(result.tokens, "Widget", "property");
assert.ok(result.tokens.some(token => token.text.startsWith("r#\"") && token.style === "string"));
assert.ok(result.tokens.some(token => token.text.startsWith("f\"\"\"") && token.style === "string"));
assert.ok(result.tokens.filter(token => token.style === "comment").length >= 2);
assert.equal(result.state.tokenizer, "base");
assert.equal(mode.indent({...result.state, braceDepth: 2}, "}"), 2);

console.log(`poly mode: ${result.tokens.length} tokens checked; aliases poly/ploy ready`);
