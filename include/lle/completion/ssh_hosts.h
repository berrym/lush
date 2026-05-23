/**
 * @file ssh_hosts.h
 * @brief SSH Host Cache and Parsing for LLE Completion
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Provides SSH host completion by parsing:
 * - ~/.ssh/config for Host entries
 * - ~/.ssh/known_hosts for previously connected hosts
 */

#ifndef LLE_SSH_HOSTS_H
#define LLE_SSH_HOSTS_H

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * TYPES
 * ============================================================================
 */

#define SSH_MAX_HOSTNAME_LEN 254
#define SSH_MAX_USERNAME_LEN 65
#define SSH_MAX_PORT_LEN 9

/**
 * SSH host entry
 */
typedef struct ssh_host {
    char hostname[SSH_MAX_HOSTNAME_LEN]; /**< Host or HostName */
    char alias[SSH_MAX_HOSTNAME_LEN];    /**< Host alias from config */
    char user[SSH_MAX_USERNAME_LEN];     /**< User from config */
    char port[SSH_MAX_PORT_LEN];         /**< Port from config */
    bool from_config;                    /**< True if from SSH config */
    bool from_known_hosts;               /**< True if from known_hosts */
    int priority;                        /**< Completion priority (0-100) */
} ssh_host_t;

/**
 * SSH host cache
 */
typedef struct ssh_host_cache {
    ssh_host_t *hosts;   /**< Array of SSH hosts */
    size_t count;        /**< Number of hosts */
    size_t capacity;     /**< Allocated capacity */
    time_t last_updated; /**< Cache timestamp */
    bool needs_refresh;  /**< Refresh flag */
} ssh_host_cache_t;

/* ============================================================================
 * CACHE MANAGEMENT
 * ============================================================================
 */

/**
 * Create a new SSH host cache
 *
 * @param initial_capacity Initial capacity (0 for default)
 * @return New cache or NULL on error
 */
ssh_host_cache_t *ssh_host_cache_create(size_t initial_capacity);

/**
 * Destroy an SSH host cache
 *
 * @param cache Cache to destroy
 */
void ssh_host_cache_destroy(ssh_host_cache_t *cache);

/**
 * Add a host to the cache
 *
 * @param cache Cache to add to
 * @param host Host to add
 * @return 0 on success, -1 on error
 */
int ssh_host_cache_add(ssh_host_cache_t *cache, const ssh_host_t *host);

/**
 * Find a host in the cache
 *
 * @param cache Cache to search
 * @param hostname Hostname or alias to find
 * @return Host entry or NULL if not found
 */
ssh_host_t *ssh_host_cache_find(ssh_host_cache_t *cache, const char *hostname);

/* ============================================================================
 * PARSING
 * ============================================================================
 */

/**
 * Parse SSH config file
 *
 * @param config_path Path to config file
 * @param cache Cache to add hosts to
 * @return Number of hosts added, -1 on error
 */
int ssh_parse_config(const char *config_path, ssh_host_cache_t *cache);

/**
 * Parse SSH known_hosts file
 *
 * @param known_hosts_path Path to known_hosts file
 * @param cache Cache to add hosts to
 * @return Number of hosts added, -1 on error
 */
int ssh_parse_known_hosts(const char *known_hosts_path,
                          ssh_host_cache_t *cache);

/**
 * Parse /etc/hosts (or compatible NSS hostname database file). Each
 * non-comment line lists an IP address followed by one or more
 * hostnames; every hostname becomes a completion candidate. Hostnames
 * already present in the cache (from higher-priority sources) are
 * skipped so user files keep their entries.
 *
 * @param etc_hosts_path Path to /etc/hosts (or fixture file)
 * @param cache Cache to add hosts to
 * @return Number of hosts added, -1 on error
 */
int ssh_parse_etc_hosts(const char *etc_hosts_path, ssh_host_cache_t *cache);

/* ============================================================================
 * GLOBAL CACHE
 * ============================================================================
 */

/**
 * Initialize the global SSH host cache
 *
 * @return 0 on success, -1 on error
 */
int ssh_hosts_init(void);

/**
 * Cleanup the global SSH host cache
 */
void ssh_hosts_cleanup(void);

/**
 * Refresh the global SSH host cache
 */
void ssh_hosts_refresh(void);

/**
 * Get the global SSH host cache
 *
 * Initializes the cache if needed and refreshes if stale.
 *
 * @return Global cache or NULL on error
 */
ssh_host_cache_t *get_ssh_host_cache(void);

#ifdef __cplusplus
}
#endif

#endif // LLE_SSH_HOSTS_H
