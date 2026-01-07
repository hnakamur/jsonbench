#ifndef RJ_PARSER_H
#define RJ_PARSER_H

#include "../config.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct rj_parser rj_parser;

int rj_json_init(rj_parser **parser, char **error_msg);
int rj_parse_buffer(rj_parser *parser, const char *buf, unsigned int len, char **error_msg);
int rj_set_max_depth(rj_parser *parser, double max_depth);
int rj_set_max_arg_num(rj_parser *parser, double max_arg_num);
int rj_set_silence(rj_parser *parser, int silence);
int rj_json_cleanup(rj_parser *parser);
const char* rj_get_result_buffer(rj_parser *parser);
size_t rj_get_result_buffer_size(rj_parser *parser);
const char* rj_get_error_buffer(rj_parser *parser);
size_t rj_get_error_buffer_size(rj_parser *parser);

#ifdef __cplusplus
}
#endif

#endif // RJ_PARSER_H