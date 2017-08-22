#include <stdio.h>
#include "event_handlers.h"

json_t *get_core_event(const char *status) {
    json_t *core = json_object();
    json_object_set_new(core, "type", json_integer(256));
    json_object_set_new(core, "timestamp", json_integer(1500625586169884));

    json_t *event = json_object();
    json_object_set_new(event, "status", json_string(status));

    json_object_set_new(core, "event", event);
    
    char *temp = json_dumps(core, JSON_INDENT(3));
    printf("Core Event created: %s\n", temp);
    
    free(temp);
    return core;
}

json_t *get_session_event(const char *event_name) {
    json_t *session = json_object();
    json_object_set_new(session, "type", json_integer(256));
    json_object_set_new(session, "timestamp", json_integer(1500625586169884));
    json_object_set_new(session, "session_id", json_integer(96435475150506));

    json_t *event = json_object();
    json_object_set_new(event, "name", json_string(event_name));

    json_object_set_new(session, "event", event);
    
    char *temp = json_dumps(session, JSON_INDENT(3));
    printf("Session Event created: %s\n", temp);
    
    free(temp);
    return session;
}

json_t* get_handle_event() {
    json_t *handle = json_object();
    json_object_set_new(handle, "type", json_integer(2));
    json_object_set_new(handle, "timestamp", json_integer(1502456339500));
    json_object_set_new(handle, "session_id", json_integer(96435475150506));
    json_object_set_new(handle, "handle_id", json_integer(4620600987866721));

    json_t *event = json_object();
    json_object_set_new(event, "name", json_string("attached"));
    json_object_set_new(event, "plugin", json_string("janus.plugin.videoroom"));
    
    json_t *opaque_id = json_object();
    json_object_set_new(opaque_id, "userID", json_string("bimal"));
    json_object_set_new(opaque_id, "deviceID", json_string("P6eDzoDArQki"));
    json_object_set_new(opaque_id, "confID", json_string("Demo Room"));
    json_object_set_new(opaque_id, "confNum", json_integer(1234));
    
    char *temp = json_dumps(opaque_id, 0);
    json_object_set_new(event, "opaque_id", json_string(temp));

    json_object_set_new(handle, "event", event);
    
    free(temp);
    temp = json_dumps(handle, JSON_INDENT(3));
    printf("Handle Event Created: %s\n", temp);
    
    free(temp);
    json_decref(opaque_id);
    return handle;
}

json_t* get_plugin_event(char *type) {
    json_t *plugin= json_object();
    json_object_set_new(plugin, "type", json_integer(64));
    json_object_set_new(plugin, "timestamp", json_integer(1502468442423));
    json_object_set_new(plugin, "session_id", json_integer(96435475150506));
    json_object_set_new(plugin, "handle_id", json_integer(4620600987866721));

    json_t *event = json_object();
    json_object_set_new(event, "plugin", json_string("janus.plugin.videoroom"));

    json_t *data= json_object();
    json_object_set_new(data, "event", json_string(type));
    json_object_set_new(data, "room", json_integer(1234));
    json_object_set_new(data, "id", json_integer(404525542925394));
    json_object_set_new(data, "private_id", json_integer(981250313));
    json_object_set_new(data, "display", json_string("bimal"));

    json_object_set_new(event, "data", data);

    json_object_set_new(plugin, "event", event);
    
    char *temp = json_dumps(plugin, JSON_INDENT(3));
    printf("Plugin Event Created: %s\n", temp);
    
    free(temp);
    return plugin;
}

int main(void) {
    
    json_t *core = get_core_event("started");
    core_eventhandler(core);
    json_decref(core);
    core = NULL;

    json_t *session = get_session_event("created");
    session_eventhandler(session);
    json_decref(session);
    session = NULL;
    
    json_t *handle = get_handle_event();
    handle_eventhandler(handle);
    json_decref(handle);

    json_t *plugin = get_plugin_event("joined");
    plugin_eventhandler(plugin);
    json_decref(plugin);
    plugin = NULL;
    
    plugin = get_plugin_event("unpublished");
    plugin_eventhandler(plugin);
    json_decref(plugin);

    session = get_session_event("destroyed");
    session_eventhandler(session);
    json_decref(session);

    core = get_core_event("shutdown");
    core_eventhandler(core);
    json_decref(core);

    return 0;
}
