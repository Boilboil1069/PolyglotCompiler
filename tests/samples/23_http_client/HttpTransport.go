// HttpTransport.go — Tiny request-line builder.
// Part of the PolyglotCompiler sample matrix.

package main

import (
    "fmt"
    "strings"
)

// Get formats an HTTP/1.1 request line for the given URL.
func Get(url string, headers map[string]string) string {
    var sb strings.Builder
    fmt.Fprintf(&sb, "GET %s HTTP/1.1\r\n", url)
    for k, v := range headers {
        fmt.Fprintf(&sb, "%s: %s\r\n", k, v)
    }
    sb.WriteString("\r\n")
    return sb.String()
}

