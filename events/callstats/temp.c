#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


int callback(void*, int, char **, char **);


int main(void) {
    
    sqlite3 *db;
    char *err_msg = 0;
    
    int rc = sqlite3_open("test.db", &db);
    
    if (rc != SQLITE_OK) {
        
        fprintf(stderr, "Cannot open database: %s\n", 
                sqlite3_errmsg(db));
        sqlite3_close(db);
        
        return 1;
    }
    
    char *sql = "SELECT * FROM Stats_Info WHERE session_id='96435475150506' AND handle_id='4620600987866721';";
    sqlite3_stmt *stmt;
    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        printf("error: ", sqlite3_errmsg(db));
        return 0;
    }
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const char *name = sqlite3_column_text(stmt, 1);
        printf("data: %s\n", name);
    }
    if (rc != SQLITE_DONE) {
        printf("error: ", sqlite3_errmsg(db));
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    free(NULL);
    return 0;
}

int callback(void *res, int argc, char **argv, 
                    char **azColName) {
        
    printf("hey man!");
    
    for (int i = 0; i < argc; i++) {

        printf("%s = %s\n", azColName[i], argv[i] ? argv[i] : "NULL");
    }
    printf("\n");
    strcpy(res,azColName[0]);
    return 0;
}
