/**
 * @file node_to_source.c
 * @brief AST to source code regeneration for round-trip testing
 *
 * Provides functionality to convert an AST back to shell source code.
 * This enables round-trip testing: parse → AST → source → parse → AST2
 * where AST and AST2 should be structurally equivalent.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "node.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Dynamic string buffer for building output */
typedef struct {
    char *data;
    size_t len;
    size_t cap;
} strbuf_t;

static void strbuf_init(strbuf_t *buf) {
    buf->data = malloc(256);
    buf->data[0] = '\0';
    buf->len = 0;
    buf->cap = 256;
}

static void strbuf_ensure(strbuf_t *buf, size_t needed) {
    if (buf->len + needed >= buf->cap) {
        while (buf->len + needed >= buf->cap) {
            buf->cap *= 2;
        }
        buf->data = realloc(buf->data, buf->cap);
    }
}

static void strbuf_append(strbuf_t *buf, const char *str) {
    if (!str)
        return;
    size_t len = strlen(str);
    strbuf_ensure(buf, len + 1);
    memcpy(buf->data + buf->len, str, len + 1);
    buf->len += len;
}

/* Forward declaration */
static void node_to_source_impl(node_t *node, strbuf_t *buf, int depth);

/**
 * @brief Check if node is a redirection type
 */
static int is_redirection(node_t *node) {
    if (!node)
        return 0;
    switch (node->type) {
    case NODE_REDIR_IN:
    case NODE_REDIR_OUT:
    case NODE_REDIR_APPEND:
    case NODE_REDIR_ERR:
    case NODE_REDIR_IN_FD:
    case NODE_REDIR_ERR_APPEND:
    case NODE_REDIR_HEREDOC:
    case NODE_REDIR_HEREDOC_STRIP:
    case NODE_REDIR_HERESTRING:
    case NODE_REDIR_BOTH:
    case NODE_REDIR_BOTH_APPEND:
    case NODE_REDIR_FD:
    case NODE_REDIR_FD_ALLOC:
    case NODE_REDIR_CLOBBER:
        return 1;
    default:
        return 0;
    }
}

/**
 * @brief Convert redirection node to source
 *
 * Note: In the parser, redir_node->val.str contains the operator text,
 * and the target filename is stored in the first child NODE_VAR node.
 */
static void redir_to_source(node_t *node, strbuf_t *buf) {
    if (!node)
        return;

    /* Get target from first child (NODE_VAR with filename) */
    const char *target = "";
    if (node->first_child && node->first_child->val_type == VAL_STR &&
        node->first_child->val.str) {
        target = node->first_child->val.str;
    }

    switch (node->type) {
    case NODE_REDIR_IN:
        strbuf_append(buf, "<");
        strbuf_append(buf, target);
        break;
    case NODE_REDIR_OUT:
        strbuf_append(buf, ">");
        strbuf_append(buf, target);
        break;
    case NODE_REDIR_APPEND:
        strbuf_append(buf, ">>");
        strbuf_append(buf, target);
        break;
    case NODE_REDIR_ERR:
        strbuf_append(buf, "2>");
        strbuf_append(buf, target);
        break;
    case NODE_REDIR_ERR_APPEND:
        strbuf_append(buf, "2>>");
        strbuf_append(buf, target);
        break;
    case NODE_REDIR_HEREDOC:
        strbuf_append(buf, "<<");
        strbuf_append(buf, target);
        break;
    case NODE_REDIR_HEREDOC_STRIP:
        strbuf_append(buf, "<<-");
        strbuf_append(buf, target);
        break;
    case NODE_REDIR_HERESTRING:
        strbuf_append(buf, "<<<");
        strbuf_append(buf, target);
        break;
    case NODE_REDIR_BOTH:
        strbuf_append(buf, "&>");
        strbuf_append(buf, target);
        break;
    case NODE_REDIR_BOTH_APPEND:
        strbuf_append(buf, "&>>");
        strbuf_append(buf, target);
        break;
    case NODE_REDIR_FD:
        strbuf_append(buf, ">&");
        strbuf_append(buf, target);
        break;
    case NODE_REDIR_CLOBBER:
        strbuf_append(buf, ">|");
        strbuf_append(buf, target);
        break;
    default:
        strbuf_append(buf, ">");
        strbuf_append(buf, target);
        break;
    }
}

/**
 * @brief Recursive implementation of node to source conversion
 */
static void node_to_source_impl(node_t *node, strbuf_t *buf, int depth) {
    if (!node)
        return;

    const char *str =
        (node->val_type == VAL_STR && node->val.str) ? node->val.str : "";

    switch (node->type) {
    case NODE_COMMAND: {
        /* Command name is in val.str, children are arguments and redirections
         */
        strbuf_append(buf, str); /* Command name */
        node_t *child = node->first_child;
        while (child) {
            strbuf_append(buf, " ");
            if (is_redirection(child)) {
                redir_to_source(child, buf);
            } else {
                node_to_source_impl(child, buf, depth);
            }
            child = child->next_sibling;
        }
        break;
    }

    case NODE_VAR:
        strbuf_append(buf, str);
        break;

    case NODE_STRING_LITERAL:
        strbuf_append(buf, "'");
        strbuf_append(buf, str);
        strbuf_append(buf, "'");
        break;

    case NODE_STRING_EXPANDABLE:
        strbuf_append(buf, "\"");
        strbuf_append(buf, str);
        strbuf_append(buf, "\"");
        break;

    case NODE_ARITH_EXP:
        /* str already contains the full $((expr)) or just expr */
        if (str[0] == '$') {
            strbuf_append(buf, str);
        } else {
            strbuf_append(buf, "$((");
            strbuf_append(buf, str);
            strbuf_append(buf, "))");
        }
        break;

    case NODE_COMMAND_SUB:
        /* str already contains the full $(cmd) or just cmd */
        if (str[0] == '$') {
            strbuf_append(buf, str);
        } else if (node->first_child) {
            strbuf_append(buf, "$(");
            node_to_source_impl(node->first_child, buf, depth);
            strbuf_append(buf, ")");
        } else {
            strbuf_append(buf, "$(");
            strbuf_append(buf, str);
            strbuf_append(buf, ")");
        }
        break;

    case NODE_PIPE: {
        /* Pipe has two children: left and right */
        node_t *left = node->first_child;
        node_t *right = left ? left->next_sibling : NULL;
        if (left)
            node_to_source_impl(left, buf, depth);
        strbuf_append(buf, " | ");
        if (right)
            node_to_source_impl(right, buf, depth);
        break;
    }

    case NODE_PIPELINE: {
        node_t *child = node->first_child;
        int first = 1;
        while (child) {
            if (!first)
                strbuf_append(buf, " | ");
            first = 0;
            node_to_source_impl(child, buf, depth);
            child = child->next_sibling;
        }
        break;
    }

    case NODE_COMMAND_LIST: {
        node_t *child = node->first_child;
        int first = 1;
        while (child) {
            if (!first)
                strbuf_append(buf, "; ");
            first = 0;
            node_to_source_impl(child, buf, depth);
            child = child->next_sibling;
        }
        break;
    }

    case NODE_LOGICAL_AND: {
        node_t *left = node->first_child;
        node_t *right = left ? left->next_sibling : NULL;
        if (left)
            node_to_source_impl(left, buf, depth);
        strbuf_append(buf, " && ");
        if (right)
            node_to_source_impl(right, buf, depth);
        break;
    }

    case NODE_LOGICAL_OR: {
        node_t *left = node->first_child;
        node_t *right = left ? left->next_sibling : NULL;
        if (left)
            node_to_source_impl(left, buf, depth);
        strbuf_append(buf, " || ");
        if (right)
            node_to_source_impl(right, buf, depth);
        break;
    }

    case NODE_BACKGROUND:
        if (node->first_child) {
            node_to_source_impl(node->first_child, buf, depth);
        }
        strbuf_append(buf, " &");
        break;

    case NODE_NEGATE:
        strbuf_append(buf, "! ");
        if (node->first_child) {
            node_to_source_impl(node->first_child, buf, depth);
        }
        break;

    case NODE_SUBSHELL:
        strbuf_append(buf, "( ");
        if (node->first_child) {
            node_to_source_impl(node->first_child, buf, depth + 1);
        }
        strbuf_append(buf, " )");
        break;

    case NODE_BRACE_GROUP:
        strbuf_append(buf, "{ ");
        if (node->first_child) {
            node_to_source_impl(node->first_child, buf, depth + 1);
        }
        strbuf_append(buf, "; }");
        break;

    case NODE_IF: {
        /* if condition; then body; [elif cond; then body;]* [else body;] fi */
        strbuf_append(buf, "if ");
        node_t *child = node->first_child;
        if (child) {
            /* Condition */
            node_to_source_impl(child, buf, depth + 1);
            child = child->next_sibling;
        }
        strbuf_append(buf, "; then ");
        if (child) {
            /* Then body */
            node_to_source_impl(child, buf, depth + 1);
            child = child->next_sibling;
        }
        /* Handle elif/else - remaining children come in pairs (cond, body)
           or single (else body) */
        while (child) {
            node_t *next = child->next_sibling;
            if (next) {
                /* elif */
                strbuf_append(buf, "; elif ");
                node_to_source_impl(child, buf, depth + 1);
                strbuf_append(buf, "; then ");
                node_to_source_impl(next, buf, depth + 1);
                child = next->next_sibling;
            } else {
                /* else */
                strbuf_append(buf, "; else ");
                node_to_source_impl(child, buf, depth + 1);
                child = NULL;
            }
        }
        strbuf_append(buf, "; fi");
        break;
    }

    case NODE_FOR: {
        /* for var in words; do body; done */
        strbuf_append(buf, "for ");
        strbuf_append(buf, str); /* variable name */
        node_t *child = node->first_child;
        if (child) {
            strbuf_append(buf, " in ");
            /* Word list - first child contains words as its children */
            node_t *word = child->first_child;
            int first = 1;
            while (word) {
                if (!first)
                    strbuf_append(buf, " ");
                first = 0;
                node_to_source_impl(word, buf, depth);
                word = word->next_sibling;
            }
            child = child->next_sibling;
        }
        strbuf_append(buf, "; do ");
        if (child) {
            node_to_source_impl(child, buf, depth + 1);
        }
        strbuf_append(buf, "; done");
        break;
    }

    case NODE_FOR_ARITH:
        /* for (( init; test; update )); do body; done */
        strbuf_append(buf, "for ((");
        strbuf_append(buf, str);
        strbuf_append(buf, ")); do ");
        if (node->first_child) {
            node_to_source_impl(node->first_child, buf, depth + 1);
        }
        strbuf_append(buf, "; done");
        break;

    case NODE_WHILE:
        strbuf_append(buf, "while ");
        if (node->first_child) {
            node_to_source_impl(node->first_child, buf, depth + 1);
            if (node->first_child->next_sibling) {
                strbuf_append(buf, "; do ");
                node_to_source_impl(node->first_child->next_sibling, buf,
                                    depth + 1);
            }
        }
        strbuf_append(buf, "; done");
        break;

    case NODE_UNTIL:
        strbuf_append(buf, "until ");
        if (node->first_child) {
            node_to_source_impl(node->first_child, buf, depth + 1);
            if (node->first_child->next_sibling) {
                strbuf_append(buf, "; do ");
                node_to_source_impl(node->first_child->next_sibling, buf,
                                    depth + 1);
            }
        }
        strbuf_append(buf, "; done");
        break;

    case NODE_CASE: {
        /* case word in pattern) body;; ... esac */
        strbuf_append(buf, "case ");
        strbuf_append(buf, str);
        strbuf_append(buf, " in ");
        node_t *item = node->first_child;
        while (item) {
            node_to_source_impl(item, buf, depth + 1);
            strbuf_append(buf, " ");
            item = item->next_sibling;
        }
        strbuf_append(buf, "esac");
        break;
    }

    case NODE_CASE_ITEM: {
        /* Pattern format: "<terminator_char><pattern>"
           where terminator_char is '0'=;;, '1'=;&, '2'=;;& */
        const char *pattern = str;
        char terminator_char = '0';
        if (str[0] >= '0' && str[0] <= '2') {
            terminator_char = str[0];
            pattern = str + 1; /* Skip prefix */
        }
        strbuf_append(buf, pattern);
        strbuf_append(buf, ") ");
        if (node->first_child) {
            node_to_source_impl(node->first_child, buf, depth);
        }
        /* Output terminator based on prefix */
        switch (terminator_char) {
        case '1':
            strbuf_append(buf, " ;&");
            break;
        case '2':
            strbuf_append(buf, " ;;&");
            break;
        default:
            strbuf_append(buf, " ;;");
            break;
        }
        break;
    }

    case NODE_FUNCTION:
        strbuf_append(buf, str); /* function name */
        strbuf_append(buf, "() { ");
        if (node->first_child) {
            node_to_source_impl(node->first_child, buf, depth + 1);
        }
        strbuf_append(buf, "; }");
        break;

    case NODE_ARITH_CMD:
        strbuf_append(buf, "((");
        strbuf_append(buf, str);
        strbuf_append(buf, "))");
        break;

    case NODE_EXTENDED_TEST:
        strbuf_append(buf, "[[ ");
        strbuf_append(buf, str);
        strbuf_append(buf, " ]]");
        break;

    case NODE_PROC_SUB_IN:
        strbuf_append(buf, "<(");
        if (node->first_child) {
            node_to_source_impl(node->first_child, buf, depth);
        }
        strbuf_append(buf, ")");
        break;

    case NODE_PROC_SUB_OUT:
        strbuf_append(buf, ">(");
        if (node->first_child) {
            node_to_source_impl(node->first_child, buf, depth);
        }
        strbuf_append(buf, ")");
        break;

    case NODE_COPROC:
        strbuf_append(buf, "coproc ");
        if (str[0]) {
            strbuf_append(buf, str);
            strbuf_append(buf, " ");
        }
        if (node->first_child) {
            node_to_source_impl(node->first_child, buf, depth);
        }
        break;

    case NODE_SELECT:
        strbuf_append(buf, "select ");
        strbuf_append(buf, str);
        if (node->first_child) {
            strbuf_append(buf, " in ");
            node_t *words = node->first_child;
            node_t *word = words->first_child;
            int first = 1;
            while (word) {
                if (!first)
                    strbuf_append(buf, " ");
                first = 0;
                node_to_source_impl(word, buf, depth);
                word = word->next_sibling;
            }
            if (words->next_sibling) {
                strbuf_append(buf, "; do ");
                node_to_source_impl(words->next_sibling, buf, depth + 1);
            }
        }
        strbuf_append(buf, "; done");
        break;

    case NODE_TIME:
        strbuf_append(buf, "time ");
        if (node->val.sint) {
            strbuf_append(buf, "-p ");
        }
        if (node->first_child) {
            node_to_source_impl(node->first_child, buf, depth);
        }
        break;

    case NODE_ANON_FUNCTION:
        strbuf_append(buf, "() ");
        if (node->first_child) {
            node_to_source_impl(node->first_child, buf, depth + 1);
        }
        break;

    case NODE_ARRAY_LITERAL:
        strbuf_append(buf, "(");
        {
            node_t *child = node->first_child;
            int first = 1;
            while (child) {
                if (!first)
                    strbuf_append(buf, " ");
                first = 0;
                node_to_source_impl(child, buf, depth);
                child = child->next_sibling;
            }
        }
        strbuf_append(buf, ")");
        break;

    case NODE_ARRAY_ACCESS:
        strbuf_append(buf, "${");
        strbuf_append(buf, str);
        strbuf_append(buf, "}");
        break;

    case NODE_ARRAY_ASSIGN:
        strbuf_append(buf, str);
        strbuf_append(buf, "=(");
        {
            node_t *child = node->first_child;
            int first = 1;
            while (child) {
                if (!first)
                    strbuf_append(buf, " ");
                first = 0;
                node_to_source_impl(child, buf, depth);
                child = child->next_sibling;
            }
        }
        strbuf_append(buf, ")");
        break;

    case NODE_ARRAY_APPEND:
        strbuf_append(buf, str);
        strbuf_append(buf, "+=(");
        {
            node_t *child = node->first_child;
            int first = 1;
            while (child) {
                if (!first)
                    strbuf_append(buf, " ");
                first = 0;
                node_to_source_impl(child, buf, depth);
                child = child->next_sibling;
            }
        }
        strbuf_append(buf, ")");
        break;

    default:
        /* For any unhandled node type, try to output the string value */
        if (str[0]) {
            strbuf_append(buf, str);
        }
        break;
    }
}

/**
 * @brief Convert an AST node back to shell source code
 *
 * Regenerates shell source code from an AST. The output is normalized
 * (canonical form) - original whitespace and formatting may differ.
 *
 * @param node Root node of AST to convert
 * @return Newly allocated string with shell source, or NULL on error.
 *         Caller must free the returned string.
 */
char *node_to_source(node_t *node) {
    if (!node)
        return strdup("");

    strbuf_t buf;
    strbuf_init(&buf);

    node_to_source_impl(node, &buf, 0);

    return buf.data;
}

/**
 * @brief Compare two AST nodes for structural equality
 *
 * Recursively compares two AST nodes to verify they have the same
 * structure, types, and values. Used for round-trip testing.
 *
 * @param a First node
 * @param b Second node
 * @return 1 if structurally equal, 0 if different
 */
int node_equals(node_t *a, node_t *b) {
    /* Both NULL is equal */
    if (!a && !b)
        return 1;
    /* One NULL, one not is unequal */
    if (!a || !b)
        return 0;

    /* Type must match */
    if (a->type != b->type)
        return 0;

    /* Value type must match */
    if (a->val_type != b->val_type)
        return 0;

    /* Value must match (for string values) */
    if (a->val_type == VAL_STR) {
        const char *sa = a->val.str ? a->val.str : "";
        const char *sb = b->val.str ? b->val.str : "";
        if (strcmp(sa, sb) != 0)
            return 0;
    } else if (a->val_type == VAL_SINT) {
        if (a->val.sint != b->val.sint)
            return 0;
    }

    /* Child count must match */
    if (a->children != b->children)
        return 0;

    /* Compare all children */
    node_t *ca = a->first_child;
    node_t *cb = b->first_child;
    while (ca && cb) {
        if (!node_equals(ca, cb))
            return 0;
        ca = ca->next_sibling;
        cb = cb->next_sibling;
    }

    /* If one has more children, not equal */
    if (ca || cb)
        return 0;

    return 1;
}
