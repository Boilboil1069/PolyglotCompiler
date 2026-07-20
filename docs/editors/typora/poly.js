/*
 * Poly / Ploy mode for CodeMirror 5 and Typora.
 *
 * The compiler calls the language "Ploy" and uses the .ploy extension.
 * "poly" is kept as a first-class fence alias because it is the natural
 * spelling used in documentation. Both names select this same mode.
 */
(function (root, factory) {
  "use strict";

  if (typeof module === "object" && module.exports) {
    // Returning the installer keeps the file testable without depending on
    // an npm copy of CodeMirror. Browser users get automatic registration.
    module.exports = factory;
  } else if (typeof define === "function" && define.amd) {
    define([], function () { return factory(root.CodeMirror); });
  } else {
    factory(root.CodeMirror);
  }
}(typeof globalThis !== "undefined" ? globalThis : this, function (CodeMirror) {
  "use strict";

  if (!CodeMirror) {
    throw new Error("Poly highlighting requires CodeMirror 5");
  }

  var MODE_VERSION = "1.0.0";

  // This list is kept in lockstep with kCanonicalKeywords in
  // frontends/ploy/src/lexer/lexer.cpp. Keywords are folded to upper case by
  // the compiler, so highlighting must also be case-insensitive.
  var KEYWORDS = wordSet([
    "LINK", "IMPORT", "EXPORT", "MAP_TYPE", "PIPELINE", "FUNC", "LET", "VAR",
    "RETURN", "RETURNS", "IF", "ELSE", "WHILE", "FOR", "IN", "MATCH", "CASE",
    "DEFAULT", "BREAK", "CONTINUE", "AS", "AND", "OR", "NOT", "CALL", "STRUCT",
    "PACKAGE", "MAP_FUNC", "CONVERT", "CONFIG", "VENV", "CONDA", "UV", "PIPENV",
    "POETRY", "NEW", "METHOD", "GET", "SET", "WITH", "DELETE", "EXTEND", "LANG",
    "PRINTLN", "STAGE", "TYPE", "CONST", "TRY", "CATCH", "FINALLY", "THROW",
    "ASYNC", "AWAIT", "WHERE", "PUB", "PRIVATE"
  ]);

  var TYPES = wordSet([
    "VOID", "INT", "FLOAT", "STRING", "BOOL", "ARRAY", "LIST", "TUPLE", "DICT",
    "OPTION", "I8", "I16", "I32", "I64", "U8", "U16", "U32", "U64", "F32",
    "F64", "USIZE", "ISIZE", "ERROR", "HANDLE"
  ]);

  var ATOMS = wordSet(["TRUE", "FALSE", "NULL"]);

  // CLASS, HANDLE, and ATTR are contextual in the parser. A lexical editor
  // mode cannot know all parser contexts cheaply, so it intentionally gives
  // these spellings a stable visual category everywhere. HANDLE is grouped
  // with types above; CLASS and ATTR use the keyword colour.
  var CONTEXTUAL_KEYWORDS = wordSet(["CLASS", "ATTR"]);

  var DEFINITION_PREFIXES = wordSet([
    "FUNC", "STRUCT", "PIPELINE", "STAGE", "TYPE", "CONST", "LET", "VAR"
  ]);

  var OPTION_CONSTRUCTORS = wordSet(["SOME", "NONE"]);

  function wordSet(words) {
    var set = Object.create(null);
    words.forEach(function (word) { set[word] = true; });
    return set;
  }

  function clearStringState(state) {
    state.tokenizer = "base";
    state.stringEnd = "";
    state.stringAllowsEscapes = false;
    state.stringEscaped = false;
  }

  function scanString(stream, state) {
    while (!stream.eol()) {
      if (state.stringEscaped) {
        stream.next();
        state.stringEscaped = false;
        continue;
      }

      if (stream.match(state.stringEnd)) {
        clearStringState(state);
        break;
      }

      var ch = stream.next();
      if (state.stringAllowsEscapes && ch === "\\") {
        state.stringEscaped = true;
      }
    }
    return "string";
  }

  function scanBlockComment(stream, state) {
    while (!stream.eol()) {
      if (stream.match("*/")) {
        state.tokenizer = "base";
        break;
      }
      stream.next();
    }
    return "comment";
  }

  function beginString(stream, state, opener, end, allowsEscapes) {
    if (!stream.match(opener)) return false;
    state.tokenizer = "string";
    state.stringEnd = end;
    state.stringAllowsEscapes = allowsEscapes;
    state.stringEscaped = false;
    scanString(stream, state);
    return true;
  }

  function tokenBase(stream, state) {
    if (stream.eatSpace()) return null;

    // Four or more leading slashes are ordinary comments in the compiler;
    // only exactly three introduce a documentation line.
    if (stream.match(/^\/\/\/(?!\/)/)) {
      stream.skipToEnd();
      return "comment meta";
    }
    if (stream.match("//")) {
      stream.skipToEnd();
      return "comment";
    }
    if (stream.match("/*")) {
      state.tokenizer = "blockComment";
      return scanBlockComment(stream, state);
    }

    // Raw strings must be checked before identifiers. Only lower-case r is
    // a prefix in the compiler lexer; R"..." remains an identifier + string.
    var raw = stream.match(/^r(#+)?"/);
    if (raw) {
      state.tokenizer = "string";
      state.stringEnd = "\"" + (raw[1] || "");
      state.stringAllowsEscapes = false;
      state.stringEscaped = false;
      return scanString(stream, state);
    }

    // Template and multiline forms. Interpolation is emitted as one kString
    // token by the real lexer, so the editor uses the same whole-string class.
    if (beginString(stream, state, "f\"\"\"", "\"\"\"", true)) return "string";
    if (beginString(stream, state, "\"\"\"", "\"\"\"", true)) return "string";
    if (beginString(stream, state, "f\"", "\"", true)) return "string";
    if (beginString(stream, state, "\"", "\"", true)) return "string";

    // Match the compiler's number scanner, including its accepted prefixed
    // forms and an optional decimal exponent.
    if (stream.match(/^0[xX][0-9a-fA-F]*/)) return "number";
    if (stream.match(/^0[bB][01]*/)) return "number";
    if (stream.match(/^0[oO][0-7]*/)) return "number";
    if (stream.match(/^\d+\.\d+(?:[eE][+-]?\d*)?/)) return "number";
    if (stream.match(/^\d+[eE][+-]?\d*/)) return "number";
    if (stream.match(/^\d+/)) return "number";

    if (stream.match(/^@[A-Za-z_][A-Za-z0-9_]*/)) {
      state.expectDefinition = false;
      state.expectProperty = false;
      return "meta";
    }

    var identifier = stream.match(/^[A-Za-z_][A-Za-z0-9_]*/);
    if (identifier) {
      var spelling = identifier[0];
      var folded = spelling.toUpperCase();

      if (state.expectDefinition) {
        state.expectDefinition = false;
        state.expectProperty = false;
        return "def";
      }
      if (state.expectProperty) {
        state.expectProperty = false;
        return "property";
      }
      if (ATOMS[folded]) return "atom";
      if (TYPES[folded]) return "variable-3";
      if (CONTEXTUAL_KEYWORDS[folded]) return "keyword";
      if (KEYWORDS[folded]) {
        if (DEFINITION_PREFIXES[folded]) state.expectDefinition = true;
        return "keyword";
      }
      // Some and None are parser-recognised constructors, not globally
      // reserved words. Keep their source spelling case-sensitive.
      if ((spelling === "Some" || spelling === "None") && OPTION_CONSTRUCTORS[folded]) {
        return "builtin";
      }
      return "variable";
    }

    var operator = stream.match(/^(?:::|->|=>|\.\.=|\.\.|==|!=|<=|>=|&&|\|\||~=|[+\-*\/%<>=!?~&|.])/);
    if (operator) {
      state.expectDefinition = false;
      state.expectProperty = operator[0] === "." || operator[0] === "::";
      return "operator";
    }

    var punctuation = stream.next();
    state.expectDefinition = false;
    state.expectProperty = false;
    if (punctuation === "{") state.braceDepth += 1;
    if (punctuation === "}") state.braceDepth = Math.max(0, state.braceDepth - 1);
    return /[()\[\]{}]/.test(punctuation) ? "bracket" : null;
  }

  CodeMirror.defineMode("poly", function (config) {
    var indentUnit = config.indentUnit || 2;

    return {
      startState: function () {
        return {
          tokenizer: "base",
          stringEnd: "",
          stringAllowsEscapes: false,
          stringEscaped: false,
          expectDefinition: false,
          expectProperty: false,
          braceDepth: 0
        };
      },

      copyState: function (state) {
        return {
          tokenizer: state.tokenizer,
          stringEnd: state.stringEnd,
          stringAllowsEscapes: state.stringAllowsEscapes,
          stringEscaped: state.stringEscaped,
          expectDefinition: state.expectDefinition,
          expectProperty: state.expectProperty,
          braceDepth: state.braceDepth
        };
      },

      token: function (stream, state) {
        if (state.tokenizer === "string") return scanString(stream, state);
        if (state.tokenizer === "blockComment") return scanBlockComment(stream, state);
        return tokenBase(stream, state);
      },

      indent: function (state, textAfter) {
        var depth = state.braceDepth;
        if (/^\s*}/.test(textAfter)) depth = Math.max(0, depth - 1);
        return depth * indentUnit;
      },

      electricInput: /^\s*[{}]$/,
      lineComment: "//",
      blockCommentStart: "/*",
      blockCommentEnd: "*/",
      fold: "brace",
      closeBrackets: {pairs: "()[]{}\"\"", triples: "\""}
    };
  });

  // Direct mode lookup is useful outside Typora. Typora itself maps both
  // fence names to text/x-poly through install_typora.py.
  CodeMirror.defineMode("ploy", function (config) {
    return CodeMirror.getMode(config, "poly");
  });
  CodeMirror.defineMIME("text/x-poly", "poly");
  CodeMirror.defineMIME("text/x-ploy", "poly");

  if (Array.isArray(CodeMirror.modeInfo) &&
      !CodeMirror.modeInfo.some(function (item) { return item.mode === "poly"; })) {
    CodeMirror.modeInfo.push({
      name: "Poly / Ploy",
      mime: "text/x-poly",
      mode: "poly",
      ext: ["ploy", "poly"],
      alias: ["poly", "ploy"]
    });
  }

  CodeMirror.polyModeVersion = MODE_VERSION;
  CodeMirror.polyModeKeywords = Object.keys(KEYWORDS)
    .concat(Object.keys(TYPES).filter(function (word) { return word !== "HANDLE"; }))
    .concat(Object.keys(ATOMS))
    .sort();
  CodeMirror.polyModeContextualKeywords = ["ATTR", "CLASS", "HANDLE"];
  return MODE_VERSION;
}));
