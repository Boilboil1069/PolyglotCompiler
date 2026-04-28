// Fixture: JavaScript source that requires ES2020 (optional chaining).
// PolyglotCompiler's JavaScript frontend gates '?.' on --ecma=es2020+.

export function userCity(user) {
    return user?.address?.city ?? "<unknown>";
}
