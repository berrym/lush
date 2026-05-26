/**
 * @file template_engine.c
 * @brief LLE Prompt Template Engine Implementation
 *
 * Specification: Spec 25 Section 6 - Template Engine
 * Version: 1.0.0
 *
 * Parses and renders prompt templates with segment references,
 * conditionals, color application, and property access.
 */

#include "lle/prompt/template.h"

#include <stdlib.h>
#include <string.h>

/* ========================================================================== */
/// Token Creation
/* ========================================================================== */

/**
 * @brief Create a literal text token.
 *
 * @param text The literal text content.
 * @param len Length of the text.
 * @return Allocated token, or NULL on failure.
 */
lle_template_token_t *lle_template_token_literal(const char *text, size_t len) {
    if (!text || len == 0) {
        return NULL;
    }

    lle_template_token_t *token = calloc(1, sizeof(*token));
    if (!token) {
        return NULL;
    }

    token->type = LLE_TOKEN_LITERAL;
    size_t copy_len = (len < LLE_TEMPLATE_LITERAL_MAX - 1)
                          ? len
                          : LLE_TEMPLATE_LITERAL_MAX - 1;
    memcpy(token->data.literal.text, text, copy_len);
    token->data.literal.text[copy_len] = '\0';
    token->data.literal.length = copy_len;

    return token;
}

/**
 * @brief Create a segment reference token.
 *
 * @param name The segment name to reference.
 * @return Allocated token, or NULL on failure.
 */
lle_template_token_t *lle_template_token_segment(const char *name) {
    if (!name || strlen(name) == 0) {
        return NULL;
    }

    lle_template_token_t *token = calloc(1, sizeof(*token));
    if (!token) {
        return NULL;
    }

    token->type = LLE_TOKEN_SEGMENT;
    snprintf(token->data.segment.segment_name,
             sizeof(token->data.segment.segment_name), "%s", name);
    token->data.segment.has_property = false;

    return token;
}

/**
 * @brief Create a segment property access token.
 *
 * @param segment The segment name.
 * @param property The property name to access.
 * @return Allocated token, or NULL on failure.
 */
lle_template_token_t *lle_template_token_property(const char *segment,
                                                  const char *property) {
    if (!segment || !property) {
        return NULL;
    }

    lle_template_token_t *token = calloc(1, sizeof(*token));
    if (!token) {
        return NULL;
    }

    token->type = LLE_TOKEN_PROPERTY;
    snprintf(token->data.segment.segment_name,
             sizeof(token->data.segment.segment_name), "%s", segment);
    snprintf(token->data.segment.property_name,
             sizeof(token->data.segment.property_name), "%s", property);
    token->data.segment.has_property = true;

    return token;
}

/**
 * @brief Create a conditional token.
 *
 * @param segment The segment to check for visibility.
 * @param property Optional property to check (NULL for segment visibility).
 * @param true_val Value to use when condition is true.
 * @param false_val Value to use when condition is false.
 * @return Allocated token, or NULL on failure.
 */
lle_template_token_t *lle_template_token_conditional(const char *segment,
                                                     const char *property,
                                                     const char *true_val,
                                                     const char *false_val) {
    if (!segment) {
        return NULL;
    }

    lle_template_token_t *token = calloc(1, sizeof(*token));
    if (!token) {
        return NULL;
    }

    token->type = LLE_TOKEN_CONDITIONAL;
    snprintf(token->data.conditional.condition_segment,
             sizeof(token->data.conditional.condition_segment), "%s", segment);

    if (property && strlen(property) > 0) {
        snprintf(token->data.conditional.condition_property,
                 sizeof(token->data.conditional.condition_property), "%s",
                 property);
        token->data.conditional.check_property = true;
    } else {
        token->data.conditional.condition_property[0] = '\0';
        token->data.conditional.check_property = false;
    }

    if (true_val) {
        snprintf(token->data.conditional.true_value,
                 sizeof(token->data.conditional.true_value), "%s", true_val);
    }

    if (false_val) {
        snprintf(token->data.conditional.false_value,
                 sizeof(token->data.conditional.false_value), "%s", false_val);
    }

    return token;
}

/**
 * @brief Create a color application token.
 *
 * @param color The semantic color name.
 * @param text The text to apply color to.
 * @return Allocated token, or NULL on failure.
 */
lle_template_token_t *lle_template_token_color(const char *color,
                                               const char *text) {
    if (!color || !text) {
        return NULL;
    }

    lle_template_token_t *token = calloc(1, sizeof(*token));
    if (!token) {
        return NULL;
    }

    token->type = LLE_TOKEN_COLOR;
    snprintf(token->data.color.color_name, sizeof(token->data.color.color_name),
             "%s", color);
    snprintf(token->data.color.text, sizeof(token->data.color.text), "%s",
             text);

    return token;
}

/**
 * @brief Create a newline token.
 *
 * @return Allocated token, or NULL on failure.
 */
lle_template_token_t *lle_template_token_newline(void) {
    lle_template_token_t *token = calloc(1, sizeof(*token));
    if (!token) {
        return NULL;
    }

    token->type = LLE_TOKEN_NEWLINE;
    return token;
}

/**
 * @brief Create an end-of-template token.
 *
 * @return Allocated token, or NULL on failure.
 */
lle_template_token_t *lle_template_token_end(void) {
    lle_template_token_t *token = calloc(1, sizeof(*token));
    if (!token) {
        return NULL;
    }

    token->type = LLE_TOKEN_END;
    return token;
}

/**
 * @brief Free a template token.
 *
 * @param token The token to free, or NULL for no-op.
 */
void lle_template_token_free(lle_template_token_t *token) { free(token); }

/* ========================================================================== */
/// Template Helpers
/* ========================================================================== */

/**
 * @brief Add a token to a parsed template's token list.
 *
 * @param tmpl The parsed template to add to.
 * @param token The token to add.
 */
static void template_add_token(lle_parsed_template_t *tmpl,
                               lle_template_token_t *token) {
    if (!tmpl || !token) {
        return;
    }

    if (tmpl->tail) {
        tmpl->tail->next = token;
    } else {
        tmpl->head = token;
    }
    tmpl->tail = token;
    tmpl->token_count++;
}

/**
 * @brief Find the closing brace, handling nested braces.
 *
 * @param p Pointer to the character after the opening brace.
 * @return Pointer to the closing brace, or NULL if not found.
 */
static const char *template_find_closing_brace(const char *p) {
    int depth = 1;
    while (*p && depth > 0) {
        if (*p == '{') {
            depth++;
        } else if (*p == '}') {
            depth--;
        }
        if (depth > 0) {
            p++;
        }
    }
    return (depth == 0) ? p : NULL;
}

/**
 * @brief Parse conditional content into a conditional token.
 *
 * Parses format: segment:true:false or segment.prop:true:false
 *
 * @param content The content string after the ? prefix.
 * @return Allocated conditional token, or NULL on failure.
 */
static lle_template_token_t *parse_conditional(const char *content) {
    /// Format: ?segment:true:false or ?segment.prop:true:false
    char segment[LLE_TEMPLATE_SEGMENT_MAX] = {0};
    char property[LLE_TEMPLATE_PROPERTY_MAX] = {0};
    char true_val[LLE_TEMPLATE_LITERAL_MAX] = {0};
    char false_val[LLE_TEMPLATE_LITERAL_MAX] = {0};

    const char *p = content;

    /// Parse segment name (and optional property)
    size_t seg_len = 0;
    while (*p && *p != ':' && *p != '.' && seg_len < sizeof(segment) - 1) {
        segment[seg_len++] = *p++;
    }
    segment[seg_len] = '\0';

    /// Check for property
    if (*p == '.') {
        p++; /// Skip .
        size_t prop_len = 0;
        while (*p && *p != ':' && prop_len < sizeof(property) - 1) {
            property[prop_len++] = *p++;
        }
        property[prop_len] = '\0';
    }

    /// Skip to true value
    if (*p == ':') {
        p++;
        size_t true_len = 0;
        while (*p && *p != ':' && true_len < sizeof(true_val) - 1) {
            true_val[true_len++] = *p++;
        }
        true_val[true_len] = '\0';
    }

    /// Skip to false value
    if (*p == ':') {
        p++;
        size_t false_len = 0;
        while (*p && false_len < sizeof(false_val) - 1) {
            false_val[false_len++] = *p++;
        }
        false_val[false_len] = '\0';
    }

    return lle_template_token_conditional(
        segment, strlen(property) > 0 ? property : NULL, true_val, false_val);
}

/**
 * @brief Parse color application syntax into a color token.
 *
 * Parses format: color:text
 *
 * @param content The content string.
 * @return Allocated color token, or NULL on failure.
 */
static lle_template_token_t *parse_color(const char *content) {
    char color[LLE_TEMPLATE_SEGMENT_MAX] = {0};
    char text[LLE_TEMPLATE_LITERAL_MAX] = {0};

    const char *colon = strchr(content, ':');
    if (!colon) {
        return NULL;
    }

    size_t color_len = colon - content;
    if (color_len >= sizeof(color)) {
        color_len = sizeof(color) - 1;
    }
    memcpy(color, content, color_len);
    color[color_len] = '\0';

    snprintf(text, sizeof(text), "%s", colon + 1);

    return lle_template_token_color(color, text);
}

/**
 * @brief Parse segment or property reference into a token.
 *
 * Parses format: segment or segment.property
 *
 * @param content The content string.
 * @return Allocated segment or property token, or NULL on failure.
 */
static lle_template_token_t *parse_segment_or_property(const char *content) {
    const char *dot = strchr(content, '.');
    if (dot) {
        /// Property access
        char segment[LLE_TEMPLATE_SEGMENT_MAX] = {0};
        char property[LLE_TEMPLATE_PROPERTY_MAX] = {0};

        size_t seg_len = dot - content;
        if (seg_len >= sizeof(segment)) {
            seg_len = sizeof(segment) - 1;
        }
        memcpy(segment, content, seg_len);
        segment[seg_len] = '\0';

        snprintf(property, sizeof(property), "%s", dot + 1);

        return lle_template_token_property(segment, property);
    } else {
        /// Simple segment
        return lle_template_token_segment(content);
    }
}

/* ========================================================================== */
/// Template Parsing
/* ========================================================================== */

/**
 * @brief Parse a template string into a token list.
 *
 * Tokenizes the template string, recognizing escape sequences,
 * variable references, conditionals, and color applications.
 *
 * @param template_str The template string to parse.
 * @param parsed Output pointer to receive the parsed template.
 * @return LLE_SUCCESS on success, or an error code on failure.
 */
lle_result_t lle_template_parse(const char *template_str,
                                lle_parsed_template_t **parsed) {
    if (!template_str || !parsed) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    lle_parsed_template_t *tmpl = calloc(1, sizeof(*tmpl));
    if (!tmpl) {
        return LLE_ERROR_OUT_OF_MEMORY;
    }

    snprintf(tmpl->original, sizeof(tmpl->original), "%s", template_str);

    const char *p = template_str;
    char literal_buf[LLE_TEMPLATE_LITERAL_MAX] = {0};
    size_t literal_pos = 0;

    while (*p) {
        /// Check for escape sequences
        if (*p == '\\' && *(p + 1)) {
            if (*(p + 1) == 'n') {
                /// Flush literal buffer first
                if (literal_pos > 0) {
                    lle_template_token_t *tok =
                        lle_template_token_literal(literal_buf, literal_pos);
                    if (tok) {
                        template_add_token(tmpl, tok);
                    }
                    literal_pos = 0;
                }
                template_add_token(tmpl, lle_template_token_newline());
                p += 2;
                continue;
            } else if (*(p + 1) == '$' || *(p + 1) == '\\') {
                /// Escaped character - add to literal
                if (literal_pos < sizeof(literal_buf) - 1) {
                    literal_buf[literal_pos++] = *(p + 1);
                }
                p += 2;
                continue;
            }
        }

        /// Check for variable/segment reference ${...}
        if (*p == '$' && *(p + 1) == '{') {
            /// Flush literal buffer
            if (literal_pos > 0) {
                lle_template_token_t *tok =
                    lle_template_token_literal(literal_buf, literal_pos);
                if (tok) {
                    template_add_token(tmpl, tok);
                }
                literal_pos = 0;
            }

            p += 2; /// Skip ${

            /// Find closing brace
            const char *end = template_find_closing_brace(p);
            if (!end) {
                lle_template_free(tmpl);
                return LLE_ERROR_INPUT_PARSING;
            }

            /// Extract content between braces
            size_t content_len = end - p;
            char content[LLE_TEMPLATE_LITERAL_MAX] = {0};
            if (content_len >= sizeof(content)) {
                content_len = sizeof(content) - 1;
            }
            memcpy(content, p, content_len);
            content[content_len] = '\0';

            /// Parse the content based on type
            lle_template_token_t *token = NULL;

            if (content[0] == '?') {
                /// Conditional: ${?segment:true:false}
                token = parse_conditional(content + 1);
            } else if (strchr(content, ':') && !strchr(content, '.')) {
                /// Color: ${color:text} - colon present, no dot
                token = parse_color(content);
            } else {
                /// Segment or property: ${segment} or ${segment.property}
                token = parse_segment_or_property(content);
            }

            if (token) {
                template_add_token(tmpl, token);
            }

            p = end + 1; /// Skip }
            continue;
        }

        /// Regular character
        if (literal_pos < sizeof(literal_buf) - 1) {
            literal_buf[literal_pos++] = *p;
        }
        p++;
    }

    /// Flush remaining literal
    if (literal_pos > 0) {
        lle_template_token_t *tok =
            lle_template_token_literal(literal_buf, literal_pos);
        if (tok) {
            template_add_token(tmpl, tok);
        }
    }

    /// Add end token
    template_add_token(tmpl, lle_template_token_end());

    tmpl->valid = true;
    *parsed = tmpl;

    return LLE_SUCCESS;
}

/**
 * @brief Free a parsed template and all its tokens.
 *
 * @param parsed The parsed template to free, or NULL for no-op.
 */
void lle_template_free(lle_parsed_template_t *parsed) {
    if (!parsed) {
        return;
    }

    lle_template_token_t *token = parsed->head;
    while (token) {
        lle_template_token_t *next = token->next;
        lle_template_token_free(token);
        token = next;
    }

    free(parsed);
}

/**
 * @brief Validate a template string for syntax errors.
 *
 * Checks for balanced braces and proper escape sequences.
 *
 * @param template_str The template string to validate.
 * @return true if valid, false otherwise.
 */
bool lle_template_validate(const char *template_str) {
    if (!template_str) {
        return false;
    }

    const char *p = template_str;
    int brace_depth = 0;

    while (*p) {
        if (*p == '\\' && *(p + 1)) {
            /// Skip escaped character
            p += 2;
            continue;
        }

        if (*p == '$' && *(p + 1) == '{') {
            brace_depth++;
            p += 2;
            continue;
        }

        if (*p == '{' && brace_depth > 0) {
            brace_depth++;
        } else if (*p == '}' && brace_depth > 0) {
            brace_depth--;
        }

        p++;
    }

    return (brace_depth == 0);
}

/* ========================================================================== */
/// Template Rendering
/* ========================================================================== */

/**
 * @brief Render a parsed template to a string.
 *
 * Evaluates all tokens using the provided render context callbacks.
 *
 * @param tmpl The parsed template to render.
 * @param render_ctx Context with callbacks for segment/color lookup.
 * @param output Buffer to receive the rendered output.
 * @param output_size Size of the output buffer.
 * @return LLE_SUCCESS on success, or an error code on failure.
 */
lle_result_t lle_template_render(const lle_parsed_template_t *tmpl,
                                 const lle_template_render_ctx_t *render_ctx,
                                 char *output, size_t output_size) {
    if (!tmpl || !render_ctx || !output || output_size == 0) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    if (!tmpl->valid) {
        return LLE_ERROR_INPUT_PARSING;
    }

    size_t pos = 0;
    lle_template_token_t *token = tmpl->head;

    while (token && token->type != LLE_TOKEN_END && pos < output_size - 1) {
        switch (token->type) {
        case LLE_TOKEN_LITERAL:
            /// Copy literal text
            {
                size_t avail = output_size - pos - 1;
                size_t copy_len = token->data.literal.length;
                if (copy_len > avail) {
                    copy_len = avail;
                }
                memcpy(output + pos, token->data.literal.text, copy_len);
                pos += copy_len;
            }
            break;

        case LLE_TOKEN_NEWLINE:
            if (pos < output_size - 1) {
                output[pos++] = '\n';
            }
            break;

        case LLE_TOKEN_SEGMENT:
            /// Render segment
            if (render_ctx->get_segment) {
                char *content =
                    render_ctx->get_segment(token->data.segment.segment_name,
                                            NULL, render_ctx->user_data);
                if (content) {
                    size_t len = strlen(content);
                    size_t avail = output_size - pos - 1;
                    if (len > avail) {
                        len = avail;
                    }
                    memcpy(output + pos, content, len);
                    pos += len;
                    free(content);
                }
            }
            break;

        case LLE_TOKEN_PROPERTY:
            /// Render segment property
            if (render_ctx->get_segment) {
                char *content = render_ctx->get_segment(
                    token->data.segment.segment_name,
                    token->data.segment.property_name, render_ctx->user_data);
                if (content) {
                    size_t len = strlen(content);
                    size_t avail = output_size - pos - 1;
                    if (len > avail) {
                        len = avail;
                    }
                    memcpy(output + pos, content, len);
                    pos += len;
                    free(content);
                }
            }
            break;

        case LLE_TOKEN_CONDITIONAL:
            /// Evaluate conditional
            {
                bool condition_met = false;

                if (render_ctx->is_visible) {
                    const char *prop =
                        token->data.conditional.check_property
                            ? token->data.conditional.condition_property
                            : NULL;
                    condition_met = render_ctx->is_visible(
                        token->data.conditional.condition_segment, prop,
                        render_ctx->user_data);
                }

                const char *branch = condition_met
                                         ? token->data.conditional.true_value
                                         : token->data.conditional.false_value;

                if (branch && strlen(branch) > 0) {
                    /* Recursively evaluate the branch content to expand
                     * nested segment references like ${git} inside
                     * conditionals like ${?git: (${git})} */
                    size_t avail = output_size - pos - 1;
                    char *temp = malloc(avail + 1);
                    if (temp) {
                        lle_result_t eval_result = lle_template_evaluate(
                            branch, render_ctx, temp, avail + 1);
                        if (eval_result == LLE_SUCCESS) {
                            size_t len = strlen(temp);
                            memcpy(output + pos, temp, len);
                            pos += len;
                        } else {
                            /// Fallback: copy branch literally
                            size_t len = strlen(branch);
                            if (len > avail) {
                                len = avail;
                            }
                            memcpy(output + pos, branch, len);
                            pos += len;
                        }
                        free(temp);
                    }
                }
            }
            break;

        case LLE_TOKEN_COLOR:
            /// Apply color to text
            {
                const char *color_code = "";
                if (render_ctx->get_color) {
                    color_code = render_ctx->get_color(
                        token->data.color.color_name, render_ctx->user_data);
                    if (!color_code) {
                        color_code = "";
                    }
                }

                /// Add color code
                size_t color_len = strlen(color_code);
                if (color_len > 0 && pos + color_len < output_size - 1) {
                    memcpy(output + pos, color_code, color_len);
                    pos += color_len;
                }

                /// Add text
                size_t text_len = strlen(token->data.color.text);
                size_t avail = output_size - pos - 1;
                if (text_len > avail) {
                    text_len = avail;
                }
                memcpy(output + pos, token->data.color.text, text_len);
                pos += text_len;

                /// Add reset if color was applied
                if (color_len > 0) {
                    const char *reset = "\033[0m";
                    size_t reset_len = strlen(reset);
                    if (pos + reset_len < output_size - 1) {
                        memcpy(output + pos, reset, reset_len);
                        pos += reset_len;
                    }
                }
            }
            break;

        default:
            break;
        }

        token = token->next;
    }

    output[pos] = '\0';
    return LLE_SUCCESS;
}

/**
 * @brief Parse and render a template string in one step.
 *
 * Convenience function that combines parsing and rendering.
 *
 * @param template_str The template string to evaluate.
 * @param render_ctx Context with callbacks for segment/color lookup.
 * @param output Buffer to receive the rendered output.
 * @param output_size Size of the output buffer.
 * @return LLE_SUCCESS on success, or an error code on failure.
 */
lle_result_t lle_template_evaluate(const char *template_str,
                                   const lle_template_render_ctx_t *render_ctx,
                                   char *output, size_t output_size) {
    if (!template_str || !render_ctx || !output || output_size == 0) {
        return LLE_ERROR_INVALID_PARAMETER;
    }

    lle_parsed_template_t *parsed = NULL;
    lle_result_t result = lle_template_parse(template_str, &parsed);
    if (result != LLE_SUCCESS) {
        return result;
    }

    result = lle_template_render(parsed, render_ctx, output, output_size);

    lle_template_free(parsed);
    return result;
}
