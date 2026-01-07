#include "jsoncparser.h"

#if HAVE_JSONC

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifndef JSON_STRING_SIZE
#define JSON_STRING_SIZE 8192
#endif

static void json_add_error(jsonc_json_data *p, const char *error_msg)
{
    assert(p != NULL);
    assert(error_msg != NULL);

    size_t error_len = strlen(error_msg);
    size_t needed_size = p->error_buffer_size + error_len + 1; /* +1 for newline */

    if (needed_size > p->error_buffer_capacity) {
        /* Grow buffer - double the capacity or use needed size, whichever is larger */
        size_t new_capacity = p->error_buffer_capacity * 2;
        if (new_capacity < needed_size) {
            new_capacity = needed_size;
        }
        char *new_buffer = realloc(p->error_buffer, new_capacity);
        if (new_buffer == NULL) {
            return; /* Silently fail if we can't allocate memory */
        }
        p->error_buffer = new_buffer;
        p->error_buffer_capacity = new_capacity;
    }

    /* Append "error_msg\n" to buffer */
    int written = snprintf(p->error_buffer + p->error_buffer_size,
                          p->error_buffer_capacity - p->error_buffer_size,
                          "%s\n", error_msg);
    if (written > 0) {
        p->error_buffer_size += written;
    }
}

static int json_add_argument(jsonc_json_data *p, const char *key, const char *value)
{
    assert(p != NULL);

    if (p->silence) {
        return 1;
    }

    char argname[JSON_STRING_SIZE];

    /**
     * Argument name is 'prefix + key'
     */
    if (p->prefix_len > 0 && key != NULL) {
        if (p->prefix_len + 1 + strlen(key) >= JSON_STRING_SIZE) {
            json_add_error(p, "Argument name too long");
            return 0;
        }
        snprintf(argname, sizeof(argname), "%s.%s", p->prefix, key);
    }
    else if (key != NULL) {
        if (strlen(key) >= JSON_STRING_SIZE) {
            json_add_error(p, "Argument name too long");
            return 0;
        }
        strncpy(argname, key, sizeof(argname) - 1);
        argname[sizeof(argname) - 1] = '\0';
    }
    else {
        if (p->prefix_len >= JSON_STRING_SIZE) {
            json_add_error(p, "Argument name too long");
            return 0;
        }
        strncpy(argname, p->prefix, sizeof(argname) - 1);
        argname[sizeof(argname) - 1] = '\0';
    }

    /* Append to result buffer instead of printing to stdout */
    size_t argname_len = strlen(argname);
    size_t argval_len = value ? strlen(value) : 0;
    size_t needed_size = argname_len + 2 + argval_len + 1; /* "name: value\n" */

    if (p->result_buffer_size + needed_size > p->result_buffer_capacity) {
        /* Grow buffer - double the capacity or use needed size, whichever is larger */
        size_t new_capacity = p->result_buffer_capacity * 2;
        if (new_capacity < p->result_buffer_size + needed_size) {
            new_capacity = p->result_buffer_size + needed_size;
        }
        char *new_buffer = realloc(p->result_buffer, new_capacity);
        if (new_buffer == NULL) {
            json_add_error(p, "Failed to allocate memory for result buffer");
            return 0;
        }
        p->result_buffer = new_buffer;
        p->result_buffer_capacity = new_capacity;
    }

    /* Append "argname: argval\n" to buffer */
    int written = snprintf(p->result_buffer + p->result_buffer_size,
                          p->result_buffer_capacity - p->result_buffer_size,
                          "%s: %s\n", argname, value ? value : "");
    if (written > 0) {
        p->result_buffer_size += written;
    }

    p->current_arg_num++;
    if (p->current_arg_num > p->arg_num_limit) {
        p->arg_num_limit_exceeded = 1;
        return 0;
    }

    return 1;
}

static int process_json_object(jsonc_json_data *p, struct json_object *obj, const char *key);

static int process_json_value(jsonc_json_data *p, struct json_object *obj, const char *key)
{
    enum json_type type;

    if (obj == NULL) {
        return json_add_argument(p, key, "");
    }

    type = json_object_get_type(obj);

    switch (type) {
        case json_type_null:
            return json_add_argument(p, key, "");

        case json_type_boolean:
            return json_add_argument(p, key, json_object_get_boolean(obj) ? "true" : "false");

        case json_type_double:
        case json_type_int: {
            const char *str = json_object_get_string(obj);
            return json_add_argument(p, key, str);
        }

        case json_type_string: {
            const char *str = json_object_get_string(obj);
            return json_add_argument(p, key, str);
        }

        case json_type_object:
        case json_type_array:
            return process_json_object(p, obj, key);

        default:
            json_add_error(p, "Unknown JSON type");
            return 0;
    }
}

static int process_json_object(jsonc_json_data *p, struct json_object *obj, const char *key)
{
    enum json_type type;
    char old_prefix[JSON_STRING_SIZE];
    size_t old_prefix_len;

    if (obj == NULL) {
        return 1;
    }

    /* Save old prefix */
    old_prefix_len = p->prefix_len;
    if (old_prefix_len > 0) {
        strncpy(old_prefix, p->prefix, sizeof(old_prefix) - 1);
        old_prefix[sizeof(old_prefix) - 1] = '\0';
    }

    /* Update prefix with current key */
    if (key != NULL) {
        if (p->prefix_len > 0) {
            snprintf(p->prefix + p->prefix_len, JSON_STRING_SIZE - p->prefix_len, ".%s", key);
            p->prefix_len = strlen(p->prefix);
        } else {
            strncpy(p->prefix, key, JSON_STRING_SIZE - 1);
            p->prefix[JSON_STRING_SIZE - 1] = '\0';
            p->prefix_len = strlen(p->prefix);
        }
    }

    type = json_object_get_type(obj);

    p->current_depth++;
    if (p->current_depth > p->depth_limit) {
        p->depth_limit_exceeded = 1;
        p->current_depth--;
        /* Restore prefix */
        if (old_prefix_len > 0) {
            strncpy(p->prefix, old_prefix, sizeof(p->prefix) - 1);
            p->prefix[sizeof(p->prefix) - 1] = '\0';
        } else {
            p->prefix[0] = '\0';
        }
        p->prefix_len = old_prefix_len;
        return 0;
    }

    if (type == json_type_object) {
        struct json_object_iterator it;
        struct json_object_iterator itEnd;

        it = json_object_iter_begin(obj);
        itEnd = json_object_iter_end(obj);

        while (!json_object_iter_equal(&it, &itEnd)) {
            const char *field_key = json_object_iter_peek_name(&it);
            struct json_object *field_val = json_object_iter_peek_value(&it);

            if (!process_json_value(p, field_val, field_key)) {
                p->current_depth--;
                /* Restore prefix */
                if (old_prefix_len > 0) {
                    strncpy(p->prefix, old_prefix, sizeof(p->prefix) - 1);
                    p->prefix[sizeof(p->prefix) - 1] = '\0';
                } else {
                    p->prefix[0] = '\0';
                }
                p->prefix_len = old_prefix_len;
                return 0;
            }

            json_object_iter_next(&it);
        }
    }
    else if (type == json_type_array) {
        size_t array_len = json_object_array_length(obj);

        for (size_t i = 0; i < array_len; i++) {
            struct json_object *item = json_object_array_get_idx(obj, i);

            if (!process_json_value(p, item, NULL)) {
                p->current_depth--;
                /* Restore prefix */
                if (old_prefix_len > 0) {
                    strncpy(p->prefix, old_prefix, sizeof(p->prefix) - 1);
                    p->prefix[sizeof(p->prefix) - 1] = '\0';
                } else {
                    p->prefix[0] = '\0';
                }
                p->prefix_len = old_prefix_len;
                return 0;
            }
        }
    }

    p->current_depth--;

    /* Restore prefix */
    if (old_prefix_len > 0) {
        strncpy(p->prefix, old_prefix, sizeof(p->prefix) - 1);
        p->prefix[sizeof(p->prefix) - 1] = '\0';
    } else {
        p->prefix[0] = '\0';
    }
    p->prefix_len = old_prefix_len;

    return 1;
}

/**
 * Initialise JSON parser.
 */
int jsonc_json_init(jsonc_json_data **json, char **error_msg) {
    assert(error_msg != NULL);

    *error_msg = NULL;

    (*json) = malloc(sizeof(jsonc_json_data));
    if (*json == NULL) return -1;

    /**
     * Prefix and current key are initially empty
     */
    (*json)->prefix = (char *) malloc(JSON_STRING_SIZE * sizeof(char));
    if ((*json)->prefix == NULL) return -1;
    memset((*json)->prefix, 0, JSON_STRING_SIZE);
    (*json)->prefix_len = 0;

    (*json)->current_key = (char *) malloc(JSON_STRING_SIZE * sizeof(char));
    if ((*json)->current_key == NULL) return -1;
    memset((*json)->current_key, 0, JSON_STRING_SIZE);
    (*json)->current_key_len = 0;

    (*json)->current_depth          = 0;
    (*json)->depth_limit            = 0;
    (*json)->depth_limit_exceeded   = 0;
    (*json)->current_arg_num        = 0;
    (*json)->arg_num_limit          = 0;
    (*json)->arg_num_limit_exceeded = 0;
    (*json)->silence                = 0;

    /**
     * Initialize result buffer with initial capacity
     */
    (*json)->result_buffer_capacity = 4096;  /* Start with 4KB */
    (*json)->result_buffer = (char *) malloc((*json)->result_buffer_capacity);
    if ((*json)->result_buffer == NULL) return -1;
    (*json)->result_buffer[0] = '\0';
    (*json)->result_buffer_size = 0;

    /**
     * Initialize error buffer with initial capacity
     */
    (*json)->error_buffer_capacity = 4096;  /* Start with 4KB */
    (*json)->error_buffer = (char *) malloc((*json)->error_buffer_capacity);
    if ((*json)->error_buffer == NULL) return -1;
    (*json)->error_buffer[0] = '\0';
    (*json)->error_buffer_size = 0;

    /**
     * json-c initialization
     */
    (*json)->tokener = json_tokener_new();
    if ((*json)->tokener == NULL) {
        *error_msg = strdup("Failed to create json_tokener");
        return -1;
    }

    (*json)->obj = NULL;
    (*json)->error = json_tokener_success;

    return 1;
}

/**
 * Allow partial processing of JSON data.
 * Note: json-c supports partial parsing by default through json_tokener_parse_ex
 */
void jsonc_json_allow_partial(jsonc_json_data *json) {
    /* json-c supports partial parsing by default, no specific flag needed */
    (void)json; /* Suppress unused parameter warning */
}

/**
 * Feed one chunk of data to the JSON parser.
 */
int jsonc_json_process_chunk(jsonc_json_data *json, const char *buf, unsigned int size, char **error_msg) {
    assert(json != NULL);
    assert(error_msg != NULL);
    *error_msg = NULL;

    /* Parse the chunk */
    json->obj = json_tokener_parse_ex(json->tokener, buf, size);
    json->error = json_tokener_get_error(json->tokener);

    if (json->error == json_tokener_continue) {
        /* Parsing is incomplete, need more data */
        return 1;
    }
    else if (json->error != json_tokener_success) {
        /* Parse error */
        if (json->depth_limit_exceeded != 0 || json->arg_num_limit_exceeded != 0) {
            if (json->depth_limit_exceeded != 0) {
                const char * err = "JSON depth limit exceeded";
                *error_msg = strdup(err);
                json_add_error(json, err);
            }
            if (json->arg_num_limit_exceeded != 0) {
                const char * err = "ARGUMENT number limit exceeded";
                if (*error_msg == NULL) {
                    *error_msg = strdup(err);
                }
                json_add_error(json, err);
            }
        }
        else {
            const char *err = json_tokener_error_desc(json->error);
            *error_msg = strdup(err);
            json_add_error(json, err);
        }
        return -1;
    }

    /* Successfully parsed, process the object */
    if (json->obj != NULL) {
        if (!process_json_value(json, json->obj, NULL)) {
            if (json->depth_limit_exceeded != 0) {
                const char * err = "JSON depth limit exceeded";
                *error_msg = strdup(err);
                json_add_error(json, err);
            }
            if (json->arg_num_limit_exceeded != 0) {
                const char * err = "ARGUMENT number limit exceeded";
                if (*error_msg == NULL) {
                    *error_msg = strdup(err);
                }
                json_add_error(json, err);
            }
            return -1;
        }
    }

    return 1;
}

/**
 * Complete the JSON parsing and check if the input was valid and complete.
 */
int jsonc_json_complete(jsonc_json_data *json, char **error_msg) {
    assert(json != NULL);
    assert(error_msg != NULL);
    *error_msg = NULL;

    /* Check if we have a complete object */
    if (json->obj == NULL && json->error == json_tokener_continue) {
        const char *err = "Incomplete JSON input";
        *error_msg = strdup(err);
        json_add_error(json, err);
        return -1;
    }

    if (json->error != json_tokener_success && json->error != json_tokener_continue) {
        const char *err = json_tokener_error_desc(json->error);
        *error_msg = strdup(err);
        json_add_error(json, err);
        return -1;
    }

    return 1;
}

/**
 * Frees the resources used for JSON parsing.
 */
int jsonc_json_cleanup(jsonc_json_data *json) {
    assert(json != NULL);

    if (json->tokener != NULL) {
        json_tokener_free(json->tokener);
        json->tokener = NULL;
    }

    if (json->obj != NULL) {
        json_object_put(json->obj);
        json->obj = NULL;
    }

    free(json->prefix);
    free(json->current_key);
    free(json->result_buffer);
    free(json->error_buffer);
    free(json);

    return 1;
}

#endif
