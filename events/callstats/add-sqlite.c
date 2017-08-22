#include <stdio.h>
#include "data_store.h"

int main(void) {
    size_t res = initialize_db(); 
    if (res != 0) {
        printf("Error initializing database!\n");
    }
    
    user_info user;
    initialize_user_info(&user);
    user.user_id = "cm5";
    user.user_num = "1234";
    insert_userinfo(&user);

    res = close_db();
    if (res != 0) {
        printf("Error closing database!\n");
    }
    return 0;
}

