// Fixture: Java source that requires Java 17 (records).
// `record` declarations became a stable language feature in Java 16
// (preview) and JDK 17 LTS.  PolyglotCompiler treats `record` as a
// Java-17 minimum.

public final class RecordInSeventeen {
    public record Point(int x, int y) { }

    public static int sum(Point p) {
        return p.x() + p.y();
    }
}
