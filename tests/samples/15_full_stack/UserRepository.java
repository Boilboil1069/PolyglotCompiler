// ============================================================================
// UserRepository.java — Java user storage
// Compiled by PolyglotCompiler's frontend_java → shared IR
// ============================================================================

import java.util.ArrayList;
import java.util.HashMap;
import java.util.Map;

public class UserRepository {
    private final ArrayList<Map<String, Object>> users;

    // Default constructor
    public UserRepository() {
        this.users = new ArrayList<>();
    }

    // Add a user record
    public void addUser(String id, String name, String passwordHash, double score) {
        Map<String, Object> user = new HashMap<>();
        user.put("id", id);
        user.put("name", name);
        user.put("passwordHash", passwordHash);
        user.put("score", score);
        users.add(user);
    }

    // Get the number of registered users
    public int count() {
        return users.size();
    }

    // List all scores as an ArrayList<Double>
    public ArrayList<Double> listScores() {
        ArrayList<Double> scores = new ArrayList<>();
        for (Map<String, Object> u : users) {
            scores.add((Double) u.get("score"));
        }
        return scores;
    }

    // Find a user name by ID (returns empty string if not found)
    public String findNameById(String id) {
        for (Map<String, Object> u : users) {
            if (id.equals(u.get("id"))) {
                return (String) u.get("name");
            }
        }
        return "";
    }
}
