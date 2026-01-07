#include <gtest/gtest.h>
#include <rapidcheck/gtest.h>
#include <string>
#include <cstring>

// Include JSON parser headers
extern "C" {
#include "../src/nlparser.h"
#include "../src/rjparser.h"
#include "../src/yajlparser.h"
}

// Helper function to compare parser results
bool compareParserResults(const char* json_input) {
    char *error_msg = nullptr;

    // Initialize parsers
    nl_parser *nl_p = nullptr;
    rj_parser *rj_p = nullptr;
    yajl_json_data *yajl_p = nullptr;

    if (nl_json_init(&nl_p, &error_msg) != 0) {
        if (error_msg) free(error_msg);
        return false;
    }
    if (rj_json_init(&rj_p, &error_msg) != 0) {
        nl_json_cleanup(nl_p);
        if (error_msg) free(error_msg);
        return false;
    }
    if (yajl_json_init(&yajl_p, &error_msg) != 1) {
        nl_json_cleanup(nl_p);
        rj_json_cleanup(rj_p);
        if (error_msg) free(error_msg);
        return false;
    }

    // Configure parsers to produce output (silence = 0)
    yajl_p->silence = 0;
    yajl_p->depth_limit = 500;
    yajl_p->arg_num_limit = 500;

    nl_set_silence(nl_p, 0);
    nl_set_max_depth(nl_p, 500);
    nl_set_max_arg_num(nl_p, 500);

    rj_set_silence(rj_p, 0);
    rj_set_max_depth(rj_p, 500);
    rj_set_max_arg_num(rj_p, 500);

    // Parse with all three parsers
    int nl_success = (nl_parse_buffer(nl_p, json_input, strlen(json_input), &error_msg) == 0);
    if (error_msg) { free(error_msg); error_msg = nullptr; }

    int rj_success = (rj_parse_buffer(rj_p, json_input, strlen(json_input), &error_msg) == 0);
    if (error_msg) { free(error_msg); error_msg = nullptr; }

    int yajl_success = (yajl_json_process_chunk(yajl_p, json_input, strlen(json_input), &error_msg) == 1);
    if (error_msg) { free(error_msg); error_msg = nullptr; }

    // All parsers should agree on success/failure
    bool results_agree = (nl_success == yajl_success) && (rj_success == yajl_success);

    // Debug output for failures
    if (!results_agree) {
        RC_LOG() << "Parser results differ: nl=" << nl_success
                 << " rj=" << rj_success
                 << " yajl=" << yajl_success
                 << " input='" << json_input << "'\n";

        // Output error buffers for parsers that failed
        if (!nl_success) {
            const char* nl_error = nl_get_error_buffer(nl_p);
            if (nl_error && strlen(nl_error) > 0) {
                RC_LOG() << "  nl error: " << nl_error;
            }
        }
        if (!rj_success) {
            const char* rj_error = rj_get_error_buffer(rj_p);
            if (rj_error && strlen(rj_error) > 0) {
                RC_LOG() << "  rj error: " << rj_error;
            }
        }
        if (!yajl_success) {
            if (yajl_p->error_buffer && yajl_p->error_buffer_size > 0) {
                RC_LOG() << "  yajl error: " << yajl_p->error_buffer;
            }
        }
    }

    // If all succeeded, compare output buffers
    if (results_agree && yajl_success) {
        const char* nl_output = nl_get_result_buffer(nl_p);
        const char* rj_output = rj_get_result_buffer(rj_p);
        const char* yajl_output = yajl_p->result_buffer;

        if (nl_output && rj_output && yajl_output) {
            results_agree = (strcmp(nl_output, yajl_output) == 0) &&
                          (strcmp(rj_output, yajl_output) == 0);
            if (!results_agree) {
                RC_LOG() << "Output buffers differ:\n"
                         << "  nl: '" << nl_output << "'\n"
                         << "  rj: '" << rj_output << "'\n"
                         << "  yajl: '" << yajl_output << "'\n";
            }
        }
    }

    // Cleanup
    nl_json_cleanup(nl_p);
    rj_json_cleanup(rj_p);
    yajl_json_cleanup(yajl_p);

    return results_agree;
}

// Custom generator for simple JSON values
namespace rc {
namespace gen {

Gen<std::string> jsonValue() {
    return oneOf(
        // JSON objects
        just(std::string("{}")),
        just(std::string("{\"key\":\"value\"}")),
        just(std::string("{\"a\":1}")),
        just(std::string("{\"x\":true}")),
        just(std::string("{\"y\":null}")),
        just(std::string("{\"nested\":{\"key\":\"value\"}}")),

        // JSON arrays
        just(std::string("[]")),
        just(std::string("[1,2,3]")),
        just(std::string("[\"a\",\"b\"]")),
        just(std::string("[true,false]")),
        just(std::string("[null]")),
        just(std::string("[[1,2],[3,4]]")),

        // JSON primitives
        just(std::string("null")),
        just(std::string("true")),
        just(std::string("false")),
        just(std::string("0")),
        just(std::string("123")),
        just(std::string("-456")),
        just(std::string("\"string\"")),
        just(std::string("\"hello world\"")),

        // Invalid JSON (to test error handling consistency)
        just(std::string("")),
        just(std::string("{")),
        just(std::string("}")),
        just(std::string("invalid")),
        just(std::string("{\"key\"")),
        just(std::string("[1,2,"))
    );
}

} // namespace gen
} // namespace rc

// Property test: All parsers should produce the same result
RC_GTEST_PROP(JSONParserComparison,
 parsersAgreeOnJSON,
 ()) {
 auto json_str = *rc::gen::jsonValue();

 RC_CLASSIFY(json_str.empty(), "empty string");
 RC_CLASSIFY(json_str.find('{') != std::string::npos, "has object");
 RC_CLASSIFY(json_str.find('[') != std::string::npos, "has array");

 // Test the parsers
 bool parsers_agree = compareParserResults(json_str.c_str());

 // All parsers should agree (either all succeed with same output, or all fail)
 RC_ASSERT(parsers_agree);
}


int main(int argc, char **argv) {
 ::testing::InitGoogleTest(&argc, argv);
 return RUN_ALL_TESTS();
}
