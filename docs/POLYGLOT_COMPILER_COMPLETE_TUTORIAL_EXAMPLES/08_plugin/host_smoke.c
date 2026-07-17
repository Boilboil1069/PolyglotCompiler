#include "common/include/plugins/plugin_api.h"

#include <stdio.h>

extern const PolyglotPluginInfo *polyglot_plugin_get_info(void);
extern PolyglotPlugin *polyglot_plugin_create(
    const PolyglotHostContext *context,
    const PolyglotHostServices *host);
extern int polyglot_plugin_activate(PolyglotPlugin *plugin);
extern void polyglot_plugin_deactivate(PolyglotPlugin *plugin);
extern void polyglot_plugin_destroy(PolyglotPlugin *plugin);

static void host_log(const PolyglotHostContext *context,
                     PolyglotLogLevel level,
                     const char *message) {
    (void)context;
    printf("log[%d]=%s\n", (int)level, message == NULL ? "<null>" : message);
}

int main(void) {
    const PolyglotPluginInfo *info = polyglot_plugin_get_info();
    if (info == NULL || info->api_version != POLYGLOT_PLUGIN_API_VERSION) {
        return 2;
    }

    printf("plugin=%s version=%s\n", info->id, info->version);

    PolyglotHostServices host = {0};
    host.log = host_log;

    PolyglotPlugin *plugin = polyglot_plugin_create(NULL, &host);
    if (plugin == NULL) {
        return 3;
    }

    int result = polyglot_plugin_activate(plugin);
    printf("activate=%d\n", result);
    polyglot_plugin_deactivate(plugin);
    polyglot_plugin_destroy(plugin);
    return result;
}
