/**
 * @file redirection.c
 * @brief I/O redirection implementation
 *
 * Comprehensive implementation of POSIX shell I/O redirection including:
 * - Basic output redirection (>)
 * - Append redirection (>>)
 * - Input redirection (<)
 * - Error redirection (2>, 2>>)
 * - Combined redirection (&>)
 * - Here strings (<<<)
 * - Here documents (<<, <<-)
 * - File descriptor redirection (>&2, 2>&1)
 * - Noclobber support (>|)
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "redirection.h"

#include "executor.h"
#include "lle/unicode_compare.h"
#include "lush.h"
#include "lush_fork.h"
#include "node.h"
#include "restricted_mode.h"
#include "shell_error.h"
#include "signals.h"
#include "symtable.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/*
 * Write all `len` bytes from `buf` to `fd`, looping over short writes
 * and retrying on EINTR. Returns true if the full payload was delivered.
 * Used for heredoc / herestring pipe-feeder paths where a partial write
 * would deliver truncated stdin to the child.
 */
static bool redir_write_all(int fd, const void *buf, size_t len) {
    const char *p = (const char *)buf;
    while (len > 0) {
        ssize_t n = write(fd, p, len);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        p += n;
        len -= (size_t)n;
    }
    return true;
}

/// Forward declarations
static int handle_redirection_node(executor_t *executor, node_t *redir_node);
static int setup_here_document(const char *delimiter, bool strip_tabs);
static int setup_here_document_with_content(const char *content);
static int setup_here_document_with_processing(executor_t *executor,
                                               const char *content,
                                               bool strip_tabs,
                                               bool expand_vars);
static int setup_here_string(executor_t *executor, const char *content);
static int setup_fd_redirection(executor_t *executor, const char *redir_text);
static int setup_fd_alloc_redirection(executor_t *executor, node_t *redir_node);
static int find_available_fd(int min_fd);

/**
 * @brief Setup redirections for a command
 *
 * Processes all redirection nodes attached to a command in left-to-right
 * order as required by POSIX. Sets up file descriptors accordingly.
 *
 * @param executor Executor context for variable expansion
 * @param command Command node containing redirection children
 * @return 0 on success, non-zero on error
 */
int setup_redirections(executor_t *executor, node_t *command) {
    if (!command) {
        return 0; /// No redirections to setup
    }

    /// POSIX compliant: Process redirections left-to-right in order they appear
    node_t *child = command->first_child;
    while (child) {
        /// NODE_REDIR_CLOBBER (>|) is the last redirection node type; a bound
        /// of NODE_REDIR_FD_ALLOC silently skipped it, so `>|` never took
        /// effect and its output leaked past the redirect. Matches
        /// is_redirection_node's full range.
        if (child->type >= NODE_REDIR_IN && child->type <= NODE_REDIR_CLOBBER) {
            int result = handle_redirection_node(executor, child);
            if (result != 0) {
                return result;
            }
        }
        child = child->next_sibling;
    }

    return 0;
}

/**
 * @brief Handle individual redirection node
 *
 * Processes a single redirection node, setting up the appropriate
 * file descriptor manipulation based on redirection type.
 *
 * @param executor Executor context for variable expansion
 * @param redir_node Redirection node to process
 * @return 0 on success, non-zero on error
 */
static int handle_redirection_node(executor_t *executor, node_t *redir_node) {
    if (!redir_node) {
        return 1;
    }

    /// Restricted-shell mode forbids output redirections to files
    /// (>, >|, >>, <>, >&FILE, &>FILE). Input redirection (<, <<,
    /// <<<) and FD-to-FD duplication (2>&1) are still allowed --
    /// they don't create or overwrite files. Matches bash's rbash
    /// restrictions exactly.
    if (restricted_mode_is_engaged()) {
        bool reject = false;
        switch (redir_node->type) {
        case NODE_REDIR_OUT:         /// >
        case NODE_REDIR_APPEND:      /// >>
        case NODE_REDIR_CLOBBER:     /// >|
        case NODE_REDIR_ERR:         /// N> (2>, 3>, etc.)
        case NODE_REDIR_ERR_APPEND:  /// N>>
        case NODE_REDIR_BOTH:        /// &>
        case NODE_REDIR_BOTH_APPEND: /// &>>
            reject = true;
            break;
        default:
            break;
        }
        if (reject) {
            executor_error_report(executor, SHELL_ERR_PERMISSION_DENIED,
                                  redir_node->loc,
                                  "restricted: output redirection forbidden");
            return 1;
        }
    }

    if (getenv("LUSH_DEBUG_REDIR")) {
        printf("DEBUG: handle_redirection_node called with type %d\n",
               redir_node->type);
    }

    /// For here documents, check if we have pre-collected content
    if (redir_node->type == NODE_REDIR_HEREDOC ||
        redir_node->type == NODE_REDIR_HEREDOC_STRIP) {
        /// The delimiter is stored in the redirection node value
        /// The content is stored in the first child node
        /// The expand variables flag is stored in the second child node
        node_t *content_node = redir_node->first_child;
        node_t *expand_flag_node =
            content_node ? content_node->next_sibling : NULL;

        if (content_node && content_node->val.str) {
            bool strip_tabs = (redir_node->type == NODE_REDIR_HEREDOC_STRIP);
            bool expand_vars = true; /// Default to expand

            /// Check expand variables flag from parser
            if (expand_flag_node && expand_flag_node->val.str) {
                expand_vars = (strcmp(expand_flag_node->val.str, "1") == 0);
            }

            return setup_here_document_with_processing(
                executor, content_node->val.str, strip_tabs, expand_vars);
        }
        /// If no content node, fall back to interactive here document
        if (redir_node->val.str) {
            return setup_here_document(redir_node->val.str,
                                       redir_node->type ==
                                           NODE_REDIR_HEREDOC_STRIP);
        }
        return 1; /// No delimiter found
    }

    /// Handle NODE_REDIR_FD first since it doesn't need a target child
    if (redir_node->type == NODE_REDIR_FD) {
        /// File descriptor redirection: >&2, 2>&1, <&3, >&$VAR, etc.
        return setup_fd_redirection(executor, redir_node->val.str);
    }

    /// Handle NODE_REDIR_FD_ALLOC for {varname}> fd allocation syntax
    if (redir_node->type == NODE_REDIR_FD_ALLOC) {
        return setup_fd_alloc_redirection(executor, redir_node);
    }

    /// Get the target (filename) from the first child for other redirections
    node_t *target_node = redir_node->first_child;
    if (!target_node) {
        return 1; /// No target specified
    }

    /// Check if target is a process substitution node (< <(cmd) or > >(cmd))
    /// This is valid bash/zsh syntax for redirecting from/to process
    /// substitution
    if (target_node->type == NODE_PROC_SUB_IN ||
        target_node->type == NODE_PROC_SUB_OUT) {
        /// Expand the process substitution to get /dev/fd/N path
        char *proc_sub_path =
            expand_process_substitution(executor, target_node);
        if (!proc_sub_path) {
            shell_error_t *error = shell_error_create(
                SHELL_ERR_PROCESS_SUBST, SHELL_SEVERITY_ERROR, redir_node->loc,
                "process substitution expansion failed");
            char *src_line =
                executor_get_source_line(executor, redir_node->loc.line);
            if (src_line) {
                shell_error_set_source_line(
                    error, src_line, redir_node->loc.column,
                    redir_node->loc.column + redir_node->loc.length);
                free(src_line);
            }
            /// Copy executor's context stack to error
            for (size_t i = 0;
                 i < executor->context_depth && i < SHELL_ERROR_CONTEXT_MAX;
                 i++) {
                if (executor->context_stack[i]) {
                    shell_error_push_context(error, "%s",
                                             executor->context_stack[i]);
                }
            }
            shell_error_set_suggestion(
                error, "ensure the command inside <(...) or >(...) is valid");
            shell_error_display(error, stderr, isatty(STDERR_FILENO));
            shell_error_free(error);
            return 1;
        }

        /// Now use the /dev/fd/N path as the target for redirection
        int result = 0;
        int fd;

        switch (redir_node->type) {
        case NODE_REDIR_IN:
            /// < <(cmd) - redirect stdin from process substitution
            fd = open(proc_sub_path, O_RDONLY);
            if (fd == -1) {
                shell_error_t *error = shell_error_create(
                    SHELL_ERR_FILE_NOT_FOUND, SHELL_SEVERITY_ERROR,
                    redir_node->loc, "%s: %s", proc_sub_path, strerror(errno));
                for (size_t i = 0;
                     i < executor->context_depth && i < SHELL_ERROR_CONTEXT_MAX;
                     i++) {
                    if (executor->context_stack[i]) {
                        shell_error_push_context(error, "%s",
                                                 executor->context_stack[i]);
                    }
                }
                char *src_line =
                    executor_get_source_line(executor, redir_node->loc.line);
                if (src_line) {
                    shell_error_set_source_line(
                        error, src_line, redir_node->loc.column,
                        redir_node->loc.column + redir_node->loc.length);
                    free(src_line);
                }
                shell_error_display(error, stderr, isatty(STDERR_FILENO));
                shell_error_free(error);
                result = 1;
            } else {
                if (dup2(fd, STDIN_FILENO) == -1) {
                    shell_error_t *error = shell_error_create(
                        SHELL_ERR_BAD_FD, SHELL_SEVERITY_ERROR, redir_node->loc,
                        "dup2: %s", strerror(errno));
                    char *src_line = executor_get_source_line(
                        executor, redir_node->loc.line);
                    if (src_line) {
                        shell_error_set_source_line(
                            error, src_line, redir_node->loc.column,
                            redir_node->loc.column + redir_node->loc.length);
                        free(src_line);
                    }
                    for (size_t i = 0; i < executor->context_depth &&
                                       i < SHELL_ERROR_CONTEXT_MAX;
                         i++) {
                        if (executor->context_stack[i]) {
                            shell_error_push_context(
                                error, "%s", executor->context_stack[i]);
                        }
                    }
                    shell_error_display(error, stderr, isatty(STDERR_FILENO));
                    shell_error_free(error);
                    result = 1;
                }
                close(fd);
            }
            break;

        case NODE_REDIR_OUT:
            /// > >(cmd) - redirect stdout to process substitution
            fd = open(proc_sub_path, O_WRONLY);
            if (fd == -1) {
                shell_error_t *error = shell_error_create(
                    SHELL_ERR_FILE_NOT_FOUND, SHELL_SEVERITY_ERROR,
                    redir_node->loc, "%s: %s", proc_sub_path, strerror(errno));
                for (size_t i = 0;
                     i < executor->context_depth && i < SHELL_ERROR_CONTEXT_MAX;
                     i++) {
                    if (executor->context_stack[i]) {
                        shell_error_push_context(error, "%s",
                                                 executor->context_stack[i]);
                    }
                }
                char *src_line =
                    executor_get_source_line(executor, redir_node->loc.line);
                if (src_line) {
                    shell_error_set_source_line(
                        error, src_line, redir_node->loc.column,
                        redir_node->loc.column + redir_node->loc.length);
                    free(src_line);
                }
                shell_error_display(error, stderr, isatty(STDERR_FILENO));
                shell_error_free(error);
                result = 1;
            } else {
                if (dup2(fd, STDOUT_FILENO) == -1) {
                    shell_error_t *error = shell_error_create(
                        SHELL_ERR_BAD_FD, SHELL_SEVERITY_ERROR, redir_node->loc,
                        "dup2: %s", strerror(errno));
                    char *src_line = executor_get_source_line(
                        executor, redir_node->loc.line);
                    if (src_line) {
                        shell_error_set_source_line(
                            error, src_line, redir_node->loc.column,
                            redir_node->loc.column + redir_node->loc.length);
                        free(src_line);
                    }
                    for (size_t i = 0; i < executor->context_depth &&
                                       i < SHELL_ERROR_CONTEXT_MAX;
                         i++) {
                        if (executor->context_stack[i]) {
                            shell_error_push_context(
                                error, "%s", executor->context_stack[i]);
                        }
                    }
                    shell_error_display(error, stderr, isatty(STDERR_FILENO));
                    shell_error_free(error);
                    result = 1;
                }
                close(fd);
            }
            break;

        default:
            /// Other redirection types with process substitution
            {
                shell_error_t *error = shell_error_create(
                    SHELL_ERR_INVALID_REDIRECT, SHELL_SEVERITY_ERROR,
                    redir_node->loc,
                    "unsupported redirection type with process substitution");
                shell_error_set_suggestion(
                    error, "use '< <(cmd)' to read from a command or "
                           "'> >(cmd)' to write to one");
                char *src_line =
                    executor_get_source_line(executor, redir_node->loc.line);
                if (src_line) {
                    shell_error_set_source_line(
                        error, src_line, redir_node->loc.column,
                        redir_node->loc.column + redir_node->loc.length);
                    free(src_line);
                }
                for (size_t i = 0;
                     i < executor->context_depth && i < SHELL_ERROR_CONTEXT_MAX;
                     i++) {
                    if (executor->context_stack[i]) {
                        shell_error_push_context(error, "%s",
                                                 executor->context_stack[i]);
                    }
                }
                shell_error_display(error, stderr, isatty(STDERR_FILENO));
                shell_error_free(error);
                result = 1;
            }
            break;
        }

        free(proc_sub_path);
        return result;
    }

    /// Regular file target - must have val.str
    if (!target_node->val.str) {
        return 1; /// No target specified
    }

    /// Expand the operand through the shared per-node-type word expander so
    /// a redirection or here-string target accepts exactly what a command
    /// argument does: bare $(...) / $((...)) / `...`, double-quote rules for
    /// a "..." operand, and $'...' ANSI-C decoding. A single-quoted
    /// NODE_STRING_LITERAL still reaches the target verbatim -- no $VAR, no
    /// backticks, no backslash escapes -- so `read -r line <<< 'literal \n
    /// stays \\ here'` preserves its backslashes the way bash and zsh do;
    /// expand_arg_node decodes only the ANSI-C $'...' literal form.
    char *target = expand_arg_node(executor, target_node);
    if (!target) {
        return 1;
    }

    /// Privileged mode security check for redirection target
    if (!is_privileged_redirection_allowed(target)) {
        fprintf(stderr,
                "lush: %s: restricted redirection target in privileged mode\n",
                target);
        free(target);
        return 1;
    }

    int result = 0;

    switch (redir_node->type) {
    case NODE_REDIR_OUT: {
        /// Standard output redirection: command > file
        /// Check for noclobber: prevent overwriting existing files
        if (is_noclobber_enabled()) {
            struct stat st;
            if (stat(target, &st) == 0) {
                /// File exists and noclobber is enabled
                fprintf(
                    stderr,
                    "lush: %s: cannot overwrite existing file (noclobber)\n",
                    target);
                result = 1;
                break;
            }
        }

        int fd = open(target, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd == -1) {
            shell_error_t *error = shell_error_create(
                SHELL_ERR_FILE_NOT_FOUND, SHELL_SEVERITY_ERROR, redir_node->loc,
                "%s: %s", target, strerror(errno));
            char *src_line =
                executor_get_source_line(executor, redir_node->loc.line);
            if (src_line) {
                shell_error_set_source_line(
                    error, src_line, redir_node->loc.column,
                    redir_node->loc.column + redir_node->loc.length);
                free(src_line);
            }
            for (size_t i = 0;
                 i < executor->context_depth && i < SHELL_ERROR_CONTEXT_MAX;
                 i++) {
                if (executor->context_stack[i]) {
                    shell_error_push_context(error, "%s",
                                             executor->context_stack[i]);
                }
            }
            shell_error_display(error, stderr, isatty(STDERR_FILENO));
            shell_error_free(error);
            result = 1;
            break;
        }
        if (dup2(fd, STDOUT_FILENO) == -1) {
            shell_error_t *error = shell_error_create(
                SHELL_ERR_BAD_FD, SHELL_SEVERITY_ERROR, redir_node->loc,
                "dup2: %s", strerror(errno));
            char *src_line =
                executor_get_source_line(executor, redir_node->loc.line);
            if (src_line) {
                shell_error_set_source_line(
                    error, src_line, redir_node->loc.column,
                    redir_node->loc.column + redir_node->loc.length);
                free(src_line);
            }
            for (size_t i = 0;
                 i < executor->context_depth && i < SHELL_ERROR_CONTEXT_MAX;
                 i++) {
                if (executor->context_stack[i]) {
                    shell_error_push_context(error, "%s",
                                             executor->context_stack[i]);
                }
            }
            shell_error_display(error, stderr, isatty(STDERR_FILENO));
            shell_error_free(error);
            result = 1;
        }
        close(fd);
        break;
    }

    case NODE_REDIR_APPEND: {
        /// Append output redirection: command >> file
        int fd = open(target, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fd == -1) {
            shell_error_t *error = shell_error_create(
                SHELL_ERR_FILE_NOT_FOUND, SHELL_SEVERITY_ERROR, redir_node->loc,
                "%s: %s", target, strerror(errno));
            char *src_line =
                executor_get_source_line(executor, redir_node->loc.line);
            if (src_line) {
                shell_error_set_source_line(
                    error, src_line, redir_node->loc.column,
                    redir_node->loc.column + redir_node->loc.length);
                free(src_line);
            }
            for (size_t i = 0;
                 i < executor->context_depth && i < SHELL_ERROR_CONTEXT_MAX;
                 i++) {
                if (executor->context_stack[i]) {
                    shell_error_push_context(error, "%s",
                                             executor->context_stack[i]);
                }
            }
            shell_error_display(error, stderr, isatty(STDERR_FILENO));
            shell_error_free(error);
            result = 1;
            break;
        }
        if (dup2(fd, STDOUT_FILENO) == -1) {
            shell_error_t *error = shell_error_create(
                SHELL_ERR_BAD_FD, SHELL_SEVERITY_ERROR, redir_node->loc,
                "dup2: %s", strerror(errno));
            char *src_line =
                executor_get_source_line(executor, redir_node->loc.line);
            if (src_line) {
                shell_error_set_source_line(
                    error, src_line, redir_node->loc.column,
                    redir_node->loc.column + redir_node->loc.length);
                free(src_line);
            }
            for (size_t i = 0;
                 i < executor->context_depth && i < SHELL_ERROR_CONTEXT_MAX;
                 i++) {
                if (executor->context_stack[i]) {
                    shell_error_push_context(error, "%s",
                                             executor->context_stack[i]);
                }
            }
            shell_error_display(error, stderr, isatty(STDERR_FILENO));
            shell_error_free(error);
            close(fd);
            result = 1;
            break;
        }
        close(fd);
        break;
    }

    case NODE_REDIR_IN: {
        /// Input redirection: command < file (stdin only)
        int fd = open(target, O_RDONLY);
        if (fd == -1) {
            shell_error_t *error = shell_error_create(
                SHELL_ERR_FILE_NOT_FOUND, SHELL_SEVERITY_ERROR, redir_node->loc,
                "%s: %s", target, strerror(errno));
            char *src_line =
                executor_get_source_line(executor, redir_node->loc.line);
            if (src_line) {
                shell_error_set_source_line(
                    error, src_line, redir_node->loc.column,
                    redir_node->loc.column + redir_node->loc.length);
                free(src_line);
            }
            for (size_t i = 0;
                 i < executor->context_depth && i < SHELL_ERROR_CONTEXT_MAX;
                 i++) {
                if (executor->context_stack[i]) {
                    shell_error_push_context(error, "%s",
                                             executor->context_stack[i]);
                }
            }
            shell_error_display(error, stderr, isatty(STDERR_FILENO));
            shell_error_free(error);
            result = 1;
            break;
        }
        if (dup2(fd, STDIN_FILENO) == -1) {
            shell_error_t *error = shell_error_create(
                SHELL_ERR_BAD_FD, SHELL_SEVERITY_ERROR, redir_node->loc,
                "dup2: %s", strerror(errno));
            char *src_line =
                executor_get_source_line(executor, redir_node->loc.line);
            if (src_line) {
                shell_error_set_source_line(
                    error, src_line, redir_node->loc.column,
                    redir_node->loc.column + redir_node->loc.length);
                free(src_line);
            }
            for (size_t i = 0;
                 i < executor->context_depth && i < SHELL_ERROR_CONTEXT_MAX;
                 i++) {
                if (executor->context_stack[i]) {
                    shell_error_push_context(error, "%s",
                                             executor->context_stack[i]);
                }
            }
            shell_error_display(error, stderr, isatty(STDERR_FILENO));
            shell_error_free(error);
            close(fd);
            result = 1;
            break;
        }
        close(fd);
        break;
    }

    case NODE_REDIR_IN_FD: {
        /// Input redirection with explicit fd: N< file (e.g., 3< file)
        /// Extract fd number from redir_node->val.str (e.g., "3<")
        int dest_fd = STDIN_FILENO;
        if (redir_node->val.str && isdigit(redir_node->val.str[0])) {
            dest_fd = redir_node->val.str[0] - '0';
        }
        int fd = open(target, O_RDONLY);
        if (fd == -1) {
            shell_error_t *error = shell_error_create(
                SHELL_ERR_FILE_NOT_FOUND, SHELL_SEVERITY_ERROR, redir_node->loc,
                "%s: %s", target, strerror(errno));
            char *src_line =
                executor_get_source_line(executor, redir_node->loc.line);
            if (src_line) {
                shell_error_set_source_line(
                    error, src_line, redir_node->loc.column,
                    redir_node->loc.column + redir_node->loc.length);
                free(src_line);
            }
            for (size_t i = 0;
                 i < executor->context_depth && i < SHELL_ERROR_CONTEXT_MAX;
                 i++) {
                if (executor->context_stack[i]) {
                    shell_error_push_context(error, "%s",
                                             executor->context_stack[i]);
                }
            }
            shell_error_display(error, stderr, isatty(STDERR_FILENO));
            shell_error_free(error);
            result = 1;
            break;
        }
        if (dup2(fd, dest_fd) == -1) {
            shell_error_t *error = shell_error_create(
                SHELL_ERR_BAD_FD, SHELL_SEVERITY_ERROR, redir_node->loc,
                "dup2: %s", strerror(errno));
            char *src_line =
                executor_get_source_line(executor, redir_node->loc.line);
            if (src_line) {
                shell_error_set_source_line(
                    error, src_line, redir_node->loc.column,
                    redir_node->loc.column + redir_node->loc.length);
                free(src_line);
            }
            for (size_t i = 0;
                 i < executor->context_depth && i < SHELL_ERROR_CONTEXT_MAX;
                 i++) {
                if (executor->context_stack[i]) {
                    shell_error_push_context(error, "%s",
                                             executor->context_stack[i]);
                }
            }
            shell_error_display(error, stderr, isatty(STDERR_FILENO));
            shell_error_free(error);
            close(fd);
            result = 1;
            break;
        }
        close(fd);
        break;
    }

    case NODE_REDIR_ERR: {
        /// Output redirection with explicit fd: N> file (e.g., 2>, 3>)
        /// Extract fd number from redir_node->val.str (e.g., "2>", "3>")
        int dest_fd = STDERR_FILENO;
        if (redir_node->val.str && isdigit(redir_node->val.str[0])) {
            dest_fd = redir_node->val.str[0] - '0';
        }
        int fd = open(target, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd == -1) {
            shell_error_t *error = shell_error_create(
                SHELL_ERR_FILE_NOT_FOUND, SHELL_SEVERITY_ERROR, redir_node->loc,
                "%s: %s", target, strerror(errno));
            char *src_line =
                executor_get_source_line(executor, redir_node->loc.line);
            if (src_line) {
                shell_error_set_source_line(
                    error, src_line, redir_node->loc.column,
                    redir_node->loc.column + redir_node->loc.length);
                free(src_line);
            }
            for (size_t i = 0;
                 i < executor->context_depth && i < SHELL_ERROR_CONTEXT_MAX;
                 i++) {
                if (executor->context_stack[i]) {
                    shell_error_push_context(error, "%s",
                                             executor->context_stack[i]);
                }
            }
            shell_error_display(error, stderr, isatty(STDERR_FILENO));
            shell_error_free(error);
            result = 1;
            break;
        }
        if (dup2(fd, dest_fd) == -1) {
            shell_error_t *error = shell_error_create(
                SHELL_ERR_BAD_FD, SHELL_SEVERITY_ERROR, redir_node->loc,
                "dup2: %s", strerror(errno));
            char *src_line =
                executor_get_source_line(executor, redir_node->loc.line);
            if (src_line) {
                shell_error_set_source_line(
                    error, src_line, redir_node->loc.column,
                    redir_node->loc.column + redir_node->loc.length);
                free(src_line);
            }
            for (size_t i = 0;
                 i < executor->context_depth && i < SHELL_ERROR_CONTEXT_MAX;
                 i++) {
                if (executor->context_stack[i]) {
                    shell_error_push_context(error, "%s",
                                             executor->context_stack[i]);
                }
            }
            shell_error_display(error, stderr, isatty(STDERR_FILENO));
            shell_error_free(error);
            close(fd);
            result = 1;
            break;
        }
        close(fd);
        break;
    }

    case NODE_REDIR_ERR_APPEND: {
        /// Append redirection with explicit fd: N>> file (e.g., 2>>, 3>>)
        /// Extract fd number from redir_node->val.str (e.g., "2>>", "3>>")
        int dest_fd = STDERR_FILENO;
        if (redir_node->val.str && isdigit(redir_node->val.str[0])) {
            dest_fd = redir_node->val.str[0] - '0';
        }
        int fd = open(target, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fd == -1) {
            shell_error_t *error = shell_error_create(
                SHELL_ERR_FILE_NOT_FOUND, SHELL_SEVERITY_ERROR, redir_node->loc,
                "%s: %s", target, strerror(errno));
            char *src_line =
                executor_get_source_line(executor, redir_node->loc.line);
            if (src_line) {
                shell_error_set_source_line(
                    error, src_line, redir_node->loc.column,
                    redir_node->loc.column + redir_node->loc.length);
                free(src_line);
            }
            for (size_t i = 0;
                 i < executor->context_depth && i < SHELL_ERROR_CONTEXT_MAX;
                 i++) {
                if (executor->context_stack[i]) {
                    shell_error_push_context(error, "%s",
                                             executor->context_stack[i]);
                }
            }
            shell_error_display(error, stderr, isatty(STDERR_FILENO));
            shell_error_free(error);
            result = 1;
            break;
        }
        if (dup2(fd, dest_fd) == -1) {
            shell_error_t *error = shell_error_create(
                SHELL_ERR_BAD_FD, SHELL_SEVERITY_ERROR, redir_node->loc,
                "dup2: %s", strerror(errno));
            char *src_line =
                executor_get_source_line(executor, redir_node->loc.line);
            if (src_line) {
                shell_error_set_source_line(
                    error, src_line, redir_node->loc.column,
                    redir_node->loc.column + redir_node->loc.length);
                free(src_line);
            }
            for (size_t i = 0;
                 i < executor->context_depth && i < SHELL_ERROR_CONTEXT_MAX;
                 i++) {
                if (executor->context_stack[i]) {
                    shell_error_push_context(error, "%s",
                                             executor->context_stack[i]);
                }
            }
            shell_error_display(error, stderr, isatty(STDERR_FILENO));
            shell_error_free(error);
            close(fd);
            result = 1;
            break;
        }
        close(fd);
        break;
    }

    case NODE_REDIR_BOTH: {
        /// Combined stdout/stderr redirection: command &> file
        int fd = open(target, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd == -1) {
            shell_error_t *error = shell_error_create(
                SHELL_ERR_FILE_NOT_FOUND, SHELL_SEVERITY_ERROR, redir_node->loc,
                "%s: %s", target, strerror(errno));
            char *src_line =
                executor_get_source_line(executor, redir_node->loc.line);
            if (src_line) {
                shell_error_set_source_line(
                    error, src_line, redir_node->loc.column,
                    redir_node->loc.column + redir_node->loc.length);
                free(src_line);
            }
            for (size_t i = 0;
                 i < executor->context_depth && i < SHELL_ERROR_CONTEXT_MAX;
                 i++) {
                if (executor->context_stack[i]) {
                    shell_error_push_context(error, "%s",
                                             executor->context_stack[i]);
                }
            }
            shell_error_display(error, stderr, isatty(STDERR_FILENO));
            shell_error_free(error);
            result = 1;
            break;
        }
        if (dup2(fd, STDOUT_FILENO) == -1 || dup2(fd, STDERR_FILENO) == -1) {
            shell_error_t *error = shell_error_create(
                SHELL_ERR_BAD_FD, SHELL_SEVERITY_ERROR, redir_node->loc,
                "dup2: %s", strerror(errno));
            char *src_line =
                executor_get_source_line(executor, redir_node->loc.line);
            if (src_line) {
                shell_error_set_source_line(
                    error, src_line, redir_node->loc.column,
                    redir_node->loc.column + redir_node->loc.length);
                free(src_line);
            }
            for (size_t i = 0;
                 i < executor->context_depth && i < SHELL_ERROR_CONTEXT_MAX;
                 i++) {
                if (executor->context_stack[i]) {
                    shell_error_push_context(error, "%s",
                                             executor->context_stack[i]);
                }
            }
            shell_error_display(error, stderr, isatty(STDERR_FILENO));
            shell_error_free(error);
            close(fd);
            result = 1;
            break;
        }
        close(fd);
        break;
    }

    case NODE_REDIR_BOTH_APPEND: {
        /// Append both stdout/stderr: command &>> file
        int fd = open(target, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fd == -1) {
            shell_error_t *error = shell_error_create(
                SHELL_ERR_FILE_NOT_FOUND, SHELL_SEVERITY_ERROR, redir_node->loc,
                "%s: %s", target, strerror(errno));
            char *src_line =
                executor_get_source_line(executor, redir_node->loc.line);
            if (src_line) {
                shell_error_set_source_line(
                    error, src_line, redir_node->loc.column,
                    redir_node->loc.column + redir_node->loc.length);
                free(src_line);
            }
            for (size_t i = 0;
                 i < executor->context_depth && i < SHELL_ERROR_CONTEXT_MAX;
                 i++) {
                if (executor->context_stack[i]) {
                    shell_error_push_context(error, "%s",
                                             executor->context_stack[i]);
                }
            }
            shell_error_display(error, stderr, isatty(STDERR_FILENO));
            shell_error_free(error);
            result = 1;
            break;
        }
        if (dup2(fd, STDOUT_FILENO) == -1 || dup2(fd, STDERR_FILENO) == -1) {
            shell_error_t *error = shell_error_create(
                SHELL_ERR_BAD_FD, SHELL_SEVERITY_ERROR, redir_node->loc,
                "dup2: %s", strerror(errno));
            char *src_line =
                executor_get_source_line(executor, redir_node->loc.line);
            if (src_line) {
                shell_error_set_source_line(
                    error, src_line, redir_node->loc.column,
                    redir_node->loc.column + redir_node->loc.length);
                free(src_line);
            }
            for (size_t i = 0;
                 i < executor->context_depth && i < SHELL_ERROR_CONTEXT_MAX;
                 i++) {
                if (executor->context_stack[i]) {
                    shell_error_push_context(error, "%s",
                                             executor->context_stack[i]);
                }
            }
            shell_error_display(error, stderr, isatty(STDERR_FILENO));
            shell_error_free(error);
            close(fd);
            result = 1;
            break;
        }
        close(fd);
        break;
    }

    case NODE_REDIR_HEREDOC: {
        /// Here document: command << DELIMITER
        result = setup_here_document(target, false);
        break;
    }

    case NODE_REDIR_HEREDOC_STRIP: {
        /// Here document with tab stripping: command <<- DELIMITER
        result = setup_here_document(target, true);
        break;
    }

    case NODE_REDIR_HERESTRING: {
        /// Here string: command <<< string
        result = setup_here_string(executor, target);
        break;
    }

    case NODE_REDIR_CLOBBER: {
        /// Force output redirection: command >| file
        /// Override noclobber setting - always allow overwriting
        int fd = open(target, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd == -1) {
            shell_error_t *error = shell_error_create(
                SHELL_ERR_FILE_NOT_FOUND, SHELL_SEVERITY_ERROR, redir_node->loc,
                "%s: %s", target, strerror(errno));
            char *src_line =
                executor_get_source_line(executor, redir_node->loc.line);
            if (src_line) {
                shell_error_set_source_line(
                    error, src_line, redir_node->loc.column,
                    redir_node->loc.column + redir_node->loc.length);
                free(src_line);
            }
            for (size_t i = 0;
                 i < executor->context_depth && i < SHELL_ERROR_CONTEXT_MAX;
                 i++) {
                if (executor->context_stack[i]) {
                    shell_error_push_context(error, "%s",
                                             executor->context_stack[i]);
                }
            }
            shell_error_display(error, stderr, isatty(STDERR_FILENO));
            shell_error_free(error);
            result = 1;
            break;
        }
        if (dup2(fd, STDOUT_FILENO) == -1) {
            shell_error_t *error = shell_error_create(
                SHELL_ERR_BAD_FD, SHELL_SEVERITY_ERROR, redir_node->loc,
                "dup2: %s", strerror(errno));
            char *src_line =
                executor_get_source_line(executor, redir_node->loc.line);
            if (src_line) {
                shell_error_set_source_line(
                    error, src_line, redir_node->loc.column,
                    redir_node->loc.column + redir_node->loc.length);
                free(src_line);
            }
            for (size_t i = 0;
                 i < executor->context_depth && i < SHELL_ERROR_CONTEXT_MAX;
                 i++) {
                if (executor->context_stack[i]) {
                    shell_error_push_context(error, "%s",
                                             executor->context_stack[i]);
                }
            }
            shell_error_display(error, stderr, isatty(STDERR_FILENO));
            shell_error_free(error);
            result = 1;
        }
        close(fd);
        break;
    }

    default:
        /// Unknown redirection type
        result = 1;
        break;
    }

    free(target);
    return result;
}

/**
 * @brief Setup here document redirection
 *
 * Creates a pipe and forks to read here document content interactively.
 * Redirects stdin to read from the here document content.
 *
 * @param delimiter Delimiter string that ends the here document
 * @param strip_tabs If true, strip leading tabs from each line (<<-)
 * @return 0 on success, non-zero on error
 */
static int setup_here_document(const char *delimiter, bool strip_tabs) {
    /// Create a temporary pipe for the here document content
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        shell_error_t *error =
            shell_error_create(SHELL_ERR_PIPE_FAILED, SHELL_SEVERITY_ERROR,
                               SOURCE_LOC_UNKNOWN, "pipe: %s", strerror(errno));
        shell_error_display(error, stderr, isatty(STDERR_FILENO));
        shell_error_free(error);
        return 1;
    }

    pid_t pid = lush_fork();
    if (pid == -1) {
        shell_error_t *error =
            shell_error_create(SHELL_ERR_FORK_FAILED, SHELL_SEVERITY_ERROR,
                               SOURCE_LOC_UNKNOWN, "fork: %s", strerror(errno));
        shell_error_display(error, stderr, isatty(STDERR_FILENO));
        shell_error_free(error);
        close(pipefd[0]);
        close(pipefd[1]);
        return 1;
    }

    if (pid == 0) {
        /// Child process: read here document lines and write to pipe.
        /// Reset inherited interactive handlers so this subshell is hung up or
        /// faulted like any process rather than running shell signal logic.
        reset_subshell_signals();
        close(pipefd[0]); /// Close read end

        char *line = NULL;
        size_t line_cap = 0;
        ssize_t line_len;

        /// Read lines until we find the delimiter
        while ((line_len = getline(&line, &line_cap, stdin)) != -1) {
            /// Remove trailing newline for comparison
            if (line_len > 0 && line[line_len - 1] == '\n') {
                line[line_len - 1] = '\0';
                line_len--;
            }

            /// Strip leading tabs if requested (<<- variant)
            char *content = line;
            if (strip_tabs) {
                while (*content == '\t') {
                    content++;
                }
            }

            /// Check if this line matches the delimiter.
            /// Compare under NFC equivalence so a heredoc terminated
            /// with a non-ASCII delimiter (e.g. EOFé) matches whether
            /// the on-script delimiter and the on-stdin terminator
            /// line are encoded as NFC, NFD, or one of each. The
            /// underlying primitive's ASCII fast path means the
            /// common EOF / END / EOT cases pay no extra cost.
            if (lle_unicode_strings_equal(content, delimiter, NULL)) {
                break; /// End of here document
            }

            /// Write the original line (with newline) to the pipe.
            /// Short or interrupted writes would deliver truncated heredoc
            /// input to the child; redir_write_all retries until complete.
            if (!redir_write_all(pipefd[1], line, strlen(line)) ||
                !redir_write_all(pipefd[1], "\n", 1)) {
                free(line);
                close(pipefd[1]);
                free_global_symtable();
                _exit(1);
            }
        }

        if (line) {
            free(line);
        }
        close(pipefd[1]);
        free_global_symtable();
        _exit(0);
    } else {
        /// Parent process: redirect stdin to read from pipe
        close(pipefd[1]); /// Close write end

        if (dup2(pipefd[0], STDIN_FILENO) == -1) {
            shell_error_t *error = shell_error_create(
                SHELL_ERR_BAD_FD, SHELL_SEVERITY_ERROR, SOURCE_LOC_UNKNOWN,
                "dup2: %s", strerror(errno));
            shell_error_display(error, stderr, isatty(STDERR_FILENO));
            shell_error_free(error);
            close(pipefd[0]);
            return 1;
        }
        close(pipefd[0]);

        /// Wait for child to finish writing the here document
        int status;
        waitpid(pid, &status, 0);
    }

    return 0;
}

/**
 * @brief Setup here document redirection with pre-collected content
 *
 * Creates a pipe and writes pre-collected content to it, then
 * redirects stdin to read from the pipe.
 *
 * @param content Pre-collected here document content
 * @return 0 on success, non-zero on error
 */
static int setup_here_document_with_content(const char *content) {
    if (!content) {
        return 1;
    }

    /// Create a temporary pipe for the here document content
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        shell_error_t *error =
            shell_error_create(SHELL_ERR_PIPE_FAILED, SHELL_SEVERITY_ERROR,
                               SOURCE_LOC_UNKNOWN, "pipe: %s", strerror(errno));
        shell_error_display(error, stderr, isatty(STDERR_FILENO));
        shell_error_free(error);
        return 1;
    }

    pid_t pid = lush_fork();
    if (pid == -1) {
        shell_error_t *error =
            shell_error_create(SHELL_ERR_FORK_FAILED, SHELL_SEVERITY_ERROR,
                               SOURCE_LOC_UNKNOWN, "fork: %s", strerror(errno));
        shell_error_display(error, stderr, isatty(STDERR_FILENO));
        shell_error_free(error);
        close(pipefd[0]);
        close(pipefd[1]);
        return 1;
    }

    if (pid == 0) {
        /// Child process: write pre-collected content to pipe.
        /// Reset inherited interactive handlers so this subshell is hung up or
        /// faulted like any process rather than running shell signal logic.
        reset_subshell_signals();
        close(pipefd[0]); /// Close read end

        /// Write the content to the pipe
        size_t content_len = strlen(content);
        if (content_len > 0) {
            ssize_t written = write(pipefd[1], content, content_len);
            if (written == -1) {
                /// In child process, use fprintf since shell_error requires
                /// allocation which may fail; also child exits immediately
                fprintf(stderr, "write: %s\n", strerror(errno));
            }
        }

        close(pipefd[1]);
        free_global_symtable();
        _exit(0);
    } else {
        /// Parent process: redirect stdin to read from pipe
        close(pipefd[1]); /// Close write end

        if (dup2(pipefd[0], STDIN_FILENO) == -1) {
            shell_error_t *error = shell_error_create(
                SHELL_ERR_BAD_FD, SHELL_SEVERITY_ERROR, SOURCE_LOC_UNKNOWN,
                "dup2: %s", strerror(errno));
            shell_error_display(error, stderr, isatty(STDERR_FILENO));
            shell_error_free(error);
            close(pipefd[0]);
            return 1;
        }
        close(pipefd[0]);

        /// Wait for child to finish writing the here document
        int status;
        waitpid(pid, &status, 0);
    }

    return 0;
}

/**
 * @brief Setup here document with variable expansion and tab stripping
 *
 * Processes here document content with optional tab stripping and
 * variable expansion, then sets up stdin redirection.
 *
 * @param executor Executor context for variable expansion
 * @param content Raw here document content
 * @param strip_tabs If true, strip leading tabs from each line
 * @param expand_vars If true, expand variables in content
 * @return 0 on success, non-zero on error
 */
static int setup_here_document_with_processing(executor_t *executor,
                                               const char *content,
                                               bool strip_tabs,
                                               bool expand_vars) {
    if (!content) {
        return 1;
    }

    /// Process the content line by line
    size_t processed_size = strlen(content) * 2 + 1; /// Allow for expansion
    char *processed_content = malloc(processed_size);
    if (!processed_content) {
        return 1;
    }
    processed_content[0] = '\0';

    char *line_start = (char *)content;
    char *line_end;

    while (*line_start) {
        /// Find end of current line
        line_end = strchr(line_start, '\n');
        if (!line_end) {
            line_end = line_start + strlen(line_start);
        }

        /// Extract line
        size_t line_len = line_end - line_start;
        char *line = malloc(line_len + 1);
        if (!line) {
            free(processed_content);
            return 1;
        }
        strncpy(line, line_start, line_len);
        line[line_len] = '\0';

        /// Strip leading tabs if requested
        char *processed_line = line;
        if (strip_tabs) {
            while (*processed_line == '\t') {
                processed_line++;
            }
        }

        /// Variable expansion if requested and executor available
        char *final_line = processed_line;
        if (expand_vars && executor) {
            /// Use the executor's variable expansion function
            char *expanded_line = expand_if_needed(executor, processed_line);
            if (expanded_line) {
                final_line = expanded_line;
            }
        }

        /// Ensure buffer is large enough
        size_t needed = strlen(processed_content) + strlen(final_line) + 2;
        if (needed > processed_size) {
            processed_size = needed * 2;
            char *new_content = realloc(processed_content, processed_size);
            if (!new_content) {
                free(line);
                if (final_line != processed_line) {
                    free(final_line);
                }
                free(processed_content);
                return 1;
            }
            processed_content = new_content;
        }

        /// Add line to processed content
        strcat(processed_content, final_line);
        if (*line_end == '\n') {
            strcat(processed_content, "\n");
        }

        /// Clean up memory
        if (final_line != processed_line) {
            free(final_line);
        }
        free(line);

        /// Move to next line
        if (*line_end == '\n') {
            line_start = line_end + 1;
        } else {
            break;
        }
    }

    /// Now use the regular setup function with processed content
    int result = setup_here_document_with_content(processed_content);
    free(processed_content);
    return result;
}

/**
 * @brief Setup here string redirection
 *
 * Creates a pipe containing the here string content with a trailing
 * newline, then redirects stdin to read from it.
 *
 * @param executor Executor context for variable expansion
 * @param content Here string content
 * @return 0 on success, non-zero on error
 */
static int setup_here_string(executor_t *executor, const char *content) {

    if (getenv("LUSH_DEBUG_REDIR")) {
        printf("DEBUG: setup_here_string called with: '%s'\n", content);
    }

    int pipefd[2];
    if (pipe(pipefd) == -1) {
        shell_error_t *error =
            shell_error_create(SHELL_ERR_PIPE_FAILED, SHELL_SEVERITY_ERROR,
                               SOURCE_LOC_UNKNOWN, "pipe: %s", strerror(errno));
        shell_error_display(error, stderr, isatty(STDERR_FILENO));
        shell_error_free(error);
        return 1;
    }

    /// IMPORTANT: do NOT re-expand here. handle_redirection_node has
    /// already run expand_arg_node on the value that the
    /// parser produced; calling it again applied escape processing
    /// twice, which (combined with expand_if_needed's lossy
    /// behavior on single-quoted content) shredded backslashes in
    /// here-string input. The bytes we received are the bytes that
    /// belong on the pipe.

    /// Write the content + trailing newline to the pipe.
    /// redir_write_all loops on EINTR / short writes so a partial
    /// delivery cannot truncate the here-string the child reads.
    if (!redir_write_all(pipefd[1], content, strlen(content)) ||
        !redir_write_all(pipefd[1], "\n", 1)) {
        shell_error_t *error = shell_error_create(
            SHELL_ERR_IO_ERROR, SHELL_SEVERITY_ERROR, SOURCE_LOC_UNKNOWN,
            "write: %s", strerror(errno));
        shell_error_display(error, stderr, isatty(STDERR_FILENO));
        shell_error_free(error);
        close(pipefd[0]);
        close(pipefd[1]);
        return 1;
    }

    close(pipefd[1]);
    (void)executor; /// no longer needed -- content arrives pre-expanded

    /// Redirect stdin to read from the pipe
    if (dup2(pipefd[0], STDIN_FILENO) == -1) {
        shell_error_t *error =
            shell_error_create(SHELL_ERR_BAD_FD, SHELL_SEVERITY_ERROR,
                               SOURCE_LOC_UNKNOWN, "dup2: %s", strerror(errno));
        shell_error_display(error, stderr, isatty(STDERR_FILENO));
        shell_error_free(error);
        close(pipefd[0]);
        return 1;
    }
    close(pipefd[0]);

    return 0;
}

/**
 * @brief Setup file descriptor redirection
 *
 * Handles file descriptor duplication patterns including:
 * - >&N, <&N - redirect stdout/stdin to fd N
 * - N>&M, N<&M - redirect fd N to fd M
 * - >&-, <&- - close stdout/stdin
 * - N>&-, N<&- - close fd N
 * - >&$VAR, <&${VAR} - variable expansion for fd target
 *
 * @param executor Executor context for variable expansion
 * @param redir_text Redirection text (e.g., ">&2", "2>&1", ">&$FD")
 * @return 0 on success, non-zero on error
 */
static int setup_fd_redirection(executor_t *executor, const char *redir_text) {
    if (!redir_text) {
        return 1;
    }

    const char *p = redir_text;
    int source_fd = -1;

    /// Check for leading digit (N>&M or N<&M pattern)
    if (isdigit(*p)) {
        source_fd = *p - '0';
        p++;
    }

    /// Determine direction: < or >
    if (*p == '<') {
        if (source_fd == -1)
            source_fd = STDIN_FILENO;
        p++;
    } else if (*p == '>') {
        if (source_fd == -1)
            source_fd = STDOUT_FILENO;
        p++;
    } else {
        return 1; /// Unknown pattern
    }

    /// Must have &
    if (*p != '&') {
        return 1;
    }
    p++;

    /// Get the target fd - may be digit, -, or variable
    int target_fd = -1;
    bool close_fd = false;

    if (*p == '-') {
        /// Close the file descriptor
        close_fd = true;
    } else if (isdigit(*p)) {
        /// Literal digit
        target_fd = *p - '0';
    } else if (*p == '$') {
        /// Variable expansion needed. This target is a raw `$name`/`${...}`
        /// fd reference (not an AST node), so it expands at the string layer;
        /// a NULL/empty result is reported as a bad fd just below.
        char *expanded = expand_if_needed(executor, p);
        if (!expanded || *expanded == '\0') {
            shell_error_t *error = shell_error_create(
                SHELL_ERR_BAD_FD, SHELL_SEVERITY_ERROR, SOURCE_LOC_UNKNOWN,
                "%s: bad file descriptor", p);
            shell_error_display(error, stderr, isatty(STDERR_FILENO));
            shell_error_free(error);
            free(expanded);
            return 1;
        }
        /// Parse the expanded value
        char *endptr;
        long fd_val = strtol(expanded, &endptr, 10);
        if (*endptr != '\0' || fd_val < 0 || fd_val > 255) {
            shell_error_t *error = shell_error_create(
                SHELL_ERR_BAD_FD, SHELL_SEVERITY_ERROR, SOURCE_LOC_UNKNOWN,
                "%s: bad file descriptor", expanded);
            shell_error_display(error, stderr, isatty(STDERR_FILENO));
            shell_error_free(error);
            free(expanded);
            return 1;
        }
        target_fd = (int)fd_val;
        free(expanded);
    } else {
        return 1; /// Unknown pattern
    }

    /// Perform the redirection
    if (close_fd) {
        if (close(source_fd) == -1) {
            /// Ignore EBADF - fd wasn't open, which is fine for close
            if (errno != EBADF) {
                shell_error_t *error = shell_error_create(
                    SHELL_ERR_BAD_FD, SHELL_SEVERITY_ERROR, SOURCE_LOC_UNKNOWN,
                    "%d: %s", source_fd, strerror(errno));
                shell_error_display(error, stderr, isatty(STDERR_FILENO));
                shell_error_free(error);
                return 1;
            }
        }
        return 0;
    }

    /// Validate that target_fd is open and suitable for the operation
    /// Use fcntl to check if the fd is open and has appropriate access mode
    int fd_flags = fcntl(target_fd, F_GETFL);
    if (fd_flags == -1) {
        /// fd is not open
        shell_error_t *error = shell_error_create(
            SHELL_ERR_BAD_FD, SHELL_SEVERITY_ERROR, SOURCE_LOC_UNKNOWN,
            "%d: Bad file descriptor", target_fd);
        shell_error_display(error, stderr, isatty(STDERR_FILENO));
        shell_error_free(error);
        return 1;
    }

    /// For output redirections (source_fd is stdout-like), verify target is
    /// writable For input redirections (source_fd is stdin-like), verify target
    /// is readable
    int access_mode = fd_flags & O_ACCMODE;
    bool is_output_redir = (source_fd != STDIN_FILENO);
    bool is_input_redir = (source_fd == STDIN_FILENO);

    if (is_output_redir && access_mode == O_RDONLY) {
        /// Trying to redirect output to a read-only fd
        shell_error_t *error = shell_error_create(
            SHELL_ERR_BAD_FD, SHELL_SEVERITY_ERROR, SOURCE_LOC_UNKNOWN,
            "%d: Bad file descriptor", target_fd);
        shell_error_display(error, stderr, isatty(STDERR_FILENO));
        shell_error_free(error);
        return 1;
    }

    if (is_input_redir && access_mode == O_WRONLY) {
        /// Trying to redirect input from a write-only fd
        shell_error_t *error = shell_error_create(
            SHELL_ERR_BAD_FD, SHELL_SEVERITY_ERROR, SOURCE_LOC_UNKNOWN,
            "%d: Bad file descriptor", target_fd);
        shell_error_display(error, stderr, isatty(STDERR_FILENO));
        shell_error_free(error);
        return 1;
    }

    if (dup2(target_fd, source_fd) == -1) {
        shell_error_t *error = shell_error_create(
            SHELL_ERR_BAD_FD, SHELL_SEVERITY_ERROR, SOURCE_LOC_UNKNOWN,
            "%d: %s", target_fd, strerror(errno));
        shell_error_display(error, stderr, isatty(STDERR_FILENO));
        shell_error_free(error);
        return 1;
    }
    return 0;
}

/**
 * @brief Save current file descriptors for later restoration
 *
 * Duplicates stdin, stdout, and stderr so they can be restored
 * after redirection operations complete.
 *
 * @param state State structure to store saved descriptors
 * @return 0 on success, non-zero on error
 */
int save_file_descriptors(redirection_state_t *state) {
    if (!state) {
        return 1;
    }

    /// Flush stdio buffers BEFORE installing the new file descriptors so any
    /// pending buffered content goes to its original destination rather than
    /// the redirection target. dup2 only changes the kernel-level fd; libc
    /// stdio still holds bytes destined for fd 1/2, and a later flush (or
    /// automatic flush triggered by a builtin or buffer fill) would write
    /// those bytes through the new fd, silently contaminating the redirected
    /// file. The symmetric flush in restore_file_descriptors handles the
    /// other direction (output produced while redirected must reach the
    /// redirected fd before it is closed).
    fflush(stdout);
    fflush(stderr);

    /// Initialize state
    state->stdin_saved = false;
    state->stdout_saved = false;
    state->stderr_saved = false;

    /// Save stdin
    state->saved_stdin = dup(STDIN_FILENO);
    if (state->saved_stdin != -1) {
        state->stdin_saved = true;
    }

    /// Save stdout
    state->saved_stdout = dup(STDOUT_FILENO);
    if (state->saved_stdout != -1) {
        state->stdout_saved = true;
    }

    /// Save stderr
    state->saved_stderr = dup(STDERR_FILENO);
    if (state->saved_stderr != -1) {
        state->stderr_saved = true;
    }

    return 0;
}

/**
 * @brief Restore file descriptors after command execution
 *
 * Restores stdin, stdout, and stderr from previously saved state
 * and closes the saved duplicates.
 *
 * @param state State structure containing saved descriptors
 * @return 0 on success, non-zero on error
 */
int restore_file_descriptors(redirection_state_t *state) {
    if (!state) {
        return 1;
    }

    int result = 0;

    /// Flush stdio buffers before restoring file descriptors
    /// This is critical: when stdout/stderr are redirected to files,
    /// unflushed data in the stdio buffers would be lost when we
    /// restore the original file descriptors (which closes the redirected ones)
    if (state->stdout_saved) {
        fflush(stdout);
    }
    if (state->stderr_saved) {
        fflush(stderr);
    }

    /// Restore stdin
    if (state->stdin_saved) {
        if (dup2(state->saved_stdin, STDIN_FILENO) == -1) {
            shell_error_t *error = shell_error_create(
                SHELL_ERR_BAD_FD, SHELL_SEVERITY_ERROR, SOURCE_LOC_UNKNOWN,
                "failed to restore stdin: %s", strerror(errno));
            shell_error_display(error, stderr, isatty(STDERR_FILENO));
            shell_error_free(error);
            result = 1;
        }
        close(state->saved_stdin);
    }

    /// Restore stdout
    if (state->stdout_saved) {
        if (dup2(state->saved_stdout, STDOUT_FILENO) == -1) {
            shell_error_t *error = shell_error_create(
                SHELL_ERR_BAD_FD, SHELL_SEVERITY_ERROR, SOURCE_LOC_UNKNOWN,
                "failed to restore stdout: %s", strerror(errno));
            shell_error_display(error, stderr, isatty(STDERR_FILENO));
            shell_error_free(error);
            result = 1;
        }
        close(state->saved_stdout);
    }

    /// Restore stderr
    if (state->stderr_saved) {
        if (dup2(state->saved_stderr, STDERR_FILENO) == -1) {
            shell_error_t *error = shell_error_create(
                SHELL_ERR_BAD_FD, SHELL_SEVERITY_ERROR, SOURCE_LOC_UNKNOWN,
                "failed to restore stderr: %s", strerror(errno));
            shell_error_display(error, stderr, isatty(STDERR_FILENO));
            shell_error_free(error);
            result = 1;
        }
        close(state->saved_stderr);
    }

    return result;
}

/**
 * @brief Find next available file descriptor
 *
 * Scans for the first unused fd >= min_fd. Used for {varname} fd allocation
 * syntax (bash 4.1+/zsh feature).
 *
 * @param min_fd Minimum fd to check (typically 10)
 * @return Available fd number, or -1 if none available
 */
static int find_available_fd(int min_fd) {
    struct stat st;
    for (int fd = min_fd; fd < 255; fd++) {
        /// Check if this fd is available by trying to fstat it
        /// If fstat returns -1 with EBADF, the fd is not open
        if (fstat(fd, &st) == -1 && errno == EBADF) {
            return fd;
        }
    }
    return -1; /// No available fd found
}

/**
 * @brief Setup fd allocation redirection
 *
 * Handles {varname} fd allocation syntax (bash 4.1+/zsh feature):
 * - {varname}>/file - allocate fd, open file for writing, store fd in varname
 * - {varname}>>/file - allocate fd, open file for appending
 * - {varname}</file - allocate fd, open file for reading
 * - {varname}>&- - close fd stored in $varname (lookup value first)
 * - {varname}>&N - allocate fd, dup to fd N
 *
 * @param executor Executor context for variable access
 * @param redir_node Redirection node containing pattern and optional target
 * @return 0 on success, non-zero on error
 */
static int setup_fd_alloc_redirection(executor_t *executor,
                                      node_t *redir_node) {
    if (!redir_node || !redir_node->val.str) {
        return 1;
    }

    const char *redir_text = redir_node->val.str;

    /// Parse {varname} - extract variable name between braces
    if (*redir_text != '{') {
        return 1;
    }

    const char *var_start = redir_text + 1;
    const char *var_end = strchr(var_start, '}');
    if (!var_end) {
        return 1;
    }

    size_t var_len = var_end - var_start;
    char *var_name = malloc(var_len + 1);
    if (!var_name) {
        return 1;
    }
    strncpy(var_name, var_start, var_len);
    var_name[var_len] = '\0';

    /// Get redirection operator after }
    const char *op = var_end + 1;

    /// Determine operation type
    bool is_input = (*op == '<');
    bool is_close = false;
    bool is_dup = false;
    bool is_append = false;
    int dup_target = -1;

    op++; /// Skip < or >

    /// Check for >> (append)
    if (*op == '>') {
        is_append = true;
        op++;
    }
    /// Check for >& or <& (dup or close)
    else if (*op == '&') {
        op++;
        if (*op == '-') {
            is_close = true;
        } else if (isdigit(*op)) {
            is_dup = true;
            dup_target = *op - '0';
        }
    }

    int result = 0;

    if (is_close) {
        /// {varname}>&- : close fd stored in $varname
        /// First look up the variable value
        char *fd_str = symtable_get_var(executor->symtable, var_name);
        if (!fd_str || *fd_str == '\0') {
            shell_error_t *error = shell_error_create(
                SHELL_ERR_BAD_FD, SHELL_SEVERITY_ERROR, SOURCE_LOC_UNKNOWN,
                "%s: ambiguous redirect", var_name);
            shell_error_display(error, stderr, isatty(STDERR_FILENO));
            shell_error_free(error);
            free(fd_str);
            free(var_name);
            return 1;
        }
        char *endptr;
        long fd_val = strtol(fd_str, &endptr, 10);
        if (*endptr != '\0' || fd_val < 0 || fd_val > 255) {
            shell_error_t *error = shell_error_create(
                SHELL_ERR_BAD_FD, SHELL_SEVERITY_ERROR, SOURCE_LOC_UNKNOWN,
                "%s: bad file descriptor", fd_str);
            shell_error_display(error, stderr, isatty(STDERR_FILENO));
            shell_error_free(error);
            free(fd_str);
            free(var_name);
            return 1;
        }
        free(fd_str);
        if (close((int)fd_val) == -1 && errno != EBADF) {
            shell_error_t *error = shell_error_create(
                SHELL_ERR_BAD_FD, SHELL_SEVERITY_ERROR, SOURCE_LOC_UNKNOWN,
                "%ld: %s", fd_val, strerror(errno));
            shell_error_display(error, stderr, isatty(STDERR_FILENO));
            shell_error_free(error);
            free(var_name);
            return 1;
        }
        /// Drop from the variable-alloc registry so executor_free does not
        /// re-close (or double-close into a recycled fd number).
        executor_untrack_alloc_fd(executor, (int)fd_val);
        free(var_name);
        return 0;
    }

    if (is_dup) {
        /// {varname}>&N : allocate fd and dup to N
        int allocated_fd = find_available_fd(10);
        if (allocated_fd < 0) {
            shell_error_t *error = shell_error_create(
                SHELL_ERR_FD_UNAVAILABLE, SHELL_SEVERITY_ERROR,
                SOURCE_LOC_UNKNOWN, "cannot allocate file descriptor for %s",
                var_name);
            shell_error_display(error, stderr, isatty(STDERR_FILENO));
            shell_error_free(error);
            free(var_name);
            return 1;
        }
        if (dup2(dup_target, allocated_fd) == -1) {
            shell_error_t *error = shell_error_create(
                SHELL_ERR_BAD_FD, SHELL_SEVERITY_ERROR, SOURCE_LOC_UNKNOWN,
                "%d: %s", dup_target, strerror(errno));
            shell_error_display(error, stderr, isatty(STDERR_FILENO));
            shell_error_free(error);
            free(var_name);
            return 1;
        }
        executor_track_alloc_fd(executor, allocated_fd);
        /// Store allocated fd in variable
        char fd_str[16];
        snprintf(fd_str, sizeof(fd_str), "%d", allocated_fd);
        symtable_set_var(executor->symtable, var_name, fd_str, SYMVAR_NONE);
        free(var_name);
        return 0;
    }

    /// {varname}>/file or {varname}</file : allocate fd and open file
    /// Get target filename from first child
    node_t *target_node = redir_node->first_child;
    if (!target_node) {
        shell_error_t *error = shell_error_create(
            SHELL_ERR_INVALID_REDIRECT, SHELL_SEVERITY_ERROR,
            SOURCE_LOC_UNKNOWN, "missing redirection target");
        shell_error_display(error, stderr, isatty(STDERR_FILENO));
        shell_error_free(error);
        free(var_name);
        return 1;
    }

    /// Process substitution as target: `exec {fd}< <(cmd)` or
    /// `exec {fd}> >(cmd)`. Expand into a /dev/fd/N path and open that.
    char *target = NULL;
    if (target_node->type == NODE_PROC_SUB_IN ||
        target_node->type == NODE_PROC_SUB_OUT) {
        target = expand_process_substitution(executor, target_node);
    } else if (target_node->val.str) {
        /// Same shared per-node-type word expander the regular redirection
        /// path uses, so `exec {fd}> $'ansi'` / `> $(cmd)` / a single-quoted
        /// verbatim target all behave identically to `> target`.
        target = expand_arg_node(executor, target_node);
    }
    if (!target) {
        shell_error_t *error = shell_error_create(
            SHELL_ERR_INVALID_REDIRECT, SHELL_SEVERITY_ERROR,
            SOURCE_LOC_UNKNOWN, "missing redirection target");
        shell_error_display(error, stderr, isatty(STDERR_FILENO));
        shell_error_free(error);
        free(var_name);
        return 1;
    }

    /// Allocate fd
    int allocated_fd = find_available_fd(10);
    if (allocated_fd < 0) {
        shell_error_t *error = shell_error_create(
            SHELL_ERR_FD_UNAVAILABLE, SHELL_SEVERITY_ERROR, SOURCE_LOC_UNKNOWN,
            "cannot allocate file descriptor for %s", var_name);
        shell_error_display(error, stderr, isatty(STDERR_FILENO));
        shell_error_free(error);
        free(var_name);
        free(target);
        return 1;
    }

    /// Open file with appropriate flags
    int flags;
    if (is_input) {
        flags = O_RDONLY;
    } else if (is_append) {
        flags = O_WRONLY | O_CREAT | O_APPEND;
    } else {
        flags = O_WRONLY | O_CREAT | O_TRUNC;
    }

    int fd = open(target, flags, 0644);
    if (fd == -1) {
        shell_error_t *error = shell_error_create(
            SHELL_ERR_FILE_NOT_FOUND, SHELL_SEVERITY_ERROR, SOURCE_LOC_UNKNOWN,
            "%s: %s", target, strerror(errno));
        shell_error_display(error, stderr, isatty(STDERR_FILENO));
        shell_error_free(error);
        free(var_name);
        free(target);
        return 1;
    }

    /// Move fd to allocated slot if necessary
    if (fd != allocated_fd) {
        if (dup2(fd, allocated_fd) == -1) {
            shell_error_t *error = shell_error_create(
                SHELL_ERR_BAD_FD, SHELL_SEVERITY_ERROR, SOURCE_LOC_UNKNOWN,
                "dup2: %s", strerror(errno));
            shell_error_display(error, stderr, isatty(STDERR_FILENO));
            shell_error_free(error);
            close(fd);
            free(var_name);
            free(target);
            return 1;
        }
        close(fd);
    }

    executor_track_alloc_fd(executor, allocated_fd);

    /// Store allocated fd in variable
    char fd_str[16];
    snprintf(fd_str, sizeof(fd_str), "%d", allocated_fd);
    symtable_set_var(executor->symtable, var_name, fd_str, SYMVAR_NONE);

    free(var_name);
    free(target);
    return result;
}

/**
 * @brief Print redirection error message
 *
 * @param message Error message to print
 */
void redirection_error(const char *message) {
    if (message) {
        fprintf(stderr, "redirection: %s\n", message);
    }
}

/**
 * @brief Check if a node represents a redirection
 *
 * @param node Node to check
 * @return true if node is a redirection type, false otherwise
 */
bool is_redirection_node(node_t *node) {
    if (!node) {
        return false;
    }

    return (node->type >= NODE_REDIR_IN && node->type <= NODE_REDIR_CLOBBER);
}

/**
 * @brief Count redirection nodes in a command
 *
 * @param command Command node to examine
 * @return Number of redirection children
 */
int count_redirections(node_t *command) {
    if (!command) {
        return 0;
    }

    int count = 0;
    node_t *child = command->first_child;
    while (child) {

        if (is_redirection_node(child)) {
            count++;
        }
        child = child->next_sibling;
    }

    return count;
}
