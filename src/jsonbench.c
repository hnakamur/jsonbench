#include <stdio.h>
#include "../config.h"
#include "yajlparser.h"
#include "jsoncparser.h"
#include "rjparser.h"
#include "nlparser.h"
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <limits.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#ifndef FILE_BUFFER_SIZE
#define FILE_BUFFER_SIZE 524288
#endif

#ifndef LIMIT_ARG_NUM
#define LIMIT_ARG_NUM 500
#endif

#ifndef LIMIT_DEPTH
#define LIMIT_DEPTH 500
#endif

// https://gist.github.com/diabloneo/9619917#gistcomment-3364033
static inline void timespec_diff(const struct timespec *a, const struct timespec *b, struct timespec *result) {
    result->tv_sec  = a->tv_sec  - b->tv_sec;
    result->tv_nsec = a->tv_nsec - b->tv_nsec;
    if (result->tv_nsec < 0) {
        --result->tv_sec;
        result->tv_nsec += 1000000000L;
    }
}

static char available_engines[10][20] = {""};
static int engine_count = 0;

#define MAX_COMPARE_ENGINES 10
typedef struct {
    char *buffer;
    size_t size;
    char engine_name[20];
} engine_result;

static void showhelp(void) {
    printf("Use: jsonbench [OPTIONS]\n\n");
    printf("OPTIONS:\n");
    printf("\t-h\tThis help\n");
    printf("\t-d\tSet maximum depth of JSON structure, default: %d\n", LIMIT_DEPTH);
    printf("\t-a\tSet maximum number of possible ARGS, default: %d\n", LIMIT_ARG_NUM);
    printf("\t-s\tBe silence; don't print out the parsed data\n");
    printf("\t-p\tAllow partial JSON values (YAJL only)\n");
    if (engine_count > 0) {
        printf("\t-e\tUse JSON engine\n");
        printf("\t-c\tCompare engines (comma-separated, e.g., RAPIDJSON,NLOHMANNJSON)\n");
        printf("\t  \tavailable engines:\n");
        for(int i = 0; i < engine_count; i++) {
            printf("\t  \t- %s\n", available_engines[i]);
        }
    }
    else {
        printf("No JSON engine available\n");
    }
    printf("\n");
}

static int read_file(const char *filename, char *buffer) {
    FILE *file = fopen(filename, "r");
    if (file == NULL) {
        printf("Error: Unable to open file %s\n", filename);
        return -1;
    }

    memset(buffer, 0, FILE_BUFFER_SIZE - 1);
    int i = 0;
    int ci;
    size_t length = 0;
    while ((ci = fgetc(file))) {
        if (ci == EOF || !(i < FILE_BUFFER_SIZE)) {
            break;
        }
        buffer[i++] = ci;
        length++;
    }
    fclose(file);

    if (i == FILE_BUFFER_SIZE && ci != EOF) {
        printf("File too long: %s\n", filename);
        return -1 * EXIT_FAILURE;
    }

    return length;
}

static int is_directory(const char *path) {
    struct stat statbuf;
    if (stat(path, &statbuf) != 0) {
        return 0;
    }
    return S_ISDIR(statbuf.st_mode);
}

static void print_diff(const char *file1_name, const char *buf1, size_t size1,
                      const char *file2_name, const char *buf2, size_t size2) {
    printf("--- %s\n", file1_name);
    printf("+++ %s\n", file2_name);

    const char *p1 = buf1;
    const char *p2 = buf2;
    const char *end1 = buf1 + size1;
    const char *end2 = buf2 + size2;

    int line_num = 1;
    const char *line1_start = p1;
    const char *line2_start = p2;

    while (p1 < end1 || p2 < end2) {
        const char *line1_end = p1;
        const char *line2_end = p2;

        while (line1_end < end1 && *line1_end != '\n') line1_end++;
        while (line2_end < end2 && *line2_end != '\n') line2_end++;

        size_t len1 = line1_end - line1_start;
        size_t len2 = line2_end - line2_start;

        if (len1 != len2 || memcmp(line1_start, line2_start, len1) != 0) {
            printf("@@ -%d +%d @@\n", line_num, line_num);
            if (p1 < end1) {
                printf("-%.*s\n", (int)len1, line1_start);
            }
            if (p2 < end2) {
                printf("+%.*s\n", (int)len2, line2_start);
            }
        }

        if (line1_end < end1) line1_end++;
        if (line2_end < end2) line2_end++;

        p1 = line1_start = line1_end;
        p2 = line2_start = line2_end;
        line_num++;
    }
}

static int process_file_get_result(const char *jsonfile, const char *jsonengine,
                                   unsigned int depth_limit, unsigned int arg_limit,
                                   char **result_buffer, size_t *result_size) {
    char *error_msg;
    unsigned int length = 0;
    char data[FILE_BUFFER_SIZE];
    int rc;

    *result_buffer = NULL;
    *result_size = 0;

    rc = read_file(jsonfile, data);
    if (rc == 0) {
        fprintf(stderr, "Zero character read from file\n");
        return EXIT_FAILURE;
    }
    else if (rc < 0) {
        fprintf(stderr, "Error reading file\n");
        return EXIT_FAILURE;
    }
    else {
        length = rc;
    }

#if HAVE_YAJL
    if (strcmp(jsonengine, "YAJL") == 0) {
        yajl_json_data *json = NULL;
        yajl_json_init(&json, &error_msg);

        if (json != NULL) {
            json->depth_limit   = depth_limit;
            json->arg_num_limit = arg_limit;
            json->silence       = 1;

            if (yajl_json_process_chunk(json, data, length, &error_msg) < 0) {
                fprintf(stderr, "Error: %s\n", error_msg);
                free(error_msg);
                yajl_json_cleanup(json);
                return EXIT_FAILURE;
            }

            if (json->result_buffer != NULL && json->result_buffer_size > 0) {
                *result_buffer = malloc(json->result_buffer_size);
                if (*result_buffer != NULL) {
                    memcpy(*result_buffer, json->result_buffer, json->result_buffer_size);
                    *result_size = json->result_buffer_size;
                }
            }

            yajl_json_cleanup(json);
        }
    }
#endif
#if HAVE_JSONC
    if (strcmp(jsonengine, "JSONC") == 0) {
        jsonc_json_data *json = NULL;
        jsonc_json_init(&json, &error_msg);

        if (json != NULL) {
            json->depth_limit   = depth_limit;
            json->arg_num_limit = arg_limit;
            json->silence       = 1;

            if (jsonc_json_process_chunk(json, data, length, &error_msg) < 0) {
                fprintf(stderr, "Error: %s\n", error_msg);
                free(error_msg);
                jsonc_json_cleanup(json);
                return EXIT_FAILURE;
            }

            if (jsonc_json_complete(json, &error_msg) < 0) {
                fprintf(stderr, "Error: %s\n", error_msg);
                free(error_msg);
                jsonc_json_cleanup(json);
                return EXIT_FAILURE;
            }

            if (json->result_buffer != NULL && json->result_buffer_size > 0) {
                *result_buffer = malloc(json->result_buffer_size);
                if (*result_buffer != NULL) {
                    memcpy(*result_buffer, json->result_buffer, json->result_buffer_size);
                    *result_size = json->result_buffer_size;
                }
            }

            jsonc_json_cleanup(json);
        }
    }
#endif
#if HAVE_RAPIDJSON
    if (strcmp(jsonengine, "RAPIDJSON") == 0) {
        rj_parser *json = NULL;
        rj_json_init(&json, &error_msg);
        if (json == NULL) {
            fprintf(stderr, "Failed to initialize JSON parser\n");
            return 2;
        }

        rj_set_max_depth(json, depth_limit);
        rj_set_max_arg_num(json, arg_limit);
        rj_set_silence(json, 1);

        rc = rj_parse_buffer(json, data, length, &error_msg);
        if (rc != 0) {
            fprintf(stderr, "Parse failed with code %d\n", rc);
            fprintf(stderr, "Error: %s\n", error_msg);
            free(error_msg);
            rj_json_cleanup(json);
            return 3;
        }

        const char* result = rj_get_result_buffer(json);
        size_t size = rj_get_result_buffer_size(json);
        if (result != NULL && size > 0) {
            *result_buffer = malloc(size);
            if (*result_buffer != NULL) {
                memcpy(*result_buffer, result, size);
                *result_size = size;
            }
        }

        rj_json_cleanup(json);
    }
#endif
#if HAVE_NLOHMANNJSON
    if (strcmp(jsonengine, "NLOHMANNJSON") == 0) {
        nl_parser *json = NULL;
        nl_json_init(&json, &error_msg);
        if (json == NULL) {
            fprintf(stderr, "Failed to initialize JSON parser\n");
            return 2;
        }

        nl_set_max_depth(json, depth_limit);
        nl_set_max_arg_num(json, arg_limit);
        nl_set_silence(json, 1);

        rc = nl_parse_buffer(json, data, length, &error_msg);
        if (rc != 0) {
            fprintf(stderr, "Parse failed with code %d\n", rc);
            fprintf(stderr, "Error: %s\n", error_msg);
            free(error_msg);
            nl_json_cleanup(json);
            return 3;
        }

        const char* result = nl_get_result_buffer(json);
        size_t size = nl_get_result_buffer_size(json);
        if (result != NULL && size > 0) {
            *result_buffer = malloc(size);
            if (*result_buffer != NULL) {
                memcpy(*result_buffer, result, size);
                *result_size = size;
            }
        }

        nl_json_cleanup(json);
    }
#endif
    return 0;
}

static int process_file(const char *jsonfile, const char *jsonengine,
                       unsigned int depth_limit, unsigned int arg_limit, int silence, int allow_partial) {
    char *error_msg;
    unsigned int length = 0;
    char data[FILE_BUFFER_SIZE];
    struct timespec ts_before, ts_after, ts_diff;
    int rc;

    printf("Processing file: %s\n", jsonfile);

    rc = read_file(jsonfile, data);
    if (rc == 0) {
        printf("Zero character read from file\n");
        return EXIT_FAILURE;
    }
    else if (rc < 0) {
        printf("Error reading file\n");
        return EXIT_FAILURE;
    }
    else {
        length = rc;
    }

#if HAVE_YAJL
    if (strcmp(jsonengine, "YAJL") == 0) {
        yajl_json_data *json = NULL;
        yajl_json_init(&json, &error_msg);

        if (json != NULL) {

            json->depth_limit   = depth_limit;
            json->arg_num_limit = arg_limit;
            json->silence       = silence;

            if (allow_partial) {
                yajl_json_allow_partial(json);
            }

            clock_gettime(CLOCK_REALTIME, &ts_before);
            if (yajl_json_process_chunk(json, data, length, &error_msg) < 0) {
                printf("Error: %s\n", error_msg);
                free(error_msg);
            } else {
                /* Complete the parsing to check if JSON is valid and complete */
                if (yajl_json_complete(json, &error_msg) < 0) {
                    printf("Error: %s\n", error_msg);
                    free(error_msg);
                }
            }
            clock_gettime(CLOCK_REALTIME, &ts_after);
            ts_diff.tv_sec  = 0;
            ts_diff.tv_nsec = 0;
            timespec_diff(&ts_after, &ts_before, &ts_diff);

            /* Output result buffer if not in silence mode */
            if (!json->silence && json->result_buffer != NULL && json->result_buffer_size > 0) {
                printf("%s", json->result_buffer);
            }

            /* Output error buffer if not in silence mode and there are errors */
            if (!json->silence && json->error_buffer != NULL && json->error_buffer_size > 0) {
                fprintf(stderr, "%s", json->error_buffer);
            }

            yajl_json_cleanup(json);
            printf("\nTime: %ld.%09ld usec\n\n", (long)ts_diff.tv_sec, ts_diff.tv_nsec);
        }
    }
#endif
#if HAVE_JSONC
    if (strcmp(jsonengine, "JSONC") == 0) {
        jsonc_json_data *json = NULL;
        jsonc_json_init(&json, &error_msg);

        if (json != NULL) {

            json->depth_limit   = depth_limit;
            json->arg_num_limit = arg_limit;
            json->silence       = silence;

            if (allow_partial) {
                jsonc_json_allow_partial(json);
            }

            clock_gettime(CLOCK_REALTIME, &ts_before);
            if (jsonc_json_process_chunk(json, data, length, &error_msg) < 0) {
                printf("Error: %s\n", error_msg);
                free(error_msg);
            } else {
                /* Complete the parsing to check if JSON is valid and complete */
                if (jsonc_json_complete(json, &error_msg) < 0) {
                    printf("Error: %s\n", error_msg);
                    free(error_msg);
                }
            }
            clock_gettime(CLOCK_REALTIME, &ts_after);
            ts_diff.tv_sec  = 0;
            ts_diff.tv_nsec = 0;
            timespec_diff(&ts_after, &ts_before, &ts_diff);

            /* Output result buffer if not in silence mode */
            if (!json->silence && json->result_buffer != NULL && json->result_buffer_size > 0) {
                printf("%s", json->result_buffer);
            }

            /* Output error buffer if not in silence mode and there are errors */
            if (!json->silence && json->error_buffer != NULL && json->error_buffer_size > 0) {
                fprintf(stderr, "%s", json->error_buffer);
            }

            jsonc_json_cleanup(json);
            printf("\nTime: %ld.%09ld usec\n\n", (long)ts_diff.tv_sec, ts_diff.tv_nsec);
        }
    }
#endif
#if HAVE_RAPIDJSON
    if (strcmp(jsonengine, "RAPIDJSON") == 0) {

        rj_parser *json = NULL;
        rj_json_init(&json, &error_msg);
        if (json == NULL) {
            fprintf(stderr, "Failed to initialize JSON parser\n");
            return 2;
        }

        rj_set_max_depth(json, depth_limit);
        rj_set_max_arg_num(json, arg_limit);
        rj_set_silence(json, silence);

        clock_gettime(CLOCK_REALTIME, &ts_before);
        rc = rj_parse_buffer(json, data, length, &error_msg);
        if (rc != 0) {
            fprintf(stderr, "Parse failed with code %d\n", rc);
            fprintf(stderr, "Error: %s\n", error_msg);
            free(error_msg);

            /* Output error buffer if not in silence mode and there are errors */
            if (!silence) {
                const char* error_buffer = rj_get_error_buffer(json);
                size_t error_size = rj_get_error_buffer_size(json);
                if (error_buffer != NULL && error_size > 0) {
                    fprintf(stderr, "%s", error_buffer);
                }
            }

            rj_json_cleanup(json);
            return 3;
        }
        else {
            clock_gettime(CLOCK_REALTIME, &ts_after);
            ts_diff.tv_sec  = 0;
            ts_diff.tv_nsec = 0;
            timespec_diff(&ts_after, &ts_before, &ts_diff);

            /* Output result buffer if not in silence mode */
            if (!silence) {
                const char* result_buffer = rj_get_result_buffer(json);
                size_t result_size = rj_get_result_buffer_size(json);
                if (result_buffer != NULL && result_size > 0) {
                    printf("%s", result_buffer);
                }
            }

            /* Output error buffer if not in silence mode and there are errors */
            if (!silence) {
                const char* error_buffer = rj_get_error_buffer(json);
                size_t error_size = rj_get_error_buffer_size(json);
                if (error_buffer != NULL && error_size > 0) {
                    fprintf(stderr, "%s", error_buffer);
                }
            }

            rj_json_cleanup(json);
            printf("\nTime: %ld.%09ld usec\n\n", (long)ts_diff.tv_sec, ts_diff.tv_nsec);
        }

    }
#endif
#if HAVE_NLOHMANNJSON
    if (strcmp(jsonengine, "NLOHMANNJSON") == 0) {

        nl_parser *json = NULL;
        nl_json_init(&json, &error_msg);
        if (json == NULL) {
            fprintf(stderr, "Failed to initialize JSON parser\n");
            return 2;
        }

        nl_set_max_depth(json, depth_limit);
        nl_set_max_arg_num(json, arg_limit);
        nl_set_silence(json, silence);

        clock_gettime(CLOCK_REALTIME, &ts_before);
        rc = nl_parse_buffer(json, data, length, &error_msg);
        if (rc != 0) {
            fprintf(stderr, "Parse failed with code %d\n", rc);
            fprintf(stderr, "Error: %s\n", error_msg);
            free(error_msg);

            /* Output error buffer if not in silence mode and there are errors */
            if (!silence) {
                const char* error_buffer = nl_get_error_buffer(json);
                size_t error_size = nl_get_error_buffer_size(json);
                if (error_buffer != NULL && error_size > 0) {
                    fprintf(stderr, "%s", error_buffer);
                }
            }

            nl_json_cleanup(json);
            return 3;
        }
        else {
            clock_gettime(CLOCK_REALTIME, &ts_after);
            ts_diff.tv_sec  = 0;
            ts_diff.tv_nsec = 0;
            timespec_diff(&ts_after, &ts_before, &ts_diff);

            /* Output result buffer if not in silence mode */
            if (!silence) {
                const char* result_buffer = nl_get_result_buffer(json);
                size_t result_size = nl_get_result_buffer_size(json);
                if (result_buffer != NULL && result_size > 0) {
                    printf("%s", result_buffer);
                }
            }

            /* Output error buffer if not in silence mode and there are errors */
            if (!silence) {
                const char* error_buffer = nl_get_error_buffer(json);
                size_t error_size = nl_get_error_buffer_size(json);
                if (error_buffer != NULL && error_size > 0) {
                    fprintf(stderr, "%s", error_buffer);
                }
            }

            nl_json_cleanup(json);
            printf("\nTime: %ld.%09ld usec\n\n", (long)ts_diff.tv_sec, ts_diff.tv_nsec);
        }

    }
#endif
    return 0;
}

static int compare_file(const char *jsonfile, const char *base_engine,
                        char **compare_engines, int compare_count,
                        unsigned int depth_limit, unsigned int arg_limit) {
    engine_result results[MAX_COMPARE_ENGINES + 1];
    int i;

    printf("Comparing file: %s\n", jsonfile);

    /* Get result from base engine */
    results[0].buffer = NULL;
    results[0].size = 0;
    strncpy(results[0].engine_name, base_engine, sizeof(results[0].engine_name) - 1);
    if (process_file_get_result(jsonfile, base_engine, depth_limit, arg_limit,
                                &results[0].buffer, &results[0].size) != 0) {
        fprintf(stderr, "Failed to process with base engine %s\n", base_engine);
        return EXIT_FAILURE;
    }

    /* Get results from compare engines */
    for (i = 0; i < compare_count; i++) {
        results[i + 1].buffer = NULL;
        results[i + 1].size = 0;
        strncpy(results[i + 1].engine_name, compare_engines[i], sizeof(results[i + 1].engine_name) - 1);
        if (process_file_get_result(jsonfile, compare_engines[i], depth_limit, arg_limit,
                                    &results[i + 1].buffer, &results[i + 1].size) != 0) {
            fprintf(stderr, "Failed to process with engine %s\n", compare_engines[i]);
            /* Free allocated buffers */
            for (int j = 0; j <= i; j++) {
                free(results[j].buffer);
            }
            return EXIT_FAILURE;
        }
    }

    /* Compare results */
    int all_match = 1;
    for (i = 0; i < compare_count; i++) {
        if (results[0].size != results[i + 1].size ||
            memcmp(results[0].buffer, results[i + 1].buffer, results[0].size) != 0) {
            all_match = 0;
            printf("\nDifference found between %s and %s:\n",
                   results[0].engine_name, results[i + 1].engine_name);
            print_diff(results[0].engine_name, results[0].buffer, results[0].size,
                      results[i + 1].engine_name, results[i + 1].buffer, results[i + 1].size);
        }
    }

    if (all_match) {
        printf("All engines produced identical output.\n");
    }

    /* Free all buffers */
    for (i = 0; i <= compare_count; i++) {
        free(results[i].buffer);
    }

    return 0;
}

static int compare_directory(const char *dirname, const char *base_engine,
                            char **compare_engines, int compare_count,
                            unsigned int depth_limit, unsigned int arg_limit) {
    DIR *dir;
    struct dirent *entry;
    char filepath[PATH_MAX];

    dir = opendir(dirname);
    if (dir == NULL) {
        fprintf(stderr, "Error: Unable to open directory %s: %s\n", dirname, strerror(errno));
        return EXIT_FAILURE;
    }

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        snprintf(filepath, sizeof(filepath), "%s/%s", dirname, entry->d_name);

        if (is_directory(filepath)) {
            compare_directory(filepath, base_engine, compare_engines, compare_count, depth_limit, arg_limit);
        } else {
            compare_file(filepath, base_engine, compare_engines, compare_count, depth_limit, arg_limit);
        }
    }

    closedir(dir);
    return 0;
}

static int process_directory(const char *dirname, const char *jsonengine,
                            unsigned int depth_limit, unsigned int arg_limit, int silence, int allow_partial) {
    DIR *dir;
    struct dirent *entry;
    char filepath[PATH_MAX];

    dir = opendir(dirname);
    if (dir == NULL) {
        fprintf(stderr, "Error: Unable to open directory %s: %s\n", dirname, strerror(errno));
        return EXIT_FAILURE;
    }

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        snprintf(filepath, sizeof(filepath), "%s/%s", dirname, entry->d_name);

        if (is_directory(filepath)) {
            process_directory(filepath, jsonengine, depth_limit, arg_limit, silence, allow_partial);
        } else {
            process_file(filepath, jsonengine, depth_limit, arg_limit, silence, allow_partial);
        }
    }

    closedir(dir);
    return 0;
}

int main(int argc, char ** argv) {
    char           c;
    char          *jsonengine = NULL;
    char          *compare_engines_str = NULL;
    char          *compare_engines[MAX_COMPARE_ENGINES];
    int            compare_count = 0;

    extern char   *optarg;
    extern int     optind, opterr, optopt;

    const char    *jsonfile = NULL;
    unsigned int   depth_limit = LIMIT_DEPTH;
    unsigned int   arg_limit   = LIMIT_ARG_NUM;
    int            silence = 0;
    int            allow_partial = 0;

#ifdef HAVE_YAJL
strcpy(available_engines[engine_count++], "YAJL");
#endif

#ifdef HAVE_JSONC
strcpy(available_engines[engine_count++], "JSONC");
#endif

#ifdef HAVE_RAPIDJSON
strcpy(available_engines[engine_count++], "RAPIDJSON");
#endif

#ifdef HAVE_NLOHMANNJSON
strcpy(available_engines[engine_count++], "NLOHMANNJSON");
#endif

    while ((c = getopt(argc, argv, "he:c:a:d:sp")) != -1) {
        switch (c) {
            case 'h':
                showhelp();
                return 0;
            case 'e':
                jsonengine    = strdup(optarg);
                if (jsonengine == NULL) {
                    fprintf(stderr, "Memory allocation error\n");
                    return EXIT_FAILURE;
                }
                int i = 0;
                for(int e = 0; e < engine_count; e++) {
                    if (strcmp(jsonengine, available_engines[e]) == 0) {
                        break;
                    }
                }
                if (i == engine_count) {
                    fprintf(stderr, "JSON engine '%s' is not available!\n", jsonengine);
                    free(jsonengine);
                    return EXIT_FAILURE;
                }
                break;
            case 'c':
                compare_engines_str = strdup(optarg);
                if (compare_engines_str == NULL) {
                    fprintf(stderr, "Memory allocation error\n");
                    return EXIT_FAILURE;
                }
                /* Parse comma-separated engine names */
                char *token = strtok(compare_engines_str, ",");
                while (token != NULL && compare_count < MAX_COMPARE_ENGINES) {
                    /* Validate engine name */
                    int found = 0;
                    for (int j = 0; j < engine_count; j++) {
                        if (strcmp(token, available_engines[j]) == 0) {
                            found = 1;
                            break;
                        }
                    }
                    if (!found) {
                        fprintf(stderr, "JSON engine '%s' is not available!\n", token);
                        free(compare_engines_str);
                        return EXIT_FAILURE;
                    }
                    compare_engines[compare_count++] = strdup(token);
                    token = strtok(NULL, ",");
                }
                break;
            case 'a':
                arg_limit     = atoi(optarg);
                if (arg_limit == 0 || arg_limit > UINT_MAX) {
                    fprintf(stderr, "Ohh... Try to pass for '-m' an integer between 0 and %u\n", (unsigned int)UINT_MAX);
                    return EXIT_FAILURE;
                }
                break;
            case 'd':
                depth_limit    = atoi(optarg);
                if (depth_limit == 0 || depth_limit > UINT_MAX) {
                    fprintf(stderr, "Ohh... Try to pass for '-m' an integer between 0 and %u\n", (unsigned int)UINT_MAX);
                    return EXIT_FAILURE;
                }
                break;
            case 's':
                silence = 1;
                break;
            case 'p':
                allow_partial = 1;
                break;
            default:
                showhelp();
                return 0;
        }
    }

    /* Get input file/directory */
    for (int i = optind; i < argc; i++) {
        if (jsonfile == NULL) {
            jsonfile = argv[i];
        }
    }

    if (jsonfile == NULL) {
        printf("No JSON file or directory was given!\n");
        return EXIT_FAILURE;
    }

    /* Compare mode */
    if (compare_count > 0) {
        if (jsonengine == NULL) {
            fprintf(stderr, "Base engine (-e) must be specified when using comparison mode (-c)\n");
            for (int i = 0; i < compare_count; i++) {
                free(compare_engines[i]);
            }
            free(compare_engines_str);
            return EXIT_FAILURE;
        }

        if (is_directory(jsonfile)) {
            compare_directory(jsonfile, jsonengine, compare_engines, compare_count, depth_limit, arg_limit);
        } else {
            compare_file(jsonfile, jsonengine, compare_engines, compare_count, depth_limit, arg_limit);
        }

        /* Free compare engines */
        for (int i = 0; i < compare_count; i++) {
            free(compare_engines[i]);
        }
        free(compare_engines_str);
        free(jsonengine);
    }
    /* Normal mode */
    else if (jsonengine != NULL) {
        if (is_directory(jsonfile)) {
            process_directory(jsonfile, jsonengine, depth_limit, arg_limit, silence, allow_partial);
        } else {
            process_file(jsonfile, jsonengine, depth_limit, arg_limit, silence, allow_partial);
        }

        free(jsonengine);
    }
    else {
        printf("No JSON engine was given!\n");
        return EXIT_FAILURE;
    }

    return 0;
}
