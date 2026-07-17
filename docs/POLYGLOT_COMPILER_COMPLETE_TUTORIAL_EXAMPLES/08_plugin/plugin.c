#include "common/include/plugins/plugin_api.h"

#include <stdlib.h>

struct PolyglotPlugin {
    const PolyglotHostContext *context;
    const PolyglotHostServices *host;
};

static const PolyglotPluginInfo k_info = {
    POLYGLOT_PLUGIN_API_VERSION,
    "com.example.tutorial.hello",
    "Tutorial Hello Plugin",
    "1.0.0",
    "PolyglotCompiler tutorial",
    "Minimal lifecycle example",
    "MIT",
    NULL,
    POLYGLOT_CAP_NONE,
    "1.47.4"
};

POLYGLOT_EXPORT const PolyglotPluginInfo *polyglot_plugin_get_info(void) {
    return &k_info;
}

POLYGLOT_EXPORT PolyglotPlugin *polyglot_plugin_create(
    const PolyglotHostContext *context,
    const PolyglotHostServices *host) {
    PolyglotPlugin *plugin = (PolyglotPlugin *)malloc(sizeof(*plugin));
    if (plugin == NULL) {
        return NULL;
    }
    plugin->context = context;
    plugin->host = host;
    return plugin;
}

POLYGLOT_EXPORT void polyglot_plugin_destroy(PolyglotPlugin *plugin) {
    free(plugin);
}

POLYGLOT_EXPORT int polyglot_plugin_activate(PolyglotPlugin *plugin) {
    if (plugin != NULL && plugin->host != NULL && plugin->host->log != NULL) {
        plugin->host->log(plugin->context, POLYGLOT_LOG_INFO,
                          "tutorial plugin activated");
    }
    return 0;
}

POLYGLOT_EXPORT void polyglot_plugin_deactivate(PolyglotPlugin *plugin) {
    (void)plugin;
}

