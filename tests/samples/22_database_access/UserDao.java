// UserDao.java — Maps raw row dictionaries onto strongly-typed user records.
// Part of the PolyglotCompiler sample matrix.

import java.util.ArrayList;
import java.util.List;
import java.util.Map;

public final class UserDao {
    public static final class User {
        public final long id;
        public final String name;
        public User(long id, String name) {
            this.id = id;
            this.name = name;
        }
    }

    public List<User> map_rows(List<Map<String, Object>> rows) {
        List<User> out = new ArrayList<>(rows.size());
        for (Map<String, Object> row : rows) {
            long id = ((Number) row.getOrDefault("id", 0)).longValue();
            String name = String.valueOf(row.getOrDefault("name", ""));
            out.add(new User(id, name));
        }
        return out;
    }
}

