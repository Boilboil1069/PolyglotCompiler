// FsmRunner.java — Iterative FSM driver bounded by an explicit step budget.
// Part of the PolyglotCompiler sample matrix.

public final class FsmRunner {
    public static int run(int initial, int[] events, int maxSteps) {
        int state = initial;
        int steps = Math.min(events.length, maxSteps);
        for (int i = 0; i < steps; ++i) {
            state = nextState(state, events[i]);
        }
        return state;
    }

    public static int nextState(int current, int event) {
        if (current == 0 && event == 0) return 1;
        if (current == 1 && event == 1) return 0;
        return current;
    }
}

