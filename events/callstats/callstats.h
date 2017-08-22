/*! \file    callstats.h
 * \author   Bimalkant Lauhny <lauhny.bimalk@gmail.com>
 * \copyright MIT License
 * \brief    Methods for sending data to callstats.io REST API
 * using sqlite3
 */

#include <string.h>
#include <curl/curl.h>
#include <jansson.h>
#include <jwt.h>
#include "jwt_provider.h"
#include "config.h"

#define CALLSTATS_REST_API_VERSION 1.0.0
#define BUFFER_SIZE  (10 * 1024)  /* 10 KB */

// function prototypes
static gint64 write_response(void *, gint64, gint64, void *);
char *callstats_authenticate(char *);
char *callstats_user_joined (user_info *, gint64);
gboolean callstats_user_alive(user_info *, gint64);
gboolean callstats_user_left(user_info *, gint64);
gboolean callstats_fabric_setup(user_info *, gint64);
gboolean callstats_conf_stats(user_info *, gint64);

// function definitions

struct write_result {
    char *data;
    gint64 pos;
};

typedef struct write_result write_result; 

static gint64 write_response(void *ptr, gint64 size, gint64 nmemb, void *stream) {
    write_result *result = (struct write_result *)stream;
    
    if(result->pos + size * nmemb >= BUFFER_SIZE - 1)
    {
        fprintf(stderr, "error: too small buffer\n");
        return 0;
    }
    
    memcpy(result->data + result->pos, ptr, size * nmemb);
    result->pos += size * nmemb;
    
    return size * nmemb;
}

// function definitions

char *callstats_authenticate(char *user_id) {
    // getting private key
    char *private_key = jwt_load_private_key(PRIVATE_KEY_PATH);
    if (private_key == NULL) {
        perror("ERROR reading private_key!");
        goto error;
    }
    
    // generating Json Web Token
    char *jwt_string = jwt_get_token(private_key, KEY_ID, APP_ID, user_id);
    if (jwt_string == NULL) {
        perror("ERROR generating token!");
        goto error;
    }
    printf("Recieved JWT: %s\n", jwt_string);
    
    const char* url = "https://auth.callstats.io/authenticate";
    CURL *curl = NULL;
    CURLcode status;
    struct curl_slist *headers = NULL;
    char *data = NULL;
    long code;
    
    json_error_t err;
    // response will later store server's response
    json_t *response = NULL;
    
    curl_global_init(CURL_GLOBAL_ALL);
    curl = curl_easy_init();
    if(!curl)
        goto error;
    
    data = malloc(BUFFER_SIZE);
    if(!data)
        goto error;
    
    write_result res = {
        .data = data,
        .pos = 0
    };
    
    // set url
    curl_easy_setopt(curl, CURLOPT_URL, url);
    // set HTTP version
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2_0);
    // append headers
    headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");
    // set headers
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    // form format
    const char *form_format= "grant_type=authorization_code&code=%s&client_id=%s@%s";
    // form data buffer 
    char form_data_buffer [BUFFER_SIZE];
    snprintf(form_data_buffer, sizeof(form_data_buffer), form_format, jwt_string, user_id, APP_ID);
    //printf("Form Data Buffer: %s\n", form_data_buffer);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, form_data_buffer);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_response);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &res);
    // perform request
    status = curl_easy_perform(curl);
    if(status != 0){
        fprintf(stderr, "error: unable to request data from %s:\n", url);
        fprintf(stderr, "%s\n", curl_easy_strerror(status));
        goto error;
    }
    
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    if(code != 200) {
        fprintf(stderr, "error: server responded with code %ld\n", code);
        goto error;
    }
    
    /* zero-terminate the result */
    data[res.pos] = '\0';
    
    response = json_loads(data, 0, &err);
    
    if (response == NULL) {
        perror("JSON parsing error!");
        goto error;
    }
    
    json_t *json_access_token = json_object_get(response, "access_token");
    
    if (json_access_token == NULL) {
        perror("ERROR");
        goto error;
    }
    
    const char *val = json_string_value(json_access_token);
    char *access_token = (char *) malloc(strlen(val) + 1);
    strcpy(access_token, val);
    //printf("Access Token Converted: %s\n", access_token);
    
    error:
    if (response) 
        json_decref(response);
    if(data)
        free(data);
    if(curl)
        curl_easy_cleanup(curl);
    if(headers)
        curl_slist_free_all(headers);
    curl_global_cleanup();
    if (jwt_string)
        free(jwt_string);
    if (private_key)
        free(private_key);
    
    return access_token;
}

char *callstats_user_joined (user_info *user, gint64 timestamp) {
    // preparing url for posting payload
    const char* url = "https://events.callstats.io/v1/apps/%s/conferences/%s";
    char url_buffer[BUFFER_SIZE];
    snprintf(url_buffer, BUFFER_SIZE, url, APP_ID, user->conf_id);
    //printf("callstats_user_joined Buffer: %s\n", url_buffer);
    
    // preparing payload
    json_t *json_payload = json_object();
    json_object_set_new(json_payload, "localID", json_string(user->user_id));
    json_object_set_new(json_payload, "deviceID", json_string(user->device_id));
    json_object_set_new(json_payload, "timestamp", json_real(timestamp/1000.0));
    json_t *endpoint_info = json_object();
    json_object_set_new(endpoint_info, "type", json_string("middlebox"));
    json_object_set_new(endpoint_info, "buildName", json_string("Janus"));
    json_object_set_new(endpoint_info, "buildVersion", json_string(JANUS_VERSION));
    json_object_set_new(endpoint_info, "appVersion", json_string(JANGOUTS_VERSION));
    json_object_set_new(json_payload, "endpointInfo", endpoint_info);
    
    // stringifying payload
    char *string_payload = json_dumps(json_payload, JSON_ENCODE_ANY); 
    printf("user_joined payload: %s \n", string_payload);
    
    CURL *curl = NULL;                                      
    CURLcode status;
    struct curl_slist *headers = NULL;
    char *data = NULL;
    long code = 0;
    
    json_error_t err;
    // response will later store server's response
    json_t *response = NULL;
    
    curl_global_init(CURL_GLOBAL_ALL);
    curl = curl_easy_init();
    if(!curl)
        goto error;
    
    data = malloc(BUFFER_SIZE);
    if(!data)
        goto error;
    
    write_result res = {
        .data = data,
        .pos = 0
    };
    
    // set url
    curl_easy_setopt(curl, CURLOPT_URL, url_buffer);
    // set HTTP version
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2_0);
    // set key and certificate
    curl_easy_setopt(curl, CURLOPT_SSLCERTTYPE, "PEM");
    curl_easy_setopt(curl, CURLOPT_SSLCERT, SERVER_CERT_PATH);
    curl_easy_setopt(curl, CURLOPT_SSLKEYTYPE, "PEM");
    curl_easy_setopt(curl, CURLOPT_SSLKEY, SERVER_KEY_PATH);
    // append headers
    headers = curl_slist_append(headers, "Accept: application/json");
    headers = curl_slist_append(headers, "Content-Type: application/json");
    const char *auth_format = "Authorization: Bearer %s";
    char auth_string[BUFFER_SIZE];
    snprintf(auth_string, BUFFER_SIZE, auth_format, user->token);
    //printf("authorization: %s\n", auth_string);
    headers = curl_slist_append(headers, auth_string);
    // set headers
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    // form format
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "POST");
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, string_payload);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, strlen(string_payload) + 1);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_response);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &res);
    // perform request
    status = curl_easy_perform(curl);
    if(status != 0){
        fprintf(stderr, "ERROR: unable to request data from %s:\n", url_buffer);
        fprintf(stderr, "ERROR:%s\n", curl_easy_strerror(status));
        goto error;
    }
    
    // zero-terminate the result
    data[res.pos] = '\0';
    printf("user_joined response: %s\n", data);
    
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    if(code != 200) {
        fprintf(stderr, "ERROR: server responded with code %ld\n", code);
        goto error;
    }
    
    
    response = json_loads(data, 0, &err);
    
    if (response == NULL) {
        fprintf(stderr, "JSON parsing error!");
        goto error;
    }
    
    json_t *json_uc_id = json_object_get(response, "ucID");
    
    if (json_uc_id == NULL) {
        perror("ERROR");
        goto error;
    }
    
    const char *str_uc_id = json_string_value(json_uc_id);
    char *uc_id = (char *) malloc(strlen(str_uc_id) + 1);
    if (uc_id == NULL) {
        printf("ERROR: Could not allocate memory! Leaving user_joined."); 
        goto error;
    }
    strcpy(uc_id, str_uc_id);
    printf("Received uc_id: %s\n", uc_id);
    
    error:
    
    if (response) 
        json_decref(response);
    if(data)
        free(data);
    if(curl)
        curl_easy_cleanup(curl);
    if(headers)
        curl_slist_free_all(headers);
    curl_global_cleanup();
    if (string_payload)
        free(string_payload);
    if (json_payload)
        json_decref(json_payload);
    
    return uc_id;
}

gboolean callstats_user_alive(user_info *user, gint64 timestamp) {
    // preparing url for posting payload
    
    const char* url = "https://events.callstats.io/v1/apps/%s/conferences/%s/%s/events/user/alive";
    char url_buffer[BUFFER_SIZE];
    snprintf(url_buffer, BUFFER_SIZE, url, APP_ID, user->conf_id, user->uc_id);
    //printf("callstats_user_alive Buffer: %s\n", url_buffer);
    
    // preparing payload
    json_t *json_payload = json_object();
    json_object_set_new(json_payload, "localID", json_string(user->user_id));
    json_object_set_new(json_payload, "deviceID", json_string(user->device_id));
    json_object_set_new(json_payload, "timestamp", json_real(timestamp/1000.0));
    
    // stringifying payload
    char *string_payload = json_dumps(json_payload, JSON_ENCODE_ANY); 
    printf("user_alive payload: %s \n", string_payload);
    
    CURL *curl = NULL;                                      
    CURLcode status;
    struct curl_slist *headers = NULL;
    char *data = NULL;
    long code = 0;
    
    // msg=TRUE means success, msg=FALSE means failure
    gboolean msg = FALSE;
    
    json_error_t err;
    
    // response will later store server's response
    json_t *response = NULL;
    
    curl_global_init(CURL_GLOBAL_ALL);
    curl = curl_easy_init();
    if(!curl)
        goto error;
    
    data = malloc(BUFFER_SIZE);
    if(!data)
        goto error;
    
    write_result res = {
        .data = data,
        .pos = 0
    };
    
    // set url
    curl_easy_setopt(curl, CURLOPT_URL, url_buffer);
    // set HTTP version
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2_0);
    // set key and certificate
    curl_easy_setopt(curl, CURLOPT_SSLCERTTYPE, "PEM");
    curl_easy_setopt(curl, CURLOPT_SSLCERT, SERVER_CERT_PATH);
    curl_easy_setopt(curl, CURLOPT_SSLKEYTYPE, "PEM");
    curl_easy_setopt(curl, CURLOPT_SSLKEY, SERVER_KEY_PATH);
    
    // append headers
    headers = curl_slist_append(headers, "Accept: application/json");
    headers = curl_slist_append(headers, "Content-Type: application/json");
    const char *auth_format = "Authorization: Bearer %s";
    char auth_string[BUFFER_SIZE];
    snprintf(auth_string, BUFFER_SIZE, auth_format, user->token);
    //printf("authorization: %s\n", auth_string);
    headers = curl_slist_append(headers, auth_string);
    // set headers
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    // form format
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "POST");
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, string_payload);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, strlen(string_payload)+1);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_response);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &res);
    // perform request
    status = curl_easy_perform(curl);
    if(status != 0){
        fprintf(stderr, "ERROR: unable to request data from %s:\n", url_buffer);
        fprintf(stderr, "ERROR: %s\n", curl_easy_strerror(status));
        goto error;
    }
    
    // zero-terminate the result
    data[res.pos] = '\0';
    printf("user_alive Response: %s\n", data);
    
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    if(code != 200) {
        fprintf(stderr, "ERROR: server responded with code %ld\n", code);
        goto error;
    }
    
    response = json_loads(data, 0, &err);
    
    if (response == NULL) {
        fprintf(stderr, "JSON parsing error!");
        goto error;
    }
    
    json_t *json_status = json_object_get(response, "status");
    
    if (json_status == NULL) {
        perror("ERROR");
        goto error;
    }
    
    const char *result = json_string_value(json_status);
    if (result == NULL) {
        printf("ERROR: Cannot get 'success' key! Leaving user_left."); 
        goto error;
    }
    
    if (strcmp(result , "success") == 0) {
        msg = TRUE; 
    }
    
    error:
    
    if (string_payload)
        free(string_payload);
    if (json_payload)
        json_decref(json_payload);
    if (response) 
        json_decref(response);
    if(data)
        free(data);
    if(curl)
        curl_easy_cleanup(curl);
    if(headers)
        curl_slist_free_all(headers);
    curl_global_cleanup();
    
    return msg;
}

gboolean callstats_user_left(user_info *user, gint64 timestamp) {
    // preparing url for posting payload
    
    const char* url = "https://events.callstats.io/v1/apps/%s/conferences/%s/%s/events/user/left";
    char url_buffer[BUFFER_SIZE];
    snprintf(url_buffer, BUFFER_SIZE, url, APP_ID, user->conf_id, user->uc_id);
    //printf("callstats_user_left Buffer: %s\n", url_buffer);
    
    // preparing payload
    json_t *json_payload = json_object();
    json_object_set_new(json_payload, "localID", json_string(user->user_id));
    json_object_set_new(json_payload, "deviceID", json_string(user->device_id));
    json_object_set_new(json_payload, "timestamp", json_real(timestamp/1000.0));
    
    // stringifying payload
    char *string_payload = json_dumps(json_payload, JSON_ENCODE_ANY); 
    printf("user_left payload: %s \n", string_payload);
    
    CURL *curl = NULL;                                      
    CURLcode status;
    struct curl_slist *headers = NULL;
    char *data = NULL;
    long code = 0;
    
    // msg=TRUE means success, msg=FALSE means failure
    gboolean msg = FALSE;
    
    json_error_t err;
    
    // response will later store server's response
    json_t *response = NULL;
    
    curl_global_init(CURL_GLOBAL_ALL);
    curl = curl_easy_init();
    if(!curl)
        goto error;
    
    data = malloc(BUFFER_SIZE);
    if(!data)
        goto error;
    
    write_result res = {
        .data = data,
        .pos = 0
    };
    
    // set url
    curl_easy_setopt(curl, CURLOPT_URL, url_buffer);
    // set HTTP version
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2_0);
    // set key and certificate
    curl_easy_setopt(curl, CURLOPT_SSLCERTTYPE, "PEM");
    curl_easy_setopt(curl, CURLOPT_SSLCERT, SERVER_CERT_PATH);
    curl_easy_setopt(curl, CURLOPT_SSLKEYTYPE, "PEM");
    curl_easy_setopt(curl, CURLOPT_SSLKEY, SERVER_KEY_PATH);
    
    // append headers
    headers = curl_slist_append(headers, "Accept: application/json");
    headers = curl_slist_append(headers, "Content-Type: application/json");
    const char *auth_format = "Authorization: Bearer %s";
    char auth_string[BUFFER_SIZE];
    snprintf(auth_string, BUFFER_SIZE, auth_format, user->token);
    //printf("authorization: %s\n", auth_string);
    headers = curl_slist_append(headers, auth_string);
    // set headers
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    // form format
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "POST");
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, string_payload);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, strlen(string_payload)+1);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_response);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &res);
    // perform request
    status = curl_easy_perform(curl);
    if(status != 0){
        fprintf(stderr, "ERROR: unable to request data from %s:\n", url_buffer);
        fprintf(stderr, "ERROR: %s\n", curl_easy_strerror(status));
        goto error;
    }
    
    // zero-terminate the result
    data[res.pos] = '\0';
    printf("user_left Response: %s\n", data);
    
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    if(code != 200) {
        fprintf(stderr, "ERROR: server responded with code %ld\n", code);
        goto error;
    }
    
    response = json_loads(data, 0, &err);
    
    if (response == NULL) {
        fprintf(stderr, "JSON parsing error!");
        goto error;
    }
    
    json_t *json_status = json_object_get(response, "status");
    
    if (json_status == NULL) {
        perror("ERROR");
        goto error;
    }
    
    const char *result = json_string_value(json_status);
    if (result == NULL) {
        printf("ERROR: Cannot get 'success' key! Leaving user_left."); 
        goto error;
    }
    
    if (strcmp(result , "success") == 0) {
        msg = TRUE;
    }
    
    error:
    
    if (string_payload)
        free(string_payload);
    if (json_payload)
        json_decref(json_payload);
    if (response) 
        json_decref(response);
    if(data)
        free(data);
    if(curl)
        curl_easy_cleanup(curl);
    if(headers)
        curl_slist_free_all(headers);
    curl_global_cleanup();
    
    return msg;
}   

gboolean callstats_fabric_setup(user_info *user, gint64 timestamp) {
    // preparing url for posting payload
    const char* url = "https://events.callstats.io/v1/apps/%s/conferences/%s/%s/events/fabric";
    char url_buffer[BUFFER_SIZE];
    snprintf(url_buffer, BUFFER_SIZE, url, APP_ID, user->conf_id, user->uc_id);
    //printf("callstats_fabric_setup Buffer: %s\n", url_buffer);
    
    // preparing payload
    json_t *json_payload = json_object();
    json_object_set_new(json_payload, "localID", json_string(user->user_id));
    json_object_set_new(json_payload, "deviceID", json_string(user->device_id));
    json_object_set_new(json_payload, "timestamp", json_real(timestamp/1000.0));
    json_object_set_new(json_payload, "remoteID", json_string("Janus"));
    json_object_set_new(json_payload, "connectionID", json_string(user->uc_id));
    json_object_set_new(json_payload, "eventType", json_string("fabricSetup"));
    
    // stringifying payload
    char *string_payload = json_dumps(json_payload, JSON_ENCODE_ANY); 
    printf("fabric_setup payload: %s \n", string_payload);
    
    CURL *curl = NULL;                                      
    CURLcode status;
    struct curl_slist *headers = NULL;
    char *data = NULL;
    long code = 0;
    
    // msg=TRUE means success, msg=FALSE means failure
    gboolean msg = FALSE;
    
    json_error_t err;
    
    // response will later store server's response
    json_t *response = NULL;
    
    curl_global_init(CURL_GLOBAL_ALL);
    curl = curl_easy_init();
    if(!curl)
        goto error;
    
    data = malloc(BUFFER_SIZE);
    if(!data)
        goto error;
    
    write_result res = {
        .data = data,
        .pos = 0
    };
    
    // set url
    curl_easy_setopt(curl, CURLOPT_URL, url_buffer);
    // set HTTP version
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2_0);
    // set key and certificate
    curl_easy_setopt(curl, CURLOPT_SSLCERTTYPE, "PEM");
    curl_easy_setopt(curl, CURLOPT_SSLCERT, SERVER_CERT_PATH);
    curl_easy_setopt(curl, CURLOPT_SSLKEYTYPE, "PEM");
    curl_easy_setopt(curl, CURLOPT_SSLKEY, SERVER_KEY_PATH);
    
    // append headers
    headers = curl_slist_append(headers, "Accept: application/json");
    headers = curl_slist_append(headers, "Content-Type: application/json");
    const char *auth_format = "Authorization: Bearer %s";
    char auth_string[BUFFER_SIZE];
    snprintf(auth_string, BUFFER_SIZE, auth_format, user->token);
    //printf("authorization: %s\n", auth_string);
    headers = curl_slist_append(headers, auth_string);
    // set headers
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    // form format
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "POST");
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, string_payload);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, strlen(string_payload+1));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_response);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &res);
    // perform request
    status = curl_easy_perform(curl);
    if(status != 0){
        fprintf(stderr, "ERROR: unable to request data from %s:\n", url_buffer);
        fprintf(stderr, "ERROR: %s\n", curl_easy_strerror(status));
        goto error;
    }
    
    // zero-terminate the result
    data[res.pos] = '\0';
    printf("user_left Response: %s\n", data);
    
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    if(code != 200) {
        fprintf(stderr, "ERROR: server responded with code %ld\n", code);
        goto error;
    }
    
    response = json_loads(data, 0, &err);
    
    if (response == NULL) {
        fprintf(stderr, "JSON parsing error!");
        goto error;
    }
    
    json_t *json_status = json_object_get(response, "status");
    
    if (json_status == NULL) {
        perror("ERROR");
        goto error;
    }
    
    const char *result = json_string_value(json_status);
    if (result == NULL) {
        printf("ERROR: Cannot get 'success' key! Leaving fabric_setup."); 
        goto error;
    }
    
    if (strcmp(result , "success") == 0) {
        msg = TRUE; 
    }
    
    error:
    
    if (string_payload)
        free(string_payload);
    if (json_payload)
        json_decref(json_payload);
    if (response) 
        json_decref(response);
    if(data)
        free(data);
    if(curl)
        curl_easy_cleanup(curl);
    if(headers)
        curl_slist_free_all(headers);
    curl_global_cleanup();
    
    return msg;
} 

gboolean callstats_conf_stats(user_info *user, gint64 timestamp) {
    // preparing url for posting payload
    
    const char* url = "https://stats.callstats.io/v1/apps/%s/conferences/%s/%s/events/stats";
    char url_buffer[BUFFER_SIZE];
    snprintf(url_buffer, BUFFER_SIZE, url, APP_ID, user->conf_id, user->uc_id);
    //printf("callstats_conf_stats Buffer: %s\n", url_buffer);
    
    // preparing payload
    json_t *json_payload = json_object();
    json_object_set_new(json_payload, "localID", json_string(user->user_id));
    json_object_set_new(json_payload, "deviceID", json_string(user->device_id));
    json_object_set_new(json_payload, "timestamp", json_real(timestamp/1000.0));
    json_object_set_new(json_payload, "remoteID", json_string("Janus"));
    json_object_set_new(json_payload, "connectionID", json_string(user->uc_id));
    
    // stringifying payload
    char *string_payload = json_dumps(json_payload, JSON_ENCODE_ANY); 
    printf("conf_stats payload: %s \n", string_payload);
    
    CURL *curl = NULL;                                      
    CURLcode status;
    struct curl_slist *headers = NULL;
    char *data = NULL;
    long code = 0;
    
    // msg=TRUE means success, msg=FALSE means failure
    gboolean msg = FALSE;
    json_error_t err;
    
    // response will later store server's response
    json_t *response = NULL;
    
    curl_global_init(CURL_GLOBAL_ALL);
    curl = curl_easy_init();
    if(!curl)
        goto error;
    
    data = malloc(BUFFER_SIZE);
    if(!data)
        goto error;
    
    write_result res = {
        .data = data,
        .pos = 0
    };
    
    // set url
    curl_easy_setopt(curl, CURLOPT_URL, url_buffer);
    // set HTTP version
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2_0);
    // set key and certificate
    curl_easy_setopt(curl, CURLOPT_SSLCERTTYPE, "PEM");
    curl_easy_setopt(curl, CURLOPT_SSLCERT, SERVER_CERT_PATH);
    curl_easy_setopt(curl, CURLOPT_SSLKEYTYPE, "PEM");
    curl_easy_setopt(curl, CURLOPT_SSLKEY, SERVER_KEY_PATH);
    
    // append headers
    headers = curl_slist_append(headers, "Accept: application/json");
    headers = curl_slist_append(headers, "Content-Type: application/json");
    const char *auth_format = "Authorization: Bearer %s";
    char auth_string[BUFFER_SIZE];
    snprintf(auth_string, BUFFER_SIZE, auth_format, user->token);
//     printf("authorization: %s\n", auth_string);
    headers = curl_slist_append(headers, auth_string);
    // set headers
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    // form format
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "POST");
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, string_payload);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, strlen(string_payload) + 1);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_response);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &res);
    // perform request
    status = curl_easy_perform(curl);
    if(status != 0){
        fprintf(stderr, "ERROR: unable to request data from %s:\n", url_buffer);
        fprintf(stderr, "ERROR: %s\n", curl_easy_strerror(status));
        goto error;
    }
    
    // zero-terminate the result
    data[res.pos] = '\0';
    printf("user_left Response: %s\n", data);
    
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    if(code != 200) {
        fprintf(stderr, "ERROR: server responded with code %ld\n", code);
        goto error;
    }
    
    response = json_loads(data, 0, &err);
    
    if (response == NULL) {
        fprintf(stderr, "JSON parsing error!");
        goto error;
    }
    
    json_t *json_status = json_object_get(response, "status");
    
    if (json_status == NULL) {
        perror("ERROR");
        goto error;
    }
    
    const char *result = json_string_value(json_status);
    if (result == NULL) {
        printf("ERROR: Cannot get 'success' key! Leaving conf_stats."); 
        goto error;
    }
    
    if (strcmp(result , "success") == 0) {
        msg = TRUE; 
    }
    
    error:
    
    if (string_payload)
        free(string_payload);
    if (json_payload)
        json_decref(json_payload);
    if (response) 
        json_decref(response);
    if(data)
        free(data);
    if(curl)
        curl_easy_cleanup(curl);
    if(headers)
        curl_slist_free_all(headers);
    curl_global_cleanup();
    
    return msg;
}
