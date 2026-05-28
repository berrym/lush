/**
 * @file autoload.c
 * @brief Zsh-style lazy function loading
 *
 * Registers names declared via `autoload`; on first call to a declared
 * name, locates the function file via fpath, wraps the body in
 * `function NAME { ... }`, and routes through the executor so the
 * resulting function joins the live function table.
 *
 * Fpath resolution order:
 *   1. $FPATH environment variable (colon-separated, POSIX-style)
 *   2. Platform-default zsh function directory globs
 *
 * Lush does not maintain its own `fpath` zsh-array yet; $FPATH covers
 * the corpus needs (prezto and oh-my-zsh both rely on autoloadable
 * functions but inherit fpath from the user's interactive shell, so
 * the env var is the right entry point for non-interactive runs).
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "autoload.h"

#include "executor.h"
#include "lush.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* ============================================================================
 * Registry
 * ============================================================================
 *
 * Small singly-linked list keyed by name. The set of autoloadable
 * names a script declares is small in practice (oh-my-zsh's plugin
 * loader rarely tops a few dozen); a list keeps the implementation
 * obvious and avoids dragging in a hash table just for this. Replace
 * with the strstr hashtable if profiling justifies it.
 */

typedef struct autoload_entry {
    char *name;
    struct autoload_entry *next;
} autoload_entry_t;

static autoload_entry_t *g_registry = NULL;

static autoload_entry_t *registry_find(const char *name) {
    for (autoload_entry_t *e = g_registry; e != NULL; e = e->next) {
        if (strcmp(e->name, name) == 0) {
            return e;
        }
    }
    return NULL;
}

static void registry_remove(const char *name) {
    autoload_entry_t **slot = &g_registry;
    while (*slot != NULL) {
        if (strcmp((*slot)->name, name) == 0) {
            autoload_entry_t *dead = *slot;
            *slot = dead->next;
            free(dead->name);
            free(dead);
            return;
        }
        slot = &(*slot)->next;
    }
}

bool autoload_register(const char *name) {
    if (!name || !*name) {
        return false;
    }
    if (registry_find(name)) {
        return true;
    }
    autoload_entry_t *e = malloc(sizeof(*e));
    if (!e) {
        return false;
    }
    e->name = strdup(name);
    if (!e->name) {
        free(e);
        return false;
    }
    e->next = g_registry;
    g_registry = e;
    return true;
}

bool autoload_is_declared(const char *name) {
    if (!name) {
        return false;
    }
    return registry_find(name) != NULL;
}

void autoload_clear(void) {
    autoload_entry_t *e = g_registry;
    while (e) {
        autoload_entry_t *next = e->next;
        free(e->name);
        free(e);
        e = next;
    }
    g_registry = NULL;
}

/* ============================================================================
 * Fpath resolution
 * ============================================================================
 */

/**
 * @brief Default fpath globs when $FPATH is unset
 *
 * Covers macOS (Homebrew + system zsh), Debian/Ubuntu, Fedora/Arch.
 * Each entry is a `glob(3)` pattern resolved at lookup time so we pick
 * up whichever zsh version is actually installed without baking the
 * version number into source. The glob is permissive — non-existent
 * paths are silently skipped.
 */
static const char *DEFAULT_FPATH_GLOBS[] = {
    "/usr/share/zsh/*/functions",
    "/usr/share/zsh/*/functions/*",
    "/usr/local/share/zsh/*/functions",
    "/usr/local/share/zsh/*/functions/*",
    "/opt/homebrew/share/zsh/*/functions",
    "/opt/homebrew/share/zsh/*/functions/*",
    NULL,
};

static bool is_regular_file(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

/**
 * @brief Try `dir/name` as a function file; on hit, write to `out_path`
 *
 * @param dir       Directory to search
 * @param name      Function file name (no slashes)
 * @param out_path  Buffer to receive the resolved path on success
 * @param out_size  Size of out_path
 * @return true if `dir/name` is a readable regular file.
 */
static bool try_dir(const char *dir, const char *name, char *out_path,
                    size_t out_size) {
    int n = snprintf(out_path, out_size, "%s/%s", dir, name);
    if (n <= 0 || (size_t)n >= out_size) {
        return false;
    }
    return is_regular_file(out_path);
}

/**
 * @brief Locate `name` on fpath; write resolved path into `out_path`
 *
 * Searches $FPATH first, then DEFAULT_FPATH_GLOBS. Returns false if no
 * candidate exists.
 */
static bool resolve_on_fpath(const char *name, char *out_path,
                             size_t out_size) {
    if (!name || strchr(name, '/')) {
        /// Defense against path traversal: a name containing `/` is not
        /// an autoload candidate, it's a path. Reject.
        return false;
    }

    const char *fpath_env = getenv("FPATH");
    if (fpath_env && *fpath_env) {
        char *fpath_copy = strdup(fpath_env);
        if (!fpath_copy) {
            return false;
        }
        char *saveptr = NULL;
        for (char *dir = strtok_r(fpath_copy, ":", &saveptr); dir != NULL;
             dir = strtok_r(NULL, ":", &saveptr)) {
            if (try_dir(dir, name, out_path, out_size)) {
                free(fpath_copy);
                return true;
            }
        }
        free(fpath_copy);
    }

    /// Platform defaults via glob expansion. We scan glob results in
    /// order so /usr/share/zsh/<ver>/functions resolves before its
    /// subdirectories (the Misc/, Zle/, Completion/ layout).
    for (int i = 0; DEFAULT_FPATH_GLOBS[i] != NULL; i++) {
        glob_t gl;
        memset(&gl, 0, sizeof(gl));
        if (glob(DEFAULT_FPATH_GLOBS[i], 0, NULL, &gl) == 0) {
            for (size_t j = 0; j < gl.gl_pathc; j++) {
                if (try_dir(gl.gl_pathv[j], name, out_path, out_size)) {
                    globfree(&gl);
                    return true;
                }
            }
        }
        globfree(&gl);
    }

    return false;
}

/* ============================================================================
 * Sourcing
 * ============================================================================
 */

/**
 * @brief Slurp file at `path` into a newly-allocated NUL-terminated buffer
 *
 * Returns NULL on any I/O failure; caller owns the returned buffer.
 */
static char *read_file(const char *path) {
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        return NULL;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    long size = ftell(fp);
    if (size < 0 || fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return NULL;
    }
    char *buf = malloc((size_t)size + 1);
    if (!buf) {
        fclose(fp);
        return NULL;
    }
    size_t n = fread(buf, 1, (size_t)size, fp);
    fclose(fp);
    if (n != (size_t)size) {
        free(buf);
        return NULL;
    }
    buf[size] = '\0';
    return buf;
}

bool autoload_try_resolve(struct executor *executor, const char *name) {
    if (!executor || !name || !*name) {
        return false;
    }
    if (!autoload_is_declared(name)) {
        return false;
    }

    char path[4096];
    if (!resolve_on_fpath(name, path, sizeof(path))) {
        /// Not on fpath; leave the entry in the registry so a later
        /// autoload-eligible run (with a different FPATH) could still
        /// resolve it. The dispatcher will fall through to PATH lookup
        /// and ultimately "command not found".
        return false;
    }

    char *body = read_file(path);
    if (!body) {
        return false;
    }

    /// Wrap the file contents as a function body. Real zsh autoload
    /// files are usually bare bodies (the function name comes from the
    /// file name) but some are already wrapped in `funcname() { ... }`.
    /// For lush we always wrap: defining `name` redefines an existing
    /// function safely, and a bare body needs the wrapper to be
    /// callable. The cost is a redundant nesting when the file already
    /// had its own definition, which is benign.
    size_t wrap_len = strlen(name) + strlen(body) + 32;
    char *wrapped = malloc(wrap_len);
    if (!wrapped) {
        free(body);
        return false;
    }
    snprintf(wrapped, wrap_len, "function %s {\n%s\n}\n", name, body);
    free(body);

    /// Capture stderr around the source step. Autoloaded files live in
    /// the system zsh function tree; they exercise zsh-only syntax (glob
    /// qualifiers, parameter flags, etc.) that lush's parser may reject.
    /// Letting those parse errors land on the script's stderr would
    /// surface lush's own gaps as if the script itself had failed. By
    /// suppressing them here we keep autoload's contract narrow:
    /// succeed silently or fail silently — diagnostics about lush's
    /// gaps belong in a debug channel, not the script's stderr.
    fflush(stderr);
    int saved_stderr = dup(STDERR_FILENO);
    int devnull = open("/dev/null", O_WRONLY);
    if (saved_stderr >= 0 && devnull >= 0) {
        dup2(devnull, STDERR_FILENO);
    }
    if (devnull >= 0) {
        close(devnull);
    }

    int rc = executor_execute_command_line(executor, wrapped, 1);

    fflush(stderr);
    if (saved_stderr >= 0) {
        dup2(saved_stderr, STDERR_FILENO);
        close(saved_stderr);
    }
    free(wrapped);

    if (rc != 0) {
        /// Function definition itself failed (syntax error in the file,
        /// etc). Leave registry untouched so the caller sees no resolution.
        return false;
    }

    registry_remove(name);
    return true;
}
