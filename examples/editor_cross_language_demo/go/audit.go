package audit

// audit_total produces an audit code from the net amount and item count.
// A nonnegative result identifies this example order in the call graph.
func audit_total(net_cents int64, quantity int64) int64 {
    return net_cents + quantity
}
