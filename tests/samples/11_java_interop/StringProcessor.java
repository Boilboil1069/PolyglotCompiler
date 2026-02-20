// ============================================================================
// StringProcessor.java — Java class for Java interop demo
// Compiled by PolyglotCompiler's frontend_java → shared IR
// ============================================================================

import java.util.ArrayList;
import java.util.List;

public class StringProcessor {
    private String prefix;
    private int processCount;

    // Constructor
    public StringProcessor(String prefix) {
        this.prefix = prefix;
        this.processCount = 0;
    }

    // Format a message with the stored prefix
    public String format(String message) {
        processCount++;
        return prefix + ": " + message;
    }

    // Split a string by delimiter and return parts count
    public int countParts(String text, String delimiter) {
        String[] parts = text.split(delimiter);
        return parts.length;
    }

    // Reverse a string
    public String reverse(String input) {
        processCount++;
        return new StringBuilder(input).reverse().toString();
    }

    // Get how many times process methods have been called
    public int getProcessCount() {
        return processCount;
    }

    // Static utility: compute the Levenshtein distance between two strings
    public static int levenshteinDistance(String a, String b) {
        int[][] dp = new int[a.length() + 1][b.length() + 1];
        for (int i = 0; i <= a.length(); i++) dp[i][0] = i;
        for (int j = 0; j <= b.length(); j++) dp[0][j] = j;
        for (int i = 1; i <= a.length(); i++) {
            for (int j = 1; j <= b.length(); j++) {
                int cost = (a.charAt(i - 1) == b.charAt(j - 1)) ? 0 : 1;
                dp[i][j] = Math.min(
                    Math.min(dp[i - 1][j] + 1, dp[i][j - 1] + 1),
                    dp[i - 1][j - 1] + cost
                );
            }
        }
        return dp[a.length()][b.length()];
    }
}
