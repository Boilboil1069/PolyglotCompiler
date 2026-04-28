// Fixture: Go source that requires Go 1.18 (generics).
// PolyglotCompiler propagates the Go version through the .ploy `LANG`
// pragma so that downstream tooling (`go build` invocations, runtime
// dispatch) targets the correct toolchain.
package generics

func Map[T any, U any](in []T, f func(T) U) []U {
    out := make([]U, 0, len(in))
    for _, v := range in {
        out = append(out, f(v))
    }
    return out
}
