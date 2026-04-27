// Package mathpkg provides a tiny addition helper used by the Go
// ImportResolver integration test.
package mathpkg

// Add returns the sum of a and b.
func Add(a int, b int) int {
    return a + b
}

// Mul returns the product of a and b.
func Mul(a int, b int) int {
    return a * b
}
