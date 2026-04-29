// plugin_host.cpp — Tiny plugin host indexing handlers by integer id.
// Part of the PolyglotCompiler sample matrix.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

extern "C" {

typedef int (*PluginHandler)(const char *);

namespace {
PluginHandler g_handlers[8] = {nullptr};
size_t        g_count       = 0;
}

int polyglot_register(PluginHandler handler) {
    if (g_count >= 8) return -1;
    g_handlers[g_count] = handler;
    return static_cast<int>(g_count++);
}

int polyglot_invoke(int id, const char *payload) {
    if (id < 0 || static_cast<size_t>(id) >= g_count) return -1;
    if (!g_handlers[id]) return -1;
    return g_handlers[id](payload ? payload : "");
}


}  // extern "C"
