/*! \file    data_store.h
 * \author   Bimalkant Lauhny <lauhny.bimalk@gmail.com>
 * \copyright MIT License
 * \brief    Methods for storing data related to user in an in-memory database
 * using sqlite3
 */

#include <sqlite3.h>
#include <stdio.h>
#include "config.h"

#define BUFFER_SIZE_SQLITE 10*1024

static sqlite3 *db = NULL;
static char *err_msg = NULL;

struct user_info {
  char *user_num;
  char *user_id;
  char *conf_num;
  char *conf_id;
  char *device_id;
  char *session_id;
  char *handle_id;
  char *audio_ssrc;
  char *video_ssrc;
  char *local_candidate;
  char *remote_candidate;
  char *uc_id;
  char *token;
};

typedef struct user_info user_info;

// function prototypes
void initialize_user_info(user_info *user);
void free_user_info(user_info *user);
gboolean initialize_db(void);
gboolean insert_userinfo(user_info *);
gboolean add_token(char *, char *, char *);
gboolean add_user_num(char *, char *, char *);
gint8 get_user_info(char *, char *, user_info *);
gboolean remove_user(char *, char *);
gboolean close_db(void);


//function definitions    

void initialize_user_info(user_info *user) {
    user->user_num= NULL;
    user->user_id = NULL;
    user->conf_num= NULL;
    user->conf_id = NULL;
    user->device_id = NULL;
    user->session_id = NULL;
    user->handle_id = NULL;
    user->audio_ssrc = NULL;
    user->video_ssrc= NULL;
    user->local_candidate = NULL;
    user->remote_candidate = NULL;
    user->uc_id = NULL;
    user->token = NULL;
}

void free_user_info(user_info *user) {
    free(user->user_num);
    free(user->user_id);
    free(user->conf_num);
    free(user->conf_id);
    free(user->device_id);
    free(user->session_id);
    free(user->handle_id);
    free(user->audio_ssrc);
    free(user->video_ssrc);
    free(user->local_candidate);
    free(user->remote_candidate);
    free(user->uc_id);
    free(user->token);
}

gboolean initialize_db(void) {
    if (db != NULL) {
        return 0;
    }
    gint8 rc = sqlite3_open(DB_PATH, &db); 
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
        sqlite3_free(err_msg);
        sqlite3_close(db);
        return FALSE;
    }
    
    const char *sql = "DROP TABLE IF EXISTS Stats_Info;"
                "CREATE TABLE Stats_Info(user_num TEXT," 
                                         "user_id TEXT," 
                                         "conf_num TEXT," 
                                         "conf_id TEXT,"
                                         "device_id TEXT,"
                                         "session_id TEXT,"
                                         "handle_id TEXT,"
                                         "audio_ssrc TEXT,"
                                         "video_ssrc TEXT,"
                                         "local_candidate TEXT,"
                                         "remote_candidate TEXT,"
                                         "uc_id TEXT,"
                                         "token TEXT);";
                                         
    rc = sqlite3_exec(db, sql, 0, 0, &err_msg);
    if (rc != SQLITE_OK ) {
        fprintf(stderr, "SQL error: %s\n", err_msg);
        sqlite3_free(err_msg);        
        sqlite3_close(db);
        return FALSE;
    } 
    return TRUE;
}

gboolean insert_userinfo(user_info *user) {
    const char *sql = "INSERT INTO Stats_Info VALUES ('%s', '%s', '%s', '%s', '%s', '%s', '%s', '%s', '%s', '%s', '%s', '%s', '%s');";
    char sql_buffer[BUFFER_SIZE_SQLITE];
    snprintf(sql_buffer, sizeof(sql_buffer), sql, 
            (user->user_num)?user->user_num:"NULL",
            (user->user_id)?user->user_id:"NULL",
            (user->conf_num)?user->conf_num:"NULL",
            (user->conf_id)?user->conf_id:"NULL",
            (user->device_id)?user->device_id:"NULL",
            (user->session_id)?user->session_id:"NULL",
            (user->handle_id)?user->handle_id:"NULL",
            (user->audio_ssrc)?user->audio_ssrc:"NULL",
            (user->video_ssrc)?user->video_ssrc:"NULL",
            (user->local_candidate)?user->local_candidate:"NULL",
            (user->remote_candidate)?user->remote_candidate:"NULL",
            (user->uc_id)?user->uc_id:"NULL",
            (user->token)?user->token:"NULL");
    printf("Buffer: %s\n", sql_buffer);
    gint8 rc = sqlite3_exec(db, sql_buffer, 0, 0, &err_msg);
    if (rc != SQLITE_OK ) {
        fprintf(stderr, "SQL error: %s\n", err_msg);
        sqlite3_free(err_msg);        
        sqlite3_close(db);
        return FALSE;
    } 
    return TRUE; 
}

gboolean add_token(char *session_id, char *handle_id, char *token) {
    const char *sql = "UPDATE Stats_Info SET token='%s' "
        "WHERE session_id='%s' AND handle_id='%s';";
    char sql_buffer[BUFFER_SIZE_SQLITE];
    snprintf(sql_buffer, sizeof(sql_buffer), sql, 
            token, session_id, handle_id);
    printf("Buffer: %s\n", sql_buffer);
    gint8 rc = sqlite3_exec(db, sql_buffer, 0, 0, &err_msg);
    if (rc != SQLITE_OK ) {
        fprintf(stderr, "SQL error: %s\n", err_msg);
        sqlite3_free(err_msg);        
        sqlite3_close(db);
        return FALSE;
    } 
    return TRUE; 
}

gboolean add_uc_id(char *session_id, char *handle_id, char *uc_id) {
    const char *sql = "UPDATE Stats_Info SET uc_id='%s' "
        "WHERE session_id='%s' AND handle_id='%s';";
    char sql_buffer[BUFFER_SIZE_SQLITE];
    snprintf(sql_buffer, sizeof(sql_buffer), sql, 
            uc_id, session_id, handle_id);
    printf("Buffer: %s\n", sql_buffer);
    gint8 rc = sqlite3_exec(db, sql_buffer, 0, 0, &err_msg);
    if (rc != SQLITE_OK ) {
        fprintf(stderr, "SQL error: %s\n", err_msg);
        sqlite3_free(err_msg);        
        sqlite3_close(db);
        return FALSE;
    } 
    return TRUE; 
}

gboolean add_user_num(char *session_id, char *handle_id, char *user_num) {
    const char *sql = "UPDATE Stats_Info SET user_num='%s' "
        "WHERE session_id='%s' AND handle_id='%s';";
    char sql_buffer[BUFFER_SIZE_SQLITE];
    snprintf(sql_buffer, sizeof(sql_buffer), sql, 
            user_num, session_id, handle_id);
    printf("Buffer: %s\n", sql_buffer);
    gboolean rc = sqlite3_exec(db, sql_buffer, 0, 0, &err_msg);
    if (rc != SQLITE_OK ) {
        fprintf(stderr, "SQL error: %s\n", err_msg);
        sqlite3_free(err_msg);        
        sqlite3_close(db);
        return FALSE;
    } 
    return TRUE; 
}

gint8 get_user_info(char *session_id, char *handle_id, user_info *user) {
    const char *sql = "SELECT * FROM Stats_Info WHERE session_id='%s' AND handle_id='%s';";
    char sql_buffer[BUFFER_SIZE_SQLITE];
    snprintf(sql_buffer, sizeof(sql_buffer), sql, 
            session_id, handle_id);
    printf("Buffer: %s\n", sql_buffer);
    sqlite3_stmt *stmt;
    gint8 rc = sqlite3_prepare_v2(db, sql_buffer, -1, &stmt, NULL);
    if (rc != SQLITE_OK ) {
        
        fprintf(stderr, "Failed to prepare SQL statement.\n");
        fprintf(stderr, "SQL error: %s\n", err_msg);
        
        sqlite3_free(err_msg);
        sqlite3_close(db);
        
        return -1;
    }  
    gint8 rows = 0;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        ++rows;
        
        const char *res = sqlite3_column_text(stmt, 0);
        user->user_num = malloc(strlen(res) + 1);
        strcpy(user->user_num, res);
        
        res = sqlite3_column_text(stmt, 1);
        user->user_id = malloc(strlen(res) + 1);
        strcpy(user->user_id, res);
        
        res = sqlite3_column_text(stmt, 2);
        user->conf_num= malloc(strlen(res) + 1);
        strcpy(user->conf_num, res);
        
        res = sqlite3_column_text(stmt, 3);
        user->conf_id = malloc(strlen(res) + 1);
        strcpy(user->conf_id, res);
        
        res = sqlite3_column_text(stmt, 4);
        user->device_id = malloc(strlen(res) + 1);
        strcpy(user->device_id, res);
        
        res = sqlite3_column_text(stmt, 5);
        user->session_id = malloc(strlen(res) + 1);
        strcpy(user->session_id, res);
            
        
        res = sqlite3_column_text(stmt, 6);
        user->handle_id = malloc(strlen(res) + 1);
        strcpy(user->handle_id, res);
        
        res = sqlite3_column_text(stmt, 7);
        user->audio_ssrc = malloc(strlen(res) + 1);
        strcpy(user->audio_ssrc, res);
        
        res = sqlite3_column_text(stmt, 8);
        user->video_ssrc = malloc(strlen(res) + 1);
        strcpy(user->video_ssrc, res);
        
        res = sqlite3_column_text(stmt, 9);
        user->local_candidate = malloc(strlen(res) + 1);
        strcpy(user->local_candidate, res);
        
        res = sqlite3_column_text(stmt, 10);
        user->remote_candidate = malloc(strlen(res) + 1);
        strcpy(user->remote_candidate, res);
        
        res = sqlite3_column_text(stmt, 11);
        user->uc_id = malloc(strlen(res) + 1);
        strcpy(user->uc_id, res);
        
        res = sqlite3_column_text(stmt, 12);
        user->token = malloc(strlen(res) + 1);
        strcpy(user->token, res);
    }
    
    if (rc != SQLITE_DONE) {
        fprintf(stderr, "Failed to prepare SQL statement.\n");
        fprintf(stderr, "SQL error: %s\n", err_msg);
        
        sqlite3_free(err_msg);
        sqlite3_close(db);
        
        return -1;
    }
    
    sqlite3_finalize(stmt);
    
    return rows;
}

gboolean remove_user(char *session_id, char *handle_id) {
    const char *sql = "DELETE FROM Stats_Info "
                      "WHERE session_id='%s' AND handle_id='%s';";
    char sql_buffer[BUFFER_SIZE_SQLITE];
    snprintf(sql_buffer, sizeof(sql_buffer), sql, 
             session_id, handle_id);
    printf("Buffer: %s\n", sql_buffer);
    gint8 rc = sqlite3_exec(db, sql_buffer, 0, 0, &err_msg);
    if (rc != SQLITE_OK ) {
        fprintf(stderr, "SQL error: %s\n", err_msg);
        sqlite3_free(err_msg);        
        sqlite3_close(db);
        return FALSE;
    } 
    return TRUE; 
}

gboolean close_db(void) {
    gboolean rc = sqlite3_close(db);
    if (rc != SQLITE_OK ) {
        sqlite3_free(err_msg);
        fprintf(stderr, "SQL error: %s\n", err_msg);
        return -1;
    } 
    sqlite3_free(err_msg);
    return 0;
}
