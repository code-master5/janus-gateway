/*! \file    event_handlers.h
 * \author   Bimalkant Lauhny <lauhny.bimalk@gmail.com>
 * \copyright MIT License
 * \brief    Methods for handling events received from Janus
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <jansson.h>
#include <pthread.h>
#include "data_store.h"
#include "callstats.h"
#include "../../utils.h"

#define BUFFER_SIZE_EVH 100

// helper functions prototypes

char *to_string(gint64);
char *without_spaces(char *);

// event handler function prototypes

void session_eventhandler(json_t *);
void handle_eventhandler(json_t *);
void jsep_eventhandler(json_t *);
void webrtc_eventhandler(json_t *);
void media_eventhandler(json_t *);
void plugin_eventhandler(json_t *);
void transport_eventhandler(json_t *);
void core_eventhandler(json_t *);


// helper functions definitions

// a function to convert a number to string
char *to_string(gint64 num) {
    char *result  = (char *) malloc(BUFFER_SIZE_EVH);
    sprintf(result, "%li", num);
    return result;
}

// a function to replace spaces in a string with '-'
char *without_spaces(char *old_str) {
    char *c_old = old_str;
    char *new_str = (char *) malloc(strlen(old_str)+1);
    char *c_new = new_str;
    gint8 spaces = 0;
    while(*c_old) {
        if (*c_old == ' ') {
            if (spaces == 0) {
                *c_new = '-';
                spaces++;
                c_new++;
            }
            c_old++;
            continue;
        }
        spaces = 0;
        *c_new = *c_old;
        c_new++;
        c_old++;
    }
    *c_new = '\0';
    return new_str;
}

// event handler definitions

// event handler for 'session' events (type: 1)
void session_eventhandler(json_t *event) {
    // extracting session_id
    json_t *sid =  json_object_get(event, "session_id");
    gint64 session_id = (gint64) json_number_value(sid);
    printf("session id : %li\n", session_id);

    //extracting event details from 'event'
    json_t *event_key = json_object_get(event, "event");

    // extracting event name ("attached" or "detached")
    json_t *evn = json_object_get(event_key, "name");
    const char *event_name = json_string_value(evn);
    printf("event name: %s\n", event_name);
}

// event handler for 'handle' events (type: 2)
void handle_eventhandler(json_t *event) {
    // never free json_t* we get from json_object_get(), since we receive a borrowed reference

    // extracting session_id
    json_t *sid =  json_object_get(event, "session_id");
    gint64 session_id = (gint64) json_number_value(sid);
    printf("session id : %li\n", session_id);

    // extracting handle_id
    json_t *hid = json_object_get(event, "handle_id");
    gint64 handle_id = (gint64) json_number_value(hid);
    printf("handle id : %li\n", handle_id);

    //extracting event details from 'event'
    json_t *event_key = json_object_get(event, "event");

    // extracting event name ("attached" or "detached")
    json_t *evn = json_object_get(event_key, "name");
    const char *event_name = json_string_value(evn);
    printf("event name: %s\n", event_name);
    if (strcmp(event_name, "attached") == 0) {
        // extracting opaque_id
        json_t *jstr_oid = json_object_get(event_key, "opaque_id");
        if (jstr_oid == NULL) {
            printf("This event does not contain opaque_id. Ignoring!!");
            return;
        }
        const char *str_oid = json_string_value(jstr_oid);
        printf("opaque_id string: %s\n", str_oid);
        json_error_t err;
        json_t *opaque_id = json_loads(str_oid, 0, &err);

        // from opaque_id we get -

        // user_id
        json_t *uid = json_object_get(opaque_id, "user");
        char *user_id = (char *) json_string_value(uid);
        printf("user id : %s\n", user_id);

        // conf_id
        json_t *cid = json_object_get(opaque_id, "roomDesc");
        char *conf_id = (char *) json_string_value(cid);
        printf("conf id : %s\n", conf_id);

        // conf_num
        json_t *cnum = json_object_get(opaque_id, "roomId");
        gint64 conf_num = (gint64)json_integer_value(cnum);
        printf("conf num : %li\n", conf_num);

        // device_id
        json_t *did = json_object_get(opaque_id, "deviceId");
        char *device_id = (char *) json_string_value(did);
        printf("device id: %s\n", device_id);


        // setting up data for storing into data store
        // def - data_store.h
        user_info user;

        // initializing user
        // def - data_store.h
        initialize_user_info(&user);

        // assigning values to user
        user.user_id = user_id;
        // since conf_id is part of post request urls, we will remove spaces from it
        // def - event_handlers.h
        user.conf_id = without_spaces(conf_id);
        // conf_num, session_id and handle_id are numbers, we need to convert
        // these to strings before storing into database
        // def - event_handlers.h
        user.conf_num = to_string(conf_num);
        user.device_id = device_id;
        user.session_id = to_string(session_id);
        user.handle_id = to_string(handle_id);

        // storing user info in data store
        // def - data_store.h
        insert_userinfo(&user);

        // get auth token for the user
        // def - callstats.h
        char *token = callstats_authenticate(user_id);

        // add token to user
        // def - data_store.h
        add_token(user.session_id, user.handle_id, token);

        // freeing up data after data is stored successfully
        free(user.conf_id);
        free(user.conf_num);
        free(user.session_id);
        free(user.handle_id);
        free(token);
        printf(":::Here:::\n");
        json_decref(opaque_id);
    } else if(strcmp(event_name, "detached") == 0) {

    }
}

void jsep_eventhandler(json_t *event) {

}

void webrtc_eventhandler(json_t *event) {

}


void media_eventhandler(json_t *event) {

}

gpointer *user_alive(gpointer user) {
    user_info s_user = (user_info) user;
    gint8 rows = get_user_info(s_user.session_id,
                               s_user.handle_id,
                               &s_user);
    while(rows > 0) {
        // Starting user_alive event
        // def - callstats.h
        gint8 rc = callstats_user_alive(s_user, janus_get_real_time());
        if (rc == TRUE) {
            printf("SUCCESS: userAlive successfull!\n");
            g_usleep(10 * 1000000);
        } else {
            printf("ERROR: userAlive failed!\n");
            g_usleep(1000000);
        }
        //fetch user info for a combination of session_id and handle_id from data store
        // def - data_store.h
        rows = get_user_info(s_user.session_id,
                             s_user.handle_id,
                             &s_user);
    }
    // free the memory allocated to user info in 'user'
    // def - data_store.h
    free_user_info(&s_user);
}

// event handler for 'plugin' events (type: 64)
void plugin_eventhandler(json_t *event) {
    // extracting session_id
    json_t *sid =  json_object_get(event, "session_id");
    gint64 sess_id = (gint64) json_number_value(sid);
    printf("session id : %li\n", sess_id);
    char *session_id = to_string(sess_id);

    // extracting handle_id
    json_t *hid = json_object_get(event, "handle_id");
    gint64 hand_id = (gint64) json_number_value(hid);
    printf("handle id : %li\n", hand_id);
    char *handle_id = to_string(hand_id);

    // extracting timestamp
    json_t *t_stamp = json_object_get(event, "timestamp");
    gint64 timestamp = (gint64) json_number_value(t_stamp);

    //extracting event details from 'event'
    json_t *event_key = json_object_get(event, "event");

    // extracting 'data' from 'event'
    json_t *data= json_object_get(event_key, "data");

    // extracting event name from 'data'
    json_t *evn = json_object_get(data, "event");
    const char *event_name = json_string_value(evn);
    printf("event name: %s\n", event_name);

    if (strcmp(event_name, "joined") == 0) {
        // extracting 'user_num'
        json_t *unum = json_object_get(data, "id");
        gint64 usr_num = (gint64)json_integer_value(unum);
        printf("user num : %li\n", usr_num);
        char *user_num = to_string(usr_num);

        // storing user_num in data store
        // def - data_store.h
        gboolean rc = add_user_num(session_id, handle_id, user_num);
        if (rc == FALSE) {
            printf("ERROR: Failed adding user_num!\n");
        }
        free(user_num);
        user_info user;
        // initializing user fields with NULL
        // def - data_store.h
        initialize_user_info(&user);

        //fetch user info for a combination of session_id and handle_id from data store
        // def - data_store.h
        gint8 rows = get_user_info(session_id, handle_id, &user);
        printf("joined: No of rows found: %d\n", rows);
        // send user-join request to callstats REST API
        // def - callstats.h
        char *uc_id  = callstats_user_joined(&user, timestamp);

        // Adding uc_id to data store
        // def - data-store.h
        rc = add_uc_id(session_id, handle_id, uc_id);
        if (rc == FALSE) {
            printf("ERROR: Failed adding uc_id!\n");
        }
        free(uc_id);

        // create a user_alive thread
        GError *error = NULL;
        GThread *thread = g_thread_try_new(NULL, user_alive, (gpointer)user, &error);
        if (error == NULL) {
            printf("\nCan't create User Alive thread :[%s]", strerror(error));
        } else {
            printf("\n User Alive thread created successfully\n");
        }
    } else if (strcmp(event_name, "unpublished") == 0) {
        user_info user;
        // initializing user fields with NULL
        // def - data_store.h
        initialize_user_info(&user);

        //fetch user info for a combination of session_id and handle_id from data store
        // def - data_store.h
        gint8 rows = get_user_info(session_id, handle_id, &user);
        printf("unpublished: No of rows found before deleting: %d\n", rows);
        // send user-left request to callstats REST API
        // def - callstats.h
        gboolean status = callstats_user_left(&user, timestamp);
        if (status == TRUE) {
            printf("Successfully sent user left.\n");
        } else {
            printf("user left request unsuccessful!.\n");
        }
        // free the memory allocated to user info in 'user'
        // def - data_store.h
        free_user_info(&user);
        // user has left, delete the related info in database
        // def - data_store.h
        remove_user(session_id, handle_id);
        // def - data_store.h
    }

    free(session_id);
    free(handle_id);
}

void transport_eventhandler(json_t *event) {

}

// event handler for 'core' events (type: 256)
void core_eventhandler(json_t *event) {
    json_t *event_key = json_object_get(event, "event");
    json_t *json_status = json_object_get(event_key, "status");
    const char *status = json_string_value(json_status);
    if (strcmp(status, "started") == 0) {
      // initializing database
      initialize_db();
    } else if (strcmp(status, "shutdown") == 0){
      // closing database
      close_db();
    }
}

