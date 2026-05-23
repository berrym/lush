/**
 * @file ssh_hosts.c
 * @brief SSH Host Cache and Parsing for LLE Completion
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Provides SSH host completion by parsing:
 * - ~/.ssh/config for Host entries
 * - ~/.ssh/known_hosts for previously connected hosts
 *
 * This module is used by the LLE completion system to provide
 * SSH hostname completions for ssh, scp, rsync commands.
 */

#include "lle/completion/ssh_hosts.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

// Forward declaration for portability (see ht_fnv1a.c)
int strncasecmp(const char *s1, const char *s2, size_t n);

/* ============================================================================
 * CONSTANTS
 * ============================================================================
 */

#define SSH_CONFIG_CACHE_TIMEOUT 300 // 5 minutes
#define MAX_SSH_HOSTS 1000
#define MAX_CONFIG_LINE_LEN 1024
#define MAX_HOSTNAME_LEN 253
#define MAX_USERNAME_LEN 64
#define MAX_PORT_LEN 8

/* ============================================================================
 * GLOBAL STATE
 * ============================================================================
 */

static ssh_host_cache_t *g_ssh_host_cache = NULL;

/* ============================================================================
 * FORWARD DECLARATIONS
 * ============================================================================
 */

static void trim_whitespace(char *str);
static bool is_ssh_host_line(const char *line);
static bool is_ssh_hostname_line(const char *line);
static bool is_ssh_user_line(const char *line);
static bool is_ssh_port_line(const char *line);
static void extract_ssh_config_value(const char *line, char *value,
                                     size_t max_len);

/* ============================================================================
 * CACHE MANAGEMENT
 * ============================================================================
 */

/**
 * @brief Create a new SSH host cache
 * @param initial_capacity Initial capacity for host entries
 * @return New cache or NULL on allocation failure
 */
ssh_host_cache_t *ssh_host_cache_create(size_t initial_capacity) {
    ssh_host_cache_t *cache = calloc(1, sizeof(ssh_host_cache_t));
    if (!cache) {
        return NULL;
    }

    if (initial_capacity == 0) {
        initial_capacity = 64;
    }

    cache->hosts = calloc(initial_capacity, sizeof(ssh_host_t));
    if (!cache->hosts) {
        free(cache);
        return NULL;
    }

    cache->capacity = initial_capacity;
    cache->count = 0;
    cache->last_updated = 0;
    cache->needs_refresh = true;

    return cache;
}

/**
 * @brief Destroy an SSH host cache and free resources
 * @param cache Cache to destroy
 */
void ssh_host_cache_destroy(ssh_host_cache_t *cache) {
    if (!cache) {
        return;
    }

    free(cache->hosts);
    free(cache);
}

/**
 * @brief Add a host entry to the cache
 * @param cache Cache to add to
 * @param host Host entry to add (copied)
 * @return 0 on success, -1 on failure
 */
int ssh_host_cache_add(ssh_host_cache_t *cache, const ssh_host_t *host) {
    if (!cache || !host) {
        return -1;
    }

    // Grow if needed
    if (cache->count >= cache->capacity) {
        size_t new_capacity = cache->capacity * 2;
        ssh_host_t *new_hosts =
            realloc(cache->hosts, new_capacity * sizeof(ssh_host_t));
        if (!new_hosts) {
            return -1;
        }
        cache->hosts = new_hosts;
        cache->capacity = new_capacity;
    }

    // Copy host entry
    cache->hosts[cache->count] = *host;
    cache->count++;

    return 0;
}

/**
 * @brief Find a host entry by hostname or alias
 * @param cache Cache to search
 * @param hostname Hostname or alias to find
 * @return Pointer to host entry or NULL if not found
 */
ssh_host_t *ssh_host_cache_find(ssh_host_cache_t *cache, const char *hostname) {
    if (!cache || !hostname) {
        return NULL;
    }

    for (size_t i = 0; i < cache->count; i++) {
        if (strcmp(cache->hosts[i].hostname, hostname) == 0 ||
            strcmp(cache->hosts[i].alias, hostname) == 0) {
            return &cache->hosts[i];
        }
    }

    return NULL;
}

/* ============================================================================
 * SSH CONFIG PARSING
 * ============================================================================
 */

/**
 * @brief Parse SSH config file for host entries
 * @param config_path Path to SSH config file
 * @param cache Cache to populate
 * @return Number of hosts added, or -1 on error
 */
int ssh_parse_config(const char *config_path, ssh_host_cache_t *cache) {
    if (!config_path || !cache) {
        return -1;
    }

    FILE *file = fopen(config_path, "r");
    if (!file) {
        return -1;
    }

    char line[MAX_CONFIG_LINE_LEN];
    ssh_host_t current_host = {0};
    bool in_host_block = false;
    int hosts_added = 0;

    while (fgets(line, sizeof(line), file)) {
        line[strcspn(line, "\n")] = '\0';
        trim_whitespace(line);

        if (line[0] == '\0' || line[0] == '#') {
            continue;
        }

        if (is_ssh_host_line(line)) {
            // Save previous host if valid
            if (in_host_block && current_host.hostname[0]) {
                current_host.from_config = true;
                current_host.priority = 80;
                if (ssh_host_cache_add(cache, &current_host) == 0) {
                    hosts_added++;
                }
            }

            // Start new host block
            memset(&current_host, 0, sizeof(current_host));
            extract_ssh_config_value(line, current_host.alias,
                                     MAX_HOSTNAME_LEN);

            // If alias doesn't contain wildcards, use as hostname too
            if (!strchr(current_host.alias, '*') &&
                !strchr(current_host.alias, '?')) {
                strncpy(current_host.hostname, current_host.alias,
                        MAX_HOSTNAME_LEN - 1);
                current_host.hostname[MAX_HOSTNAME_LEN - 1] = '\0';
            }

            in_host_block = true;
        } else if (in_host_block) {
            if (is_ssh_hostname_line(line)) {
                extract_ssh_config_value(line, current_host.hostname,
                                         MAX_HOSTNAME_LEN);
            } else if (is_ssh_user_line(line)) {
                extract_ssh_config_value(line, current_host.user,
                                         MAX_USERNAME_LEN);
            } else if (is_ssh_port_line(line)) {
                extract_ssh_config_value(line, current_host.port, MAX_PORT_LEN);
            }
        }
    }

    // Save last host
    if (in_host_block && current_host.hostname[0]) {
        current_host.from_config = true;
        current_host.priority = 80;
        if (ssh_host_cache_add(cache, &current_host) == 0) {
            hosts_added++;
        }
    }

    fclose(file);
    return hosts_added;
}

/**
 * @brief Parse known_hosts file for previously connected hosts
 * @param known_hosts_path Path to known_hosts file
 * @param cache Cache to populate
 * @return Number of hosts added, or -1 on error
 */
int ssh_parse_known_hosts(const char *known_hosts_path,
                          ssh_host_cache_t *cache) {
    if (!known_hosts_path || !cache) {
        return -1;
    }

    FILE *file = fopen(known_hosts_path, "r");
    if (!file) {
        return -1;
    }

    char line[MAX_CONFIG_LINE_LEN];
    int hosts_added = 0;

    while (fgets(line, sizeof(line), file)) {
        line[strcspn(line, "\n")] = '\0';

        // Skip empty lines, comments, and hashed entries
        if (line[0] == '\0' || line[0] == '#' || line[0] == '|') {
            continue;
        }

        // Extract hostname (first field before space)
        char *space = strchr(line, ' ');
        if (!space) {
            continue;
        }

        size_t hostname_len = space - line;
        if (hostname_len >= MAX_HOSTNAME_LEN) {
            hostname_len = MAX_HOSTNAME_LEN - 1;
        }

        ssh_host_t host = {0};
        strncpy(host.hostname, line, hostname_len);
        host.hostname[hostname_len] = '\0';

        // Handle [host]:port format
        if (host.hostname[0] == '[') {
            char *bracket = strchr(host.hostname, ']');
            if (bracket) {
                memmove(host.hostname, host.hostname + 1,
                        bracket - host.hostname - 1);
                host.hostname[bracket - host.hostname - 1] = '\0';
            }
        }

        // Remove any comma-separated aliases, keep first
        char *comma = strchr(host.hostname, ',');
        if (comma) {
            *comma = '\0';
        }

        // Skip if already in cache
        if (ssh_host_cache_find(cache, host.hostname)) {
            continue;
        }

        host.from_known_hosts = true;
        host.priority = 50;

        if (ssh_host_cache_add(cache, &host) == 0) {
            hosts_added++;
        }
    }

    fclose(file);
    return hosts_added;
}

/**
 * @brief Parse /etc/hosts (or compatible NSS hostname database file).
 *
 * Each non-comment line has the form `IP host [alias ...]`. The IP is
 * skipped; every subsequent whitespace-separated field on the line is
 * treated as a hostname or alias and added to the cache (deduped
 * against earlier higher-priority sources). A `#` anywhere on the
 * line begins a comment and is truncated. Blank lines are skipped.
 *
 * Hostnames from this source get priority 20 -- below ssh config
 * (100) and known_hosts (50) so user-tuned entries appear first.
 *
 * @param etc_hosts_path Path to /etc/hosts file
 * @param cache Cache to populate
 * @return Number of hosts added, -1 on error
 */
int ssh_parse_etc_hosts(const char *etc_hosts_path, ssh_host_cache_t *cache) {
    if (!etc_hosts_path || !cache) {
        return -1;
    }

    FILE *file = fopen(etc_hosts_path, "r");
    if (!file) {
        return -1;
    }

    char line[MAX_CONFIG_LINE_LEN];
    int hosts_added = 0;

    while (fgets(line, sizeof(line), file)) {
        // Strip newline and trailing CR.
        line[strcspn(line, "\r\n")] = '\0';

        // Truncate at any '#' (comment to end of line).
        char *hash = strchr(line, '#');
        if (hash) {
            *hash = '\0';
        }

        // Skip the IP address (first whitespace-separated field).
        char *p = line;
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (*p == '\0') {
            continue; // blank or comment-only
        }
        while (*p != '\0' && *p != ' ' && *p != '\t') {
            p++; // consume IP
        }

        // Each remaining whitespace-separated token is a hostname/alias.
        while (*p != '\0') {
            while (*p == ' ' || *p == '\t') {
                p++;
            }
            if (*p == '\0') {
                break;
            }
            char *name_start = p;
            while (*p != '\0' && *p != ' ' && *p != '\t') {
                p++;
            }
            size_t name_len = (size_t)(p - name_start);
            if (name_len == 0 || name_len >= MAX_HOSTNAME_LEN) {
                continue;
            }

            ssh_host_t host = {0};
            memcpy(host.hostname, name_start, name_len);
            host.hostname[name_len] = '\0';

            // Skip duplicates (priority: earlier sources win).
            if (ssh_host_cache_find(cache, host.hostname)) {
                continue;
            }

            host.from_known_hosts = false;
            host.priority = 20;

            if (ssh_host_cache_add(cache, &host) == 0) {
                hosts_added++;
            }
        }
    }

    fclose(file);
    return hosts_added;
}

/* ============================================================================
 * GLOBAL CACHE ACCESS
 * ============================================================================
 */

/**
 * @brief Initialize the global SSH host cache
 * @return 0 on success, -1 on failure
 */
int ssh_hosts_init(void) {
    if (g_ssh_host_cache) {
        return 0; // Already initialized
    }

    g_ssh_host_cache = ssh_host_cache_create(MAX_SSH_HOSTS);
    if (!g_ssh_host_cache) {
        return -1;
    }

    ssh_hosts_refresh();
    return 0;
}

/**
 * @brief Clean up global SSH host cache
 */
void ssh_hosts_cleanup(void) {
    if (g_ssh_host_cache) {
        ssh_host_cache_destroy(g_ssh_host_cache);
        g_ssh_host_cache = NULL;
    }
}

/**
 * @brief Refresh global SSH host cache from config files
 */
void ssh_hosts_refresh(void) {
    if (!g_ssh_host_cache) {
        return;
    }

    // Clear existing entries
    g_ssh_host_cache->count = 0;

    // Get home directory
    const char *home = getenv("HOME");
    if (!home) {
        return;
    }

    char path[1024];

    /* Source list matches docs/COMPLETION_SYSTEM.md §SSH host
     * completion. Order matters: ssh_host_cache_find dedupes by
     * hostname and earlier entries win, so higher-priority sources are
     * read first.
     *
     *   1. ~/.ssh/config            -- user-curated Host stanzas (pri 100)
     *   2. ~/.ssh/known_hosts       -- hosts the user has connected to (pri 50)
     *   3. /etc/ssh/ssh_known_hosts -- system-wide known hosts (pri 50)
     *   4. /etc/hosts               -- NSS hostname database (pri 20)
     *
     * Note: /etc/ssh/ssh_config (SSH *client* config) is intentionally
     * NOT a source -- the doc only promises the four files above and
     * sysadmin-defined Host stanzas in the client config are unusual
     * enough that they should be opted into by the user copying the
     * relevant stanzas to ~/.ssh/config. */

    // User SSH config
    snprintf(path, sizeof(path), "%s/.ssh/config", home);
    ssh_parse_config(path, g_ssh_host_cache);

    // User known_hosts
    snprintf(path, sizeof(path), "%s/.ssh/known_hosts", home);
    ssh_parse_known_hosts(path, g_ssh_host_cache);

    // System-wide known_hosts
    ssh_parse_known_hosts("/etc/ssh/ssh_known_hosts", g_ssh_host_cache);

    // NSS hostname database
    ssh_parse_etc_hosts("/etc/hosts", g_ssh_host_cache);

    g_ssh_host_cache->last_updated = time(NULL);
    g_ssh_host_cache->needs_refresh = false;
}

/**
 * @brief Get the global SSH host cache, initializing if needed
 * @return Pointer to global cache
 */
ssh_host_cache_t *get_ssh_host_cache(void) {
    if (!g_ssh_host_cache) {
        ssh_hosts_init();
    }

    // Check if refresh needed
    if (g_ssh_host_cache && g_ssh_host_cache->needs_refresh) {
        ssh_hosts_refresh();
    }

    // Check cache timeout
    if (g_ssh_host_cache) {
        time_t now = time(NULL);
        if (now - g_ssh_host_cache->last_updated > SSH_CONFIG_CACHE_TIMEOUT) {
            ssh_hosts_refresh();
        }
    }

    return g_ssh_host_cache;
}

/* ============================================================================
 * UTILITY FUNCTIONS
 * ============================================================================
 */

/**
 * @brief Trim leading and trailing whitespace from a string
 *
 * Modifies the string in place by removing whitespace characters
 * from both ends.
 *
 * @param str String to trim (modified in place)
 */
static void trim_whitespace(char *str) {
    if (!str) {
        return;
    }

    char *start = str;
    while (*start && isspace((unsigned char)*start)) {
        start++;
    }

    if (start != str) {
        memmove(str, start, strlen(start) + 1);
    }

    char *end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) {
        *end-- = '\0';
    }
}

/**
 * @brief Check if line is an SSH Host directive
 *
 * @param line Line to check
 * @return true if line starts with "Host " or "Host\t"
 */
static bool is_ssh_host_line(const char *line) {
    return strncasecmp(line, "Host ", 5) == 0 ||
           strncasecmp(line, "Host\t", 5) == 0;
}

/**
 * @brief Check if line is an SSH HostName directive
 *
 * @param line Line to check
 * @return true if line starts with "HostName " or "HostName\t"
 */
static bool is_ssh_hostname_line(const char *line) {
    return strncasecmp(line, "HostName ", 9) == 0 ||
           strncasecmp(line, "HostName\t", 9) == 0;
}

/**
 * @brief Check if line is an SSH User directive
 *
 * @param line Line to check
 * @return true if line starts with "User " or "User\t"
 */
static bool is_ssh_user_line(const char *line) {
    return strncasecmp(line, "User ", 5) == 0 ||
           strncasecmp(line, "User\t", 5) == 0;
}

/**
 * @brief Check if line is an SSH Port directive
 *
 * @param line Line to check
 * @return true if line starts with "Port " or "Port\t"
 */
static bool is_ssh_port_line(const char *line) {
    return strncasecmp(line, "Port ", 5) == 0 ||
           strncasecmp(line, "Port\t", 5) == 0;
}

/**
 * @brief Extract value from an SSH config directive line
 *
 * Extracts the value portion of a config line (after the keyword
 * and whitespace), with trailing whitespace trimmed.
 *
 * @param line Config line to parse
 * @param value Output buffer for extracted value
 * @param max_len Maximum length of output buffer
 */
static void extract_ssh_config_value(const char *line, char *value,
                                     size_t max_len) {
    if (!line || !value || max_len == 0) {
        return;
    }

    // Skip keyword
    const char *p = line;
    while (*p && !isspace((unsigned char)*p)) {
        p++;
    }

    // Skip whitespace
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }

    // Copy value
    size_t len = strlen(p);
    if (len >= max_len) {
        len = max_len - 1;
    }
    strncpy(value, p, len);
    value[len] = '\0';

    // Remove trailing whitespace
    trim_whitespace(value);
}
