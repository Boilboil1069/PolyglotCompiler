// ============================================================================
// SortEngine.java — Java generic sorting and map utilities
// Compiled by PolyglotCompiler's frontend_java → shared IR
// ============================================================================

import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.Map;

public class SortEngine {
    private final HashMap<String, Integer> entries;

    // Default constructor
    public SortEngine() {
        this.entries = new HashMap<>();
    }

    // Add a key-value entry
    public void addEntry(String key, int value) {
        entries.put(key, value);
    }

    // Return alphabetically sorted keys
    public ArrayList<String> sortedKeys() {
        ArrayList<String> keys = new ArrayList<>(entries.keySet());
        Collections.sort(keys);
        return keys;
    }

    // Sort a list of integers in ascending order
    public ArrayList<Integer> sortInts(ArrayList<Integer> data) {
        ArrayList<Integer> copy = new ArrayList<>(data);
        Collections.sort(copy);
        return copy;
    }

    // Get the underlying map
    public HashMap<String, Integer> getMap() {
        return new HashMap<>(entries);
    }

    // Get the entry count
    public int size() {
        return entries.size();
    }
}
