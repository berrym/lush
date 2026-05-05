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

    char ssh_dir[1024];
    snprintf(ssh_dir, sizeof(ssh_dir), "%s/.ssh", fx->home);
    if (mkdir(ssh_dir, 0700) != 0) {
        free(fx->home);
        return -1;
    }

    char config_path[1024];
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
        char ssh_dir[1024], config_path[1024];
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

int main(void) {
    printf("=== SSH Host Completion Tests ===\n\n");

    RUN_TEST(ssh_source_emits_configured_host);
    RUN_TEST(ssh_source_preserves_user_at_prefix);
    RUN_TEST(ssh_source_skips_remote_path_syntax);

    return TEST_RESULT();
}
