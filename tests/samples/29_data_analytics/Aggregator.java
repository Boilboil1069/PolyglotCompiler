// Aggregator.java — Computes count/min/max/mean over a numeric column.
// Part of the PolyglotCompiler sample matrix.

import java.util.HashMap;
import java.util.List;
import java.util.Map;

public final class Aggregator {
    public Map<String, Double> stats(List<Double> column) {
        Map<String, Double> out = new HashMap<>();
        if (column == null || column.isEmpty()) {
            out.put("count", 0.0);
            return out;
        }
        double min = Double.POSITIVE_INFINITY;
        double max = Double.NEGATIVE_INFINITY;
        double sum = 0.0;
        for (double v : column) {
            if (v < min) min = v;
            if (v > max) max = v;
            sum += v;
        }
        out.put("count", (double) column.size());
        out.put("min", min);
        out.put("max", max);
        out.put("mean", sum / column.size());
        return out;
    }
}

