// Typed JavaScript subset compiled by PolyglotCompiler's JavaScript frontend.

/**
 * Convert an activity signal and login recency into a retention health score.
 *
 * @param {i64} activitySignal
 * @param {i64} daysSinceLogin
 * @param {i64} inactivityWeight
 * @returns {i64}
 */
function js_retention_score(activitySignal, daysSinceLogin, inactivityWeight) {
    return activitySignal - daysSinceLogin * inactivityWeight;
}

