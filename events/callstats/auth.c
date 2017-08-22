#include <stdio.h>
#include <string.h>
#include "callstats.h"
#include <jwt.h>
#include <jansson.h>
#include <curl/curl.h>

int main() {
    char *token = authenticate("Janus");
    if (token == NULL) { 
       perror("ERROR auth!");
    }
    printf("Recieved Response: %s\n", token);
    free(token);
    return 0;
}
