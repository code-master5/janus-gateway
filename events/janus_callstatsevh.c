/*! \file   janus_callstatsevh.c
 * \author Bimalkant Lauhny <lauhny.bimalk@gmail.com>
 * \copyright GNU General Public License v3
 * \brief  Janus CallstatsEventHandler plugin
 * \details  This is an event handler plugin for Janus.
 * This specific plugin forwards every event it receives
 * to a callstats.io REST API via an HTTP POST request, using libcurl.
 *
 * \ingroup eventhandlers
 * \ref eventhandlers
 */

#include "eventhandler.h"

#include <math.h>
#include <curl/curl.h>

#include "../debug.h"
#include "../config.h"
#include "../mutex.h"
#include "../utils.h"
#include "callstats/event_handlers.h"

/* Plugin information */
#define JANUS_CALLSTATSEVH_VERSION			1
#define JANUS_CALLSTATSEVH_VERSION_STRING	"0.0.1"
#define JANUS_CALLSTATSEVH_DESCRIPTION		"This is an event handler plugin for Janus, which forwards events to callstats via REST API."
#define JANUS_CALLSTATSEVH_NAME			"JANUS CallstatsEventHandler plugin"
#define JANUS_CALLSTATSEVH_AUTHOR			"Bimalkant Lauhny"
#define JANUS_CALLSTATSEVH_PACKAGE			"janus.eventhandler.callstatsevh"

/* Plugin methods */
janus_eventhandler *create(void);
int janus_callstatsevh_init(const char *config_path);
void janus_callstatsevh_destroy(void);
int janus_callstatsevh_get_api_compatibility(void);
int janus_callstatsevh_get_version(void);
const char *janus_callstatsevh_get_version_string(void);
const char *janus_callstatsevh_get_description(void);
const char *janus_callstatsevh_get_name(void);
const char *janus_callstatsevh_get_author(void);
const char *janus_callstatsevh_get_package(void);
void janus_callstatsevh_incoming_event(json_t *event);

/* Event handler setup */
static janus_eventhandler janus_callstatsevh =
	JANUS_EVENTHANDLER_INIT (
		.init = janus_callstatsevh_init,
		.destroy = janus_callstatsevh_destroy,

		.get_api_compatibility = janus_callstatsevh_get_api_compatibility,
		.get_version = janus_callstatsevh_get_version,
		.get_version_string = janus_callstatsevh_get_version_string,
		.get_description = janus_callstatsevh_get_description,
		.get_name = janus_callstatsevh_get_name,
		.get_author = janus_callstatsevh_get_author,
		.get_package = janus_callstatsevh_get_package,

		.incoming_event = janus_callstatsevh_incoming_event,

		.events_mask = JANUS_EVENT_TYPE_NONE
	);

/* Plugin creator */
janus_eventhandler *create(void) {
	JANUS_LOG(LOG_VERB, "%s created!\n", JANUS_CALLSTATSEVH_NAME);
	return &janus_callstatsevh;
}


/* Useful stuff */
static volatile gint initialized = 0, stopping = 0;
static GThread *handler_thread;
static void *janus_callstatsevh_handler(void *data);

/* Queue of events to handle */
static GAsyncQueue *events = NULL;
static gboolean group_events = FALSE;
static json_t exit_event;
static void janus_callstatsevh_event_free(json_t *event) {
	if(!event || event == &exit_event)
		return;
	json_decref(event);
}

/* Retransmission management */
static int max_retransmissions = 5;
static int retransmissions_backoff = 100;

/* Web backend to send the events to */
static char *backend = NULL;
static char *backend_user = NULL, *backend_pwd = NULL;

/* Plugin implementation */
int janus_callstatsevh_init(const char *config_path) {
	if(g_atomic_int_get(&stopping)) {
		/* Still stopping from before */
		return -1;
	}
	if(config_path == NULL) {
		/* Invalid arguments */
		return -1;
	}

	/* Read configuration */
	gboolean enabled = FALSE;
	char filename[255];
	g_snprintf(filename, 255, "%s/%s.cfg", config_path, JANUS_CALLSTATSEVH_PACKAGE);
	JANUS_LOG(LOG_VERB, "Configuration file: %s\n", filename);
	janus_config *config = janus_config_parse(filename);
	if(config != NULL) {
		/* Handle configuration */
		janus_config_print(config);

		/* Setup the callstats event handler, if required */
		janus_config_item *item = janus_config_get_item_drilldown(config, "general", "enabled");
		if(!item || !item->value || !janus_is_true(item->value)) {
			JANUS_LOG(LOG_WARN, "callstats event handler disabled (Janus API)\n");
		} else {
			/* Backend to send events to */
			item = janus_config_get_item_drilldown(config, "general", "backend");
			if(!item || !item->value || strstr(item->value, "http") != item->value) {
				JANUS_LOG(LOG_WARN, "Missing or invalid backend\n");
			} else {
				backend = g_strdup(item->value);
				/* Any credentials needed? */
				item = janus_config_get_item_drilldown(config, "general", "backend_user");
				backend_user = (item && item->value) ? g_strdup(item->value) : NULL;
				item = janus_config_get_item_drilldown(config, "general", "backend_pwd");
				backend_pwd = (item && item->value) ? g_strdup(item->value) : NULL;
				/* Any specific setting for retransmissions? */
				item = janus_config_get_item_drilldown(config, "general", "max_retransmissions");
				if(item && item->value) {
					int mr = atoi(item->value);
					if(mr < 0) {
						JANUS_LOG(LOG_WARN, "Invalid negative value for 'max_retransmissions', using default (%d)\n", max_retransmissions);
					} else if(mr == 0) {
						JANUS_LOG(LOG_WARN, "Retransmissions disabled (max_retransmissions=0)\n");
						max_retransmissions = 0;
					} else {
						max_retransmissions = mr;
					}
				}
				item = janus_config_get_item_drilldown(config, "general", "retransmissions_backoff");
				if(item && item->value) {
					int rb = atoi(item->value);
					if(rb <= 0) {
						JANUS_LOG(LOG_WARN, "Invalid negative or null value for 'retransmissions_backoff', using default (%d)\n", retransmissions_backoff);
					} else {
						retransmissions_backoff = rb;
					}
				}
				/* Which events should we subscribe to? */
				item = janus_config_get_item_drilldown(config, "general", "events");
				if(item && item->value) {
					if(!strcasecmp(item->value, "none")) {
						/* Don't subscribe to anything at all */
						janus_flags_reset(&janus_callstatsevh.events_mask);
					} else if(!strcasecmp(item->value, "all")) {
						/* Subscribe to everything */
						janus_flags_set(&janus_callstatsevh.events_mask, JANUS_EVENT_TYPE_ALL);
					} else {
						/* Check what we need to subscribe to */
						gchar **subscribe = g_strsplit(item->value, ",", -1);
						if(subscribe != NULL) {
							gchar *index = subscribe[0];
							if(index != NULL) {
								int i=0;
								while(index != NULL) {
									while(isspace(*index))
										index++;
									if(strlen(index)) {
										if(!strcasecmp(index, "sessions")) {
											janus_flags_set(&janus_callstatsevh.events_mask, JANUS_EVENT_TYPE_SESSION);
										} else if(!strcasecmp(index, "handles")) {
											janus_flags_set(&janus_callstatsevh.events_mask, JANUS_EVENT_TYPE_HANDLE);
										} else if(!strcasecmp(index, "jsep")) {
											janus_flags_set(&janus_callstatsevh.events_mask, JANUS_EVENT_TYPE_JSEP);
										} else if(!strcasecmp(index, "webrtc")) {
											janus_flags_set(&janus_callstatsevh.events_mask, JANUS_EVENT_TYPE_WEBRTC);
										} else if(!strcasecmp(index, "media")) {
											janus_flags_set(&janus_callstatsevh.events_mask, JANUS_EVENT_TYPE_MEDIA);
										} else if(!strcasecmp(index, "plugins")) {
											janus_flags_set(&janus_callstatsevh.events_mask, JANUS_EVENT_TYPE_PLUGIN);
										} else if(!strcasecmp(index, "transports")) {
											janus_flags_set(&janus_callstatsevh.events_mask, JANUS_EVENT_TYPE_TRANSPORT);
										} else if(!strcasecmp(index, "core")) {
											janus_flags_set(&janus_callstatsevh.events_mask, JANUS_EVENT_TYPE_CORE);
										} else {
											JANUS_LOG(LOG_WARN, "Unknown event type '%s'\n", index);
										}
									}
									i++;
									index = subscribe[i];
								}
							}
							g_strfreev(subscribe);
						}
					}
				}
				/* Is grouping of events ok? */
				item = janus_config_get_item_drilldown(config, "general", "grouping");
				group_events = item && item->value && janus_is_true(item->value);
				/* Done */
				enabled = TRUE;
			}
		}
	}

	janus_config_destroy(config);
	config = NULL;
	if(!enabled) {
		JANUS_LOG(LOG_FATAL, "Callstats event handler not enabled/needed, giving up...\n");
		return -1;	/* No point in keeping the plugin loaded */
	}
	JANUS_LOG(LOG_VERB, "Callstats event handler configured: %s\n", backend);

	/* Initialize libcurl, needed for forwarding events via HTTP POST */
	curl_global_init(CURL_GLOBAL_ALL);

	/* Initialize the events queue */
	events = g_async_queue_new_full((GDestroyNotify) janus_callstatsevh_event_free);

	g_atomic_int_set(&initialized, 1);

	/* Launch the thread that will handle incoming events */
	GError *error = NULL;
	handler_thread = g_thread_try_new("janus callstatsevh handler", janus_callstatsevh_handler, NULL, &error);
	if(error != NULL) {
		g_atomic_int_set(&initialized, 0);
		JANUS_LOG(LOG_ERR, "Got error %d (%s) trying to launch the CallstatsEventHandler handler thread...\n", error->code, error->message ? error->message : "??");
		return -1;
	}
	JANUS_LOG(LOG_INFO, "%s initialized!\n", JANUS_CALLSTATSEVH_NAME);
	return 0;
}

void janus_callstatsevh_destroy(void) {
	if(!g_atomic_int_get(&initialized))
		return;
	g_atomic_int_set(&stopping, 1);

	g_async_queue_push(events, &exit_event);
	if(handler_thread != NULL) {
		g_thread_join(handler_thread);
		handler_thread = NULL;
	}

	g_async_queue_unref(events);
	events = NULL;

	g_free(backend);

	g_atomic_int_set(&initialized, 0);
	g_atomic_int_set(&stopping, 0);
	JANUS_LOG(LOG_INFO, "%s destroyed!\n", JANUS_CALLSTATSEVH_NAME);
}

int janus_callstatsevh_get_api_compatibility(void) {
	/* Important! This is what your plugin MUST always return: don't lie here or bad things will happen */
	return JANUS_EVENTHANDLER_API_VERSION;
}

int janus_callstatsevh_get_version(void) {
	return JANUS_CALLSTATSEVH_VERSION;
}

const char *janus_callstatsevh_get_version_string(void) {
	return JANUS_CALLSTATSEVH_VERSION_STRING;
}

const char *janus_callstatsevh_get_description(void) {
	return JANUS_CALLSTATSEVH_DESCRIPTION;
}

const char *janus_callstatsevh_get_name(void) {
	return JANUS_CALLSTATSEVH_NAME;
}

const char *janus_callstatsevh_get_author(void) {
	return JANUS_CALLSTATSEVH_AUTHOR;
}

const char *janus_callstatsevh_get_package(void) {
	return JANUS_CALLSTATSEVH_PACKAGE;
}

void janus_callstatsevh_incoming_event(json_t *event) {
	if(g_atomic_int_get(&stopping) || !g_atomic_int_get(&initialized)) {
		/* Janus is closing or the plugin is: unref the event as we won't handle it */
		json_decref(event);
		return;
	}

	/* Do NOT handle the event here in this callback! Since Janus notifies you right
	 * away when something happens, these events are triggered from working threads and
	 * not some sort of message bus. As such, performing I/O or network operations in
	 * here could dangerously slow Janus down. Let's just reference and enqueue the event,
	 * and handle it in our own thread: the event contains a monotonic time indicator of
	 * when the event actually happened on this machine, so that, if relevant, we can compute
	 * any delay in the actual event processing ourselves. */
	json_incref(event);
	g_async_queue_push(events, event);

}


/* Thread to handle incoming events */
static void *janus_callstatsevh_handler(void *data) {
	JANUS_LOG(LOG_VERB, "Joining CallstatsEventHandler handler thread\n");
	json_t *event = NULL, *output = NULL;
	int count = 0, max = group_events ? 100 : 1;
	while(g_atomic_int_get(&initialized) && !g_atomic_int_get(&stopping)) {
        event = g_async_queue_pop(events);
        if(event == NULL)
            continue;
        if(event == &exit_event)
            break;
        count = 0;
        output = NULL;
        
        while(TRUE) {
            /* Handle event: just for fun, let's see how long it took for us to take care of this */
            json_t *created = json_object_get(event, "timestamp");
            if(created && json_is_integer(created)) {
                gint64 then = json_integer_value(created);
                gint64 now = janus_get_monotonic_time();
                JANUS_LOG(LOG_DBG, "Handled event after %"SCNu64" us\n", now-then);
            }
            
            /* Let's check what kind of event this is: we don't really do anything
             * with it in this plugin, it's just to show how you can handle
             * different types of events in an event handler. */
            int type = json_integer_value(json_object_get(event, "type"));
            switch(type) {
                case JANUS_EVENT_TYPE_SESSION:
                    session_eventhandler(event);
                    break;
                case JANUS_EVENT_TYPE_HANDLE:
                    handle_eventhandler(event);
                    break;
                case JANUS_EVENT_TYPE_JSEP:
                    jsep_eventhandler(event);
                    break;
                case JANUS_EVENT_TYPE_WEBRTC:
                    webrtc_eventhandler(event);
                    break;
                case JANUS_EVENT_TYPE_MEDIA:
                    media_eventhandler(event);
                    break;
                case JANUS_EVENT_TYPE_PLUGIN:
                    plugin_eventhandler(event);
                    break;
                case JANUS_EVENT_TYPE_TRANSPORT:
                    transport_eventhandler(event);
                    break;
                case JANUS_EVENT_TYPE_CORE:
                    core_eventhandler(event);
                    break;
                default:
                    JANUS_LOG(LOG_WARN, "Unknown type of event '%d'\n", type);
                    break;
            }
            if(!group_events) {
                /* We're done here, we just need a single event */
                output = event;
                break;
            }
            /* If we got here, we're grouping */
            if(output == NULL)
                output = json_array();
            json_array_append_new(output, event);
            /* Never group more than a maximum number of events, though, or we might stay here forever */
            count++;
            if(count == max)
                break;
            event = g_async_queue_try_pop(events);
            if(event == NULL || event == &exit_event)
                break;
        }
        json_decref(output);
        output = NULL;
	}
	JANUS_LOG(LOG_VERB, "Leaving CallstatsEventHandler handler thread\n");
	return NULL;
}
