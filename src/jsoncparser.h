#ifndef JSONCPARSER_H
#define JSONCPARSER_H

#include "../config.h"

#if HAVE_JSONC

typedef struct jsonc_json_data jsonc_json_data;

#include <json-c/json.h>
#include <json-c/json_tokener.h>

struct jsonc_json_data {
    /* json-c configuration and parser state */
    struct json_tokener *tokener;
    struct json_object *obj;
    enum json_tokener_error error;

    /* prefix is used to create data hierarchy (i.e., 'parent.child.value') */
    char          *prefix;                   // cppcheck-suppress unusedStructMember
    size_t         prefix_len;               // cppcheck-suppress unusedStructMember
    char          *current_key;              // cppcheck-suppress unusedStructMember
    size_t         current_key_len;          // cppcheck-suppress unusedStructMember
    long int       current_depth;            // cppcheck-suppress unusedStructMember
    long int       depth_limit;              // cppcheck-suppress unusedStructMember
    int            depth_limit_exceeded;     // cppcheck-suppress unusedStructMember
    long int       current_arg_num;          // cppcheck-suppress unusedStructMember
    long int       arg_num_limit;            // cppcheck-suppress unusedStructMember
    int            arg_num_limit_exceeded;   // cppcheck-suppress unusedStructMember
    int            silence;                  // cppcheck-suppress unusedStructMember

    /* dynamic buffer for storing results */
    char          *result_buffer;            // cppcheck-suppress unusedStructMember
    size_t         result_buffer_size;       // cppcheck-suppress unusedStructMember
    size_t         result_buffer_capacity;   // cppcheck-suppress unusedStructMember

    /* dynamic buffer for storing error messages */
    char          *error_buffer;             // cppcheck-suppress unusedStructMember
    size_t         error_buffer_size;        // cppcheck-suppress unusedStructMember
    size_t         error_buffer_capacity;    // cppcheck-suppress unusedStructMember
};

int jsonc_json_init(jsonc_json_data **json, char **error_msg);

void jsonc_json_allow_partial(jsonc_json_data *json);

int jsonc_json_process_chunk(jsonc_json_data *json, const char *buf,
        unsigned int size, char **error_msg);

int jsonc_json_complete(jsonc_json_data *json, char **error_msg);

int jsonc_json_cleanup(jsonc_json_data *json);

#endif

#endif
