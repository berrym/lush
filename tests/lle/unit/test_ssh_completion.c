/*
 * Lush Shell - SSH Host Completion Tests
 * Copyright (C) 2021-2026 Michael Berry
 *
 * Licensed under the MIT License. See LICENSE file for details.
 *
 * Behaviour tests for the SSH host completion source. The source is
 * exercised through its public entry point lle_completion_source_ssh_hosts
 * with a real ssh_hosts cache populated from a fixture ~/.ssh/config in
 * a temporary HOME, mirroring how it is invoked at runtime by the
 * source manager. Coverage:
 *
 *   - Bare prefix matches a configured Host stanza and emits the host.
 *     Catches regressions where ssh source is unregistered or where
 *     hostname prefix matching breaks.
 *
 *   - "user@prefix" preserves the user-typed user segment and matches
 *     the post-@ portion against hostnames. Catches regressions in the
 *     user@ split logic added when the source was wired up.
 *
 *   - "host:" (remote-path syntax for scp/sftp/rsync) returns no
 *     candidates -- the source defers remote-path completion to a
 *     future feature and must not collide with it by emitting hosts.
 */

#include "lle/completion/completion_sources.h"
#include "lle/completion/completion_types.h"
#include "lle/completion/ssh_hosts.h"
#include "lle/completion/word_context.h"
#include "lle/memory_management.h"
#include "test_framework.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Fixture management -------------------------------------------------- */

/* Make a temp $HOME with $HOME/.ssh/config populated by `config_body`.
 * Caller must ssh_test_teardown() the result to restore environment and
 * reclaim disk fixtures. */
typedef struct {
    char *home;       /* heap-allocated; freed by teardown */
    char *saved_home; /* prior $HOME (or NULL); restored by teardown */
} ssh_test_fixture_t;

static int ssh_test_setup(ssh_test_fixture_t *fx, const char *config_body) {
    char tmpl[] = "/tmp/lush_ssh_test_XXXXXX";
    char *dir = mkdtemp(tmpl);
    if (!dir)
        return -1;
    fx->home = strdup(dir);

    /* ssh_dir holds "${home}/.ssh"; config_path holds "${ssh_dir}/config".
     * Buffers are sized so the chained snprintfs cannot truncate when
     * home is a normal tmpdir path -- silences gcc -Wformat-truncation
     * over the worst-case length estimate. */
    char ssh_dir[1024];
    snprintf(ssh_dir, sizeof(ssh_dir), "%s/.ssh", fx->home);
    if (mkdir(ssh_dir, 0700) != 0) {
        free(fx->home);
        return -1;
    }

    char config_path[1040];
    snprintf(config_path, sizeof(config_path), "%s/config", ssh_dir);
    FILE *fp = fopen(config_path, "w");
    if (!fp) {
        rmdir(ssh_dir);
        free(fx->home);
        return -1;
    }
    fputs(config_body, fp);
    fclose(fp);

    /* Force the cache to re-read from the fixture HOME. */
    ssh_hosts_cleanup();

    const char *prior = getenv("HOME");
    fx->saved_home = prior ? strdup(prior) : NULL;
    setenv("HOME", fx->home, 1);

    if (ssh_hosts_init() != 0) {
        return -1;
    }
    return 0;
}

static void ssh_test_teardown(ssh_test_fixture_t *fx) {
    /* Cleanup cache before the fixture filesystem disappears. */
    ssh_hosts_cleanup();

    if (fx->home) {
        char ssh_dir[1024];
        char config_path[1040]; /* see ssh_test_setup sizing rationale */
        snprintf(ssh_dir, sizeof(ssh_dir), "%s/.ssh", fx->home);
        snprintf(config_path, sizeof(config_path), "%s/config", ssh_dir);
        unlink(config_path);
        rmdir(ssh_dir);
        rmdir(fx->home);
        free(fx->home);
    }

    if (fx->saved_home) {
        setenv("HOME", fx->saved_home, 1);
        free(fx->saved_home);
    } else {
        unsetenv("HOME");
    }
}

/* Build a minimal lle_word_context_t with just the fields the source
 * actually reads. The source touches dequoted_filename_prefix only; we
 * supply the rest as zeroed defaults. */
static void make_context(lle_word_context_t *ctx, const char *prefix) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->dequoted_filename_prefix = (char *)prefix;
}

/* Tests --------------------------------------------------------------- */

TEST(ssh_source_emits_configured_host) {
    /* Single Host stanza; bare prefix `exa` should match `example.com`. */
    ssh_test_fixture_t fx = {0};
    if (ssh_test_setup(&fx, "Host example.com\n  User alice\n") != 0) {
        TEST_FAIL_MSG("could not set up SSH fixture");
        return;
    }

    lle_memory_pool_t *pool = (lle_memory_pool_t *)1; /* LLE pool sentinel */
    lle_completion_result_t *result = NULL;
    lle_result_t r = lle_completion_result_create(pool, 8, &result);
    ASSERT(r == LLE_SUCCESS);

    lle_word_context_t ctx;
    make_context(&ctx, "exa");

    r = lle_completion_source_ssh_hosts(pool, &ctx, result);
    ASSERT(r == LLE_SUCCESS);

    bool found = false;
    for (size_t i = 0; i < result->count; i++) {
        const lle_completion_item_t *item = &result->items[i];
        /* Default user from the stanza should be honoured when the
         * user typed no `@` segment. */
        if (item->text && strcmp(item->text, "alice@example.com") == 0) {
            found = true;
            break;
        }
    }
    if (!found) {
        TEST_FAIL_MSG("expected `alice@example.com` in candidates");
    }

    ssh_test_teardown(&fx);
}

TEST(ssh_source_preserves_user_at_prefix) {
    /* When the user types `bob@`, the typed user wins over the User
     * directive in the Host stanza -- otherwise we'd silently override
     * the user's explicit choice. */
    ssh_test_fixture_t fx = {0};
    if (ssh_test_setup(&fx, "Host example.com\n  User alice\n") != 0) {
        TEST_FAIL_MSG("could not set up SSH fixture");
        return;
    }

    lle_memory_pool_t *pool = (lle_memory_pool_t *)1;
    lle_completion_result_t *result = NULL;
    lle_completion_result_create(pool, 8, &result);

    lle_word_context_t ctx;
    make_context(&ctx, "bob@exa");

    lle_result_t r = lle_completion_source_ssh_hosts(pool, &ctx, result);
    ASSERT(r == LLE_SUCCESS);

    bool found_bob = false;
    bool found_alice = false;
    for (size_t i = 0; i < result->count; i++) {
        const lle_completion_item_t *item = &result->items[i];
        if (!item->text)
            continue;
        if (strcmp(item->text, "bob@example.com") == 0)
            found_bob = true;
        if (strcmp(item->text, "alice@example.com") == 0)
            found_alice = true;
    }
    if (!found_bob) {
        TEST_FAIL_MSG("expected `bob@example.com` (typed user preserved)");
    }
    if (found_alice) {
        TEST_FAIL_MSG(
            "config-stanza user `alice@...` must NOT override typed `bob@`");
    }

    ssh_test_teardown(&fx);
}

TEST(ssh_source_skips_remote_path_syntax) {
    /* `host:path` is scp/sftp/rsync remote-path completion territory.
     * The source must not emit host candidates once the user has typed
     * the colon -- doing so would conflict with a future remote-path
     * source. */
    ssh_test_fixture_t fx = {0};
    if (ssh_test_setup(&fx, "Host example.com\n") != 0) {
        TEST_FAIL_MSG("could not set up SSH fixture");
        return;
    }

    lle_memory_pool_t *pool = (lle_memory_pool_t *)1;
    lle_completion_result_t *result = NULL;
    lle_completion_result_create(pool, 8, &result);

    lle_word_context_t ctx;
    make_context(&ctx, "example.com:");

    lle_result_t r = lle_completion_source_ssh_hosts(pool, &ctx, result);
    ASSERT(r == LLE_SUCCESS);

    if (result->count != 0) {
        TEST_FAIL_MSG("remote-path syntax must yield zero host candidates");
    }

    ssh_test_teardown(&fx);
}

/* /etc/hosts parser tests ------------------------------------------------ */

/* Write `body` to a fresh temp file and return its path (heap-allocated;
 * caller frees and unlinks). NULL on failure. */
static char *write_temp_file(const char *body) {
    char tmpl[] = "/tmp/lush_etc_hosts_XXXXXX";
    int fd = mkstemp(tmpl);
    if (fd < 0) {
        return NULL;
    }
    size_t n = strlen(body);
    if (write(fd, body, n) != (ssize_t)n) {
        close(fd);
        unlink(tmpl);
        return NULL;
    }
    close(fd);
    return strdup(tmpl);
}

TEST(etc_hosts_parses_primary_and_aliases) {
    /* Standard /etc/hosts shape: IP, primary, two aliases. All three
     * names should land in the cache. */
    const char *body = "127.0.0.1 localhost loopback lo\n"
                       "192.168.1.5 fileserver fs\n";
    char *path = write_temp_file(body);
    if (!path) {
        TEST_FAIL_MSG("could not write temp /etc/hosts fixture");
        return;
    }

    ssh_host_cache_t *cache = ssh_host_cache_create(64);
    int n = ssh_parse_etc_hosts(path, cache);

    bool got_localhost = ssh_host_cache_find(cache, "localhost") != NULL;
    bool got_loopback = ssh_host_cache_find(cache, "loopback") != NULL;
    bool got_lo = ssh_host_cache_find(cache, "lo") != NULL;
    bool got_fs = ssh_host_cache_find(cache, "fileserver") != NULL;
    bool got_fs_alias = ssh_host_cache_find(cache, "fs") != NULL;

    ssh_host_cache_destroy(cache);
    unlink(path);
    free(path);

    if (n != 5) {
        char msg[64];
        snprintf(msg, sizeof(msg), "expected 5 hosts added, got %d", n);
        TEST_FAIL_MSG(msg);
        return;
    }
    if (!(got_localhost && got_loopback && got_lo && got_fs && got_fs_alias)) {
        TEST_FAIL_MSG("missing one or more expected aliases from /etc/hosts");
        return;
    }
}

TEST(etc_hosts_skips_comments_and_blanks) {
    /* `#`-prefixed comment lines, blank lines, and end-of-line comments
     * must not produce candidates. */
    const char *body = "# leading comment\n"
                       "\n"
                       "10.0.0.1 alpha   # this is the gateway\n"
                       "   # indented comment\n"
                       "10.0.0.2 beta\n";
    char *path = write_temp_file(body);
    if (!path) {
        TEST_FAIL_MSG("could not write fixture");
        return;
    }

    ssh_host_cache_t *cache = ssh_host_cache_create(64);
    int n = ssh_parse_etc_hosts(path, cache);

    bool got_alpha = ssh_host_cache_find(cache, "alpha") != NULL;
    bool got_beta = ssh_host_cache_find(cache, "beta") != NULL;
    bool got_comment = ssh_host_cache_find(cache, "this") != NULL ||
                       ssh_host_cache_find(cache, "comment") != NULL;

    ssh_host_cache_destroy(cache);
    unlink(path);
    free(path);

    if (n != 2 || !got_alpha || !got_beta || got_comment) {
        char msg[128];
        snprintf(msg, sizeof(msg),
                 "expected only alpha+beta, got n=%d (alpha=%d beta=%d "
                 "comment-as-host=%d)",
                 n, (int)got_alpha, (int)got_beta, (int)got_comment);
        TEST_FAIL_MSG(msg);
        return;
    }
}

TEST(etc_hosts_dedupes_against_higher_priority_source) {
    /* If a host is already in the cache (e.g., from ssh_config) the
     * /etc/hosts entry is skipped so the higher-priority entry wins. */
    const char *body = "1.2.3.4 production-host\n";
    char *path = write_temp_file(body);
    if (!path) {
        TEST_FAIL_MSG("could not write fixture");
        return;
    }

    ssh_host_cache_t *cache = ssh_host_cache_create(64);

    /* Pre-populate with a fake higher-priority entry. */
    ssh_host_t existing = {0};
    strncpy(existing.hostname, "production-host", SSH_MAX_HOSTNAME_LEN - 1);
    existing.priority = 100;
    ssh_host_cache_add(cache, &existing);

    int n = ssh_parse_etc_hosts(path, cache);

    ssh_host_t *found = ssh_host_cache_find(cache, "production-host");
    int priority_after = found ? found->priority : -1;

    ssh_host_cache_destroy(cache);
    unlink(path);
    free(path);

    if (n != 0) {
        char msg[64];
        snprintf(msg, sizeof(msg),
                 "expected 0 new hosts (dedupe), got %d", n);
        TEST_FAIL_MSG(msg);
        return;
    }
    if (priority_after != 100) {
        char msg[64];
        snprintf(msg, sizeof(msg),
                 "expected higher-priority entry preserved, got pri %d",
                 priority_after);
        TEST_FAIL_MSG(msg);
        return;
    }
}

int main(void) {
    printf("=== SSH Host Completion Tests ===\n\n");

    RUN_TEST(ssh_source_emits_configured_host);
    RUN_TEST(ssh_source_preserves_user_at_prefix);
    RUN_TEST(ssh_source_skips_remote_path_syntax);
    RUN_TEST(etc_hosts_parses_primary_and_aliases);
    RUN_TEST(etc_hosts_skips_comments_and_blanks);
    RUN_TEST(etc_hosts_dedupes_against_higher_priority_source);

    return TEST_RESULT();
}
