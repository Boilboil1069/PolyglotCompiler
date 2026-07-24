// Stateless Java rule compiled by PolyglotCompiler's Java frontend.
// A plain static method keeps the cross-language boundary on an i64 ABI.

public final class SignalRules {
    public static long java_activity_signal(
            long sessions,
            long purchases,
            long complaints,
            long sessionWeight,
            long purchaseWeight,
            long complaintWeight) {
        // Compound assignments retain the declared long type throughout the
        // Java frontend's strict static subset.
        sessions *= sessionWeight;
        purchases *= purchaseWeight;
        complaints *= complaintWeight;
        sessions += purchases;
        sessions -= complaints;
        return sessions;
    }
}
