// SchemaValidator.java — Validates a normalised key/value record.
// Part of the PolyglotCompiler sample matrix.

import java.util.LinkedHashMap;
import java.util.Map;

public final class SchemaValidator {
    private SchemaValidator() { }

    public static Map<String, String> normalise(Map<String, Object> input) {
        Map<String, String> out = new LinkedHashMap<>();
        for (Map.Entry<String, Object> e : input.entrySet()) {
            out.put(e.getKey(), String.valueOf(e.getValue()));
        }
        return out;
    }

    public static boolean hasKey(Map<String, ?> input, String key) {
        return input != null && input.containsKey(key);
    }
}

