/**
 * @file bin_network.c
 * @brief `network` builtin -- network/SSH host management
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "builtins.h"
#include "lle/completion/ssh_hosts.h"

/**
 * @brief Manage network and SSH host completion
 *
 * Provides subcommands for SSH host management:
 * - (no args): Show SSH host count and status
 * - hosts: List all cached SSH hosts
 * - refresh: Reload SSH host cache from config files
 * - help: Show usage information
 *
 * @param argc Argument count
 * @param argv Argument vector with subcommand
 * @return 0 on success, 1 on unknown command or error
 */
int bin_network(int argc, char **argv) {
    if (!argv) {
        return 1;
    }

    /// No arguments - show SSH host count
    if (argc == 1) {
        ssh_host_cache_t *cache = get_ssh_host_cache();
        printf("SSH Hosts Status:\n");
        printf("  Hosts cached: %zu\n", cache ? cache->count : 0);
        printf("  Remote session: %s\n", getenv("SSH_CLIENT") ? "Yes" : "No");
        return 0;
    }

    /// Handle subcommands
    if (strcmp(argv[1], "hosts") == 0) {
        ssh_host_cache_t *cache = get_ssh_host_cache();
        if (!cache || cache->count == 0) {
            printf("No SSH hosts found\n");
            printf("Check ~/.ssh/config and ~/.ssh/known_hosts\n");
            return 0;
        }

        printf("SSH Hosts (%zu total):\n", cache->count);
        printf("%-25s %-25s %-8s %s\n", "Alias", "Hostname", "Port", "Source");
        printf("%-25s %-25s %-8s %s\n", "-----", "--------", "----", "------");

        for (size_t i = 0; i < cache->count && i < 50; i++) {
            ssh_host_t *host = &cache->hosts[i];
            const char *alias = host->alias[0] ? host->alias : "-";
            const char *hostname = host->hostname[0] ? host->hostname : "-";
            const char *port = host->port[0] ? host->port : "22";
            const char *source = host->from_config ? "config" : "known_hosts";
            printf("%-25s %-25s %-8s %s\n", alias, hostname, port, source);
        }

        if (cache->count > 50) {
            printf("... and %zu more hosts\n", cache->count - 50);
        }
        return 0;
    }

    if (strcmp(argv[1], "refresh") == 0) {
        printf("Refreshing SSH host cache...\n");
        ssh_hosts_refresh();
        ssh_host_cache_t *cache = get_ssh_host_cache();
        printf("Loaded %zu SSH hosts\n", cache ? cache->count : 0);
        return 0;
    }

    if (strcmp(argv[1], "help") == 0) {
        printf("Usage: network [command]\n\n");
        printf("Commands:\n");
        printf("  hosts    - List SSH hosts from config and known_hosts\n");
        printf("  refresh  - Refresh SSH host cache\n");
        printf("  help     - Show this help message\n");
        return 0;
    }

    printf("network: unknown command '%s'\n", argv[1]);
    printf("Use 'network help' for usage\n");
    return 1;
}
