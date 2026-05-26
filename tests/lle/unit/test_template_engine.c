/**
 * Unit tests for LLE Template Engine (Spec 25 Section 6)
 *
 * Tests template parsing and rendering with segments, properties,
 * conditionals, and color application.
 */

#include "lle/error_handling.h"
#include "lle/prompt/template.h"
#include "test_framework.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Test bodies in this file use the older 2-arg / no-arg ASSERT forms.
 * Override them to call the shared framework so each test still gets
 * setjmp/longjmp failure isolation without editing every test body.
 * PASS() becomes a no-op since the framework auto-counts on TEST exit. */
#undef ASSERT
#undef ASSERT_EQ
#undef ASSERT_STR_EQ
#define ASSERT(cond)                                                           \
    do {                                                                       \
        if (!(cond)) {                                                         \
            TEST_FAIL_MSG(#cond);                                              \
        }                                                                      \
    } while (0)
#define ASSERT_EQ(a, b) ASSERT((a) == (b))
#define ASSERT_STR_EQ(a, b) ASSERT(strcmp((a), (b)) == 0)
#define PASS() ((void)0)

/* ========================================================================== */
/// Mock render context callbacks
/* ========================================================================== */

typedef struct {
    const char *directory;
    const char *git_branch;
    bool git_visible;
    const char *user;
} mock_context_t;

static char *mock_get_segment(const char *segment_name, const char *property,
                              void *user_data) {
    mock_context_t *ctx = user_data;
    if (!ctx)
        return NULL;

    if (strcmp(segment_name, "directory") == 0) {
        if (!property) {
            return ctx->directory ? strdup(ctx->directory) : NULL;
        }
    } else if (strcmp(segment_name, "git") == 0) {
        if (!property) {
            if (ctx->git_visible && ctx->git_branch) {
                char *buf = malloc(256);
                snprintf(buf, 256, "(%s)", ctx->git_branch);
                return buf;
            }
            return NULL;
        } else if (strcmp(property, "branch") == 0) {
            return ctx->git_branch ? strdup(ctx->git_branch) : NULL;
        }
    } else if (strcmp(segment_name, "user") == 0) {
        return ctx->user ? strdup(ctx->user) : NULL;
    }

    return NULL;
}

static bool mock_is_visible(const char *segment_name, const char *property,
                            void *user_data) {
    mock_context_t *ctx = user_data;
    if (!ctx)
        return false;

    if (strcmp(segment_name, "directory") == 0) {
        return ctx->directory != NULL;
    } else if (strcmp(segment_name, "git") == 0) {
        if (property && strcmp(property, "branch") == 0) {
            return ctx->git_branch != NULL;
        }
        return ctx->git_visible;
    } else if (strcmp(segment_name, "user") == 0) {
        return ctx->user != NULL;
    }

    return false;
}

static const char *mock_get_color(const char *color_name, void *user_data) {
    (void)user_data;

    if (strcmp(color_name, "primary") == 0) {
        return "\033[1;34m"; /// Bold blue
    } else if (strcmp(color_name, "success") == 0) {
        return "\033[1;32m"; /// Bold green
    } else if (strcmp(color_name, "error") == 0) {
        return "\033[1;31m"; /// Bold red
    }

    return "";
}

/* ========================================================================== */
/// Token creation tests
/* ========================================================================== */

TEST(token_literal_creation) {
    lle_template_token_t *token = lle_template_token_literal("hello", 5);
    ASSERT(token != NULL);
    ASSERT_EQ(token->type, LLE_TOKEN_LITERAL);
    ASSERT_STR_EQ(token->data.literal.text, "hello");
    ASSERT_EQ(token->data.literal.length, 5);
    lle_template_token_free(token);
    PASS();
}

TEST(token_segment_creation) {
    lle_template_token_t *token = lle_template_token_segment("directory");
    ASSERT(token != NULL);
    ASSERT_EQ(token->type, LLE_TOKEN_SEGMENT);
    ASSERT_STR_EQ(token->data.segment.segment_name, "directory");
    ASSERT_EQ(token->data.segment.has_property, false);
    lle_template_token_free(token);
    PASS();
}

TEST(token_property_creation) {
    lle_template_token_t *token = lle_template_token_property("git", "branch");
    ASSERT(token != NULL);
    ASSERT_EQ(token->type, LLE_TOKEN_PROPERTY);
    ASSERT_STR_EQ(token->data.segment.segment_name, "git");
    ASSERT_STR_EQ(token->data.segment.property_name, "branch");
    ASSERT_EQ(token->data.segment.has_property, true);
    lle_template_token_free(token);
    PASS();
}

TEST(token_conditional_creation) {
    lle_template_token_t *token =
        lle_template_token_conditional("git", NULL, "yes", "no");
    ASSERT(token != NULL);
    ASSERT_EQ(token->type, LLE_TOKEN_CONDITIONAL);
    ASSERT_STR_EQ(token->data.conditional.condition_segment, "git");
    ASSERT_EQ(token->data.conditional.check_property, false);
    ASSERT_STR_EQ(token->data.conditional.true_value, "yes");
    ASSERT_STR_EQ(token->data.conditional.false_value, "no");
    lle_template_token_free(token);
    PASS();
}

TEST(token_conditional_with_property) {
    lle_template_token_t *token = lle_template_token_conditional(
        "git", "branch", "on branch", "no branch");
    ASSERT(token != NULL);
    ASSERT_EQ(token->type, LLE_TOKEN_CONDITIONAL);
    ASSERT_STR_EQ(token->data.conditional.condition_segment, "git");
    ASSERT_STR_EQ(token->data.conditional.condition_property, "branch");
    ASSERT_EQ(token->data.conditional.check_property, true);
    lle_template_token_free(token);
    PASS();
}

TEST(token_color_creation) {
    lle_template_token_t *token = lle_template_token_color("primary", "text");
    ASSERT(token != NULL);
    ASSERT_EQ(token->type, LLE_TOKEN_COLOR);
    ASSERT_STR_EQ(token->data.color.color_name, "primary");
    ASSERT_STR_EQ(token->data.color.text, "text");
    lle_template_token_free(token);
    PASS();
}

TEST(token_newline_creation) {
    lle_template_token_t *token = lle_template_token_newline();
    ASSERT(token != NULL);
    ASSERT_EQ(token->type, LLE_TOKEN_NEWLINE);
    lle_template_token_free(token);
    PASS();
}

/* ========================================================================== */
/// Template parsing tests
/* ========================================================================== */

TEST(parse_empty_template) {
    lle_parsed_template_t *parsed = NULL;
    lle_result_t result = lle_template_parse("", &parsed);
    ASSERT_EQ(result, LLE_SUCCESS);
    ASSERT(parsed != NULL);
    ASSERT(parsed->valid);
    ASSERT_EQ(parsed->token_count, 1); /// Just end token
    lle_template_free(parsed);
    PASS();
}

TEST(parse_literal_only) {
    lle_parsed_template_t *parsed = NULL;
    lle_result_t result = lle_template_parse("hello world", &parsed);
    ASSERT_EQ(result, LLE_SUCCESS);
    ASSERT(parsed != NULL);
    ASSERT(parsed->valid);
    ASSERT_EQ(parsed->token_count, 2); /// literal + end
    ASSERT_EQ(parsed->head->type, LLE_TOKEN_LITERAL);
    ASSERT_STR_EQ(parsed->head->data.literal.text, "hello world");
    lle_template_free(parsed);
    PASS();
}

TEST(parse_segment_reference) {
    lle_parsed_template_t *parsed = NULL;
    lle_result_t result = lle_template_parse("${directory}", &parsed);
    ASSERT_EQ(result, LLE_SUCCESS);
    ASSERT(parsed != NULL);
    ASSERT_EQ(parsed->token_count, 2); /// segment + end
    ASSERT_EQ(parsed->head->type, LLE_TOKEN_SEGMENT);
    ASSERT_STR_EQ(parsed->head->data.segment.segment_name, "directory");
    lle_template_free(parsed);
    PASS();
}

TEST(parse_property_reference) {
    lle_parsed_template_t *parsed = NULL;
    lle_result_t result = lle_template_parse("${git.branch}", &parsed);
    ASSERT_EQ(result, LLE_SUCCESS);
    ASSERT(parsed != NULL);
    ASSERT_EQ(parsed->head->type, LLE_TOKEN_PROPERTY);
    ASSERT_STR_EQ(parsed->head->data.segment.segment_name, "git");
    ASSERT_STR_EQ(parsed->head->data.segment.property_name, "branch");
    lle_template_free(parsed);
    PASS();
}

TEST(parse_conditional) {
    lle_parsed_template_t *parsed = NULL;
    lle_result_t result = lle_template_parse("${?git:yes:no}", &parsed);
    ASSERT_EQ(result, LLE_SUCCESS);
    ASSERT(parsed != NULL);
    ASSERT_EQ(parsed->head->type, LLE_TOKEN_CONDITIONAL);
    ASSERT_STR_EQ(parsed->head->data.conditional.condition_segment, "git");
    ASSERT_STR_EQ(parsed->head->data.conditional.true_value, "yes");
    ASSERT_STR_EQ(parsed->head->data.conditional.false_value, "no");
    lle_template_free(parsed);
    PASS();
}

TEST(parse_conditional_with_property) {
    lle_parsed_template_t *parsed = NULL;
    lle_result_t result =
        lle_template_parse("${?git.branch:has branch:}", &parsed);
    ASSERT_EQ(result, LLE_SUCCESS);
    ASSERT(parsed != NULL);
    ASSERT_EQ(parsed->head->type, LLE_TOKEN_CONDITIONAL);
    ASSERT_EQ(parsed->head->data.conditional.check_property, true);
    ASSERT_STR_EQ(parsed->head->data.conditional.condition_property, "branch");
    lle_template_free(parsed);
    PASS();
}

TEST(parse_color_application) {
    lle_parsed_template_t *parsed = NULL;
    lle_result_t result = lle_template_parse("${primary:hello}", &parsed);
    ASSERT_EQ(result, LLE_SUCCESS);
    ASSERT(parsed != NULL);
    ASSERT_EQ(parsed->head->type, LLE_TOKEN_COLOR);
    ASSERT_STR_EQ(parsed->head->data.color.color_name, "primary");
    ASSERT_STR_EQ(parsed->head->data.color.text, "hello");
    lle_template_free(parsed);
    PASS();
}

TEST(parse_newline_escape) {
    lle_parsed_template_t *parsed = NULL;
    lle_result_t result = lle_template_parse("line1\\nline2", &parsed);
    ASSERT_EQ(result, LLE_SUCCESS);
    ASSERT(parsed != NULL);
    ASSERT_EQ(parsed->token_count, 4); /// literal + newline + literal + end
    ASSERT_EQ(parsed->head->type, LLE_TOKEN_LITERAL);
    ASSERT_EQ(parsed->head->next->type, LLE_TOKEN_NEWLINE);
    ASSERT_EQ(parsed->head->next->next->type, LLE_TOKEN_LITERAL);
    lle_template_free(parsed);
    PASS();
}

TEST(parse_escaped_dollar) {
    lle_parsed_template_t *parsed = NULL;
    lle_result_t result = lle_template_parse("cost: \\$100", &parsed);
    ASSERT_EQ(result, LLE_SUCCESS);
    ASSERT(parsed != NULL);
    ASSERT_EQ(parsed->head->type, LLE_TOKEN_LITERAL);
    ASSERT_STR_EQ(parsed->head->data.literal.text, "cost: $100");
    lle_template_free(parsed);
    PASS();
}

TEST(parse_mixed_template) {
    lle_parsed_template_t *parsed = NULL;
    lle_result_t result =
        lle_template_parse("${directory} ${?git:${git} :}> ", &parsed);
    ASSERT_EQ(result, LLE_SUCCESS);
    ASSERT(parsed != NULL);
    ASSERT(parsed->valid);
    /// Should have: segment, literal, conditional, literal, end
    ASSERT(parsed->token_count >= 4);
    lle_template_free(parsed);
    PASS();
}

TEST(validate_valid_template) {
    ASSERT(lle_template_validate("${directory}"));
    ASSERT(lle_template_validate("hello ${world}"));
    ASSERT(lle_template_validate("${?git:yes:no}"));
    ASSERT(lle_template_validate(""));
    PASS();
}

TEST(validate_unclosed_brace) {
    ASSERT(!lle_template_validate("${directory"));
    ASSERT(!lle_template_validate("hello ${world"));
    PASS();
}

/* ========================================================================== */
/// Template rendering tests
/* ========================================================================== */

TEST(render_literal_only) {
    mock_context_t ctx = {0};
    lle_template_render_ctx_t render_ctx = {.get_segment = mock_get_segment,
                                            .is_visible = mock_is_visible,
                                            .get_color = mock_get_color,
                                            .user_data = &ctx};

    char output[256];
    lle_result_t result = lle_template_evaluate("hello world", &render_ctx,
                                                output, sizeof(output));
    ASSERT_EQ(result, LLE_SUCCESS);
    ASSERT_STR_EQ(output, "hello world");
    PASS();
}

TEST(render_segment) {
    mock_context_t ctx = {
        .directory = "/home/user",
    };
    lle_template_render_ctx_t render_ctx = {.get_segment = mock_get_segment,
                                            .is_visible = mock_is_visible,
                                            .get_color = mock_get_color,
                                            .user_data = &ctx};

    char output[256];
    lle_result_t result = lle_template_evaluate("${directory}", &render_ctx,
                                                output, sizeof(output));
    ASSERT_EQ(result, LLE_SUCCESS);
    ASSERT_STR_EQ(output, "/home/user");
    PASS();
}

TEST(render_property) {
    mock_context_t ctx = {
        .git_branch = "main",
        .git_visible = true,
    };
    lle_template_render_ctx_t render_ctx = {.get_segment = mock_get_segment,
                                            .is_visible = mock_is_visible,
                                            .get_color = mock_get_color,
                                            .user_data = &ctx};

    char output[256];
    lle_result_t result = lle_template_evaluate("${git.branch}", &render_ctx,
                                                output, sizeof(output));
    ASSERT_EQ(result, LLE_SUCCESS);
    ASSERT_STR_EQ(output, "main");
    PASS();
}

TEST(render_conditional_true) {
    mock_context_t ctx = {
        .git_branch = "main",
        .git_visible = true,
    };
    lle_template_render_ctx_t render_ctx = {.get_segment = mock_get_segment,
                                            .is_visible = mock_is_visible,
                                            .get_color = mock_get_color,
                                            .user_data = &ctx};

    char output[256];
    lle_result_t result = lle_template_evaluate(
        "${?git:in git:not git}", &render_ctx, output, sizeof(output));
    ASSERT_EQ(result, LLE_SUCCESS);
    ASSERT_STR_EQ(output, "in git");
    PASS();
}

TEST(render_conditional_false) {
    mock_context_t ctx = {
        .git_visible = false,
    };
    lle_template_render_ctx_t render_ctx = {.get_segment = mock_get_segment,
                                            .is_visible = mock_is_visible,
                                            .get_color = mock_get_color,
                                            .user_data = &ctx};

    char output[256];
    lle_result_t result = lle_template_evaluate(
        "${?git:in git:not git}", &render_ctx, output, sizeof(output));
    ASSERT_EQ(result, LLE_SUCCESS);
    ASSERT_STR_EQ(output, "not git");
    PASS();
}

TEST(render_color_application) {
    mock_context_t ctx = {0};
    lle_template_render_ctx_t render_ctx = {.get_segment = mock_get_segment,
                                            .is_visible = mock_is_visible,
                                            .get_color = mock_get_color,
                                            .user_data = &ctx};

    char output[256];
    lle_result_t result = lle_template_evaluate("${primary:hello}", &render_ctx,
                                                output, sizeof(output));
    ASSERT_EQ(result, LLE_SUCCESS);
    /// Should contain color code, text, and reset
    ASSERT(strstr(output, "\033[1;34m") != NULL); /// Blue
    ASSERT(strstr(output, "hello") != NULL);
    ASSERT(strstr(output, "\033[0m") != NULL); /// Reset
    PASS();
}

TEST(render_newline) {
    mock_context_t ctx = {0};
    lle_template_render_ctx_t render_ctx = {.get_segment = mock_get_segment,
                                            .is_visible = mock_is_visible,
                                            .get_color = mock_get_color,
                                            .user_data = &ctx};

    char output[256];
    lle_result_t result = lle_template_evaluate("line1\\nline2", &render_ctx,
                                                output, sizeof(output));
    ASSERT_EQ(result, LLE_SUCCESS);
    ASSERT_STR_EQ(output, "line1\nline2");
    PASS();
}

TEST(render_complex_template) {
    mock_context_t ctx = {
        .directory = "~/project",
        .git_branch = "feature",
        .git_visible = true,
        .user = "alice",
    };
    lle_template_render_ctx_t render_ctx = {.get_segment = mock_get_segment,
                                            .is_visible = mock_is_visible,
                                            .get_color = mock_get_color,
                                            .user_data = &ctx};

    /// Test with separate segments - conditional values are literal strings
    char output[512];
    lle_result_t result =
        lle_template_evaluate("${user}@host:${directory} ${git}$ ", &render_ctx,
                              output, sizeof(output));
    ASSERT_EQ(result, LLE_SUCCESS);
    ASSERT(strstr(output, "alice") != NULL);
    ASSERT(strstr(output, "~/project") != NULL);
    ASSERT(strstr(output, "(feature)") != NULL);
    ASSERT(strstr(output, "$ ") != NULL);
    PASS();
}

TEST(render_missing_segment) {
    mock_context_t ctx = {0}; /// No segments set
    lle_template_render_ctx_t render_ctx = {.get_segment = mock_get_segment,
                                            .is_visible = mock_is_visible,
                                            .get_color = mock_get_color,
                                            .user_data = &ctx};

    char output[256];
    lle_result_t result = lle_template_evaluate("${nonexistent}", &render_ctx,
                                                output, sizeof(output));
    ASSERT_EQ(result, LLE_SUCCESS);
    ASSERT_STR_EQ(output, ""); /// Missing segment renders as empty
    PASS();
}

/* ========================================================================== */
/// Main test runner
/* ========================================================================== */

int main(void) {
    printf("===========================================\n");
    printf("    LLE Template Engine Unit Tests\n");
    printf("===========================================\n\n");

    /// Token creation tests
    RUN_TEST(token_literal_creation);
    RUN_TEST(token_segment_creation);
    RUN_TEST(token_property_creation);
    RUN_TEST(token_conditional_creation);
    RUN_TEST(token_conditional_with_property);
    RUN_TEST(token_color_creation);
    RUN_TEST(token_newline_creation);

    /// Parsing tests
    RUN_TEST(parse_empty_template);
    RUN_TEST(parse_literal_only);
    RUN_TEST(parse_segment_reference);
    RUN_TEST(parse_property_reference);
    RUN_TEST(parse_conditional);
    RUN_TEST(parse_conditional_with_property);
    RUN_TEST(parse_color_application);
    RUN_TEST(parse_newline_escape);
    RUN_TEST(parse_escaped_dollar);
    RUN_TEST(parse_mixed_template);
    RUN_TEST(validate_valid_template);
    RUN_TEST(validate_unclosed_brace);

    /// Rendering tests
    RUN_TEST(render_literal_only);
    RUN_TEST(render_segment);
    RUN_TEST(render_property);
    RUN_TEST(render_conditional_true);
    RUN_TEST(render_conditional_false);
    RUN_TEST(render_color_application);
    RUN_TEST(render_newline);
    RUN_TEST(render_complex_template);
    RUN_TEST(render_missing_segment);

    return TEST_RESULT();
}
