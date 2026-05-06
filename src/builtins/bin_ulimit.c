/**
 * @file bin_ulimit.c
 * @brief `ulimit` builtin -- get/set process resource limits
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#include "builtins.h"
#include "executor.h"
#include "shell_error.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <unistd.h>

/**
 * @brief Set or display resource limits
 *
 * Displays or modifies shell resource limits (file size, open files,
 * CPU time, stack size, etc.). Supports -a to show all limits,
 * -H for hard limits, -S for soft limits.
 *
 * @param argc Argument count
 * @param argv Argument vector with options and limit values
 * @return 0 on success, 1 on error
 */
int bin_ulimit(int argc, char **argv) {
    int opt;
    int resource = RLIMIT_FSIZE; // Default to file size limit
    bool show_all = false;
    bool hard_limit = false;
    char *limit_value = NULL;

    // Reset getopt for this call
    optind = 1;

    // Parse options
    while ((opt = getopt(argc, argv, "aHSfntsuvh")) != -1) {
        switch (opt) {
        case 'a':
            show_all = true;
            break;
        case 'H':
            hard_limit = true;
            break;
        case 'S':
            hard_limit = false; // Soft limit (default)
            break;
        case 'f':
            resource = RLIMIT_FSIZE;
            break;
        case 'n':
            resource = RLIMIT_NOFILE;
            break;
        case 't':
            resource = RLIMIT_CPU;
            break;
        case 's':
#ifdef RLIMIT_STACK
            resource = RLIMIT_STACK;
#else
            fprintf(stderr, "ulimit: -s not supported on this system\n");
            return 1;
#endif
            break;
        case 'u':
#ifdef RLIMIT_NPROC
            resource = RLIMIT_NPROC;
#else
            fprintf(stderr, "ulimit: -u not supported on this system\n");
            return 1;
#endif
            break;
        case 'v':
#ifdef RLIMIT_AS
            resource = RLIMIT_AS;
#else
            fprintf(stderr, "ulimit: -v not supported on this system\n");
            return 1;
#endif
            break;
        case 'h':
            printf("ulimit: set or display resource limits\n");
            printf("Options:\n");
            printf("  -a     Display all limits\n");
            printf("  -H     Set hard limit\n");
            printf("  -S     Set soft limit (default)\n");
            printf("  -f     File size limit (512-byte blocks)\n");
            printf("  -n     Number of open files\n");
            printf("  -t     CPU time limit (seconds)\n");
#ifdef RLIMIT_STACK
            printf("  -s     Stack size limit (1024-byte blocks)\n");
#endif
#ifdef RLIMIT_NPROC
            printf("  -u     Number of user processes\n");
#endif
#ifdef RLIMIT_AS
            printf("  -v     Virtual memory limit (1024-byte blocks)\n");
#endif
            return 0;
        default:
            fprintf(stderr, "ulimit: invalid option -%c\n", optopt);
            return 1;
        }
    }

    // Get remaining argument (limit value)
    if (optind < argc) {
        limit_value = argv[optind];
    }

    if (show_all) {
        // Display all limits
        struct rlimit rlim;

#ifdef RLIMIT_CORE
        printf("core file size          (blocks, -c) ");
        if (getrlimit(RLIMIT_CORE, &rlim) == 0) {
            if (rlim.rlim_cur == RLIM_INFINITY) {
                printf("unlimited\n");
            } else {
                printf("%lu\n", (unsigned long)(rlim.rlim_cur / 512));
            }
        } else {
            printf("unknown\n");
        }
#endif

#ifdef RLIMIT_DATA
        printf("data seg size           (kbytes, -d) ");
        if (getrlimit(RLIMIT_DATA, &rlim) == 0) {
            if (rlim.rlim_cur == RLIM_INFINITY) {
                printf("unlimited\n");
            } else {
                printf("%lu\n", (unsigned long)(rlim.rlim_cur / 1024));
            }
        } else {
            printf("unknown\n");
        }
#endif

        printf("file size               (blocks, -f) ");
        if (getrlimit(RLIMIT_FSIZE, &rlim) == 0) {
            if (rlim.rlim_cur == RLIM_INFINITY) {
                printf("unlimited\n");
            } else {
                printf("%lu\n", (unsigned long)(rlim.rlim_cur / 512));
            }
        } else {
            printf("unknown\n");
        }

        printf("open files                    (-n) ");
        if (getrlimit(RLIMIT_NOFILE, &rlim) == 0) {
            if (rlim.rlim_cur == RLIM_INFINITY) {
                printf("unlimited\n");
            } else {
                printf("%lu\n", (unsigned long)rlim.rlim_cur);
            }
        } else {
            printf("unknown\n");
        }

        printf("stack size              (kbytes, -s) ");
        if (getrlimit(RLIMIT_STACK, &rlim) == 0) {
            if (rlim.rlim_cur == RLIM_INFINITY) {
                printf("unlimited\n");
            } else {
                printf("%lu\n", (unsigned long)(rlim.rlim_cur / 1024));
            }
        } else {
            printf("unknown\n");
        }

        printf("cpu time               (seconds, -t) ");
        if (getrlimit(RLIMIT_CPU, &rlim) == 0) {
            if (rlim.rlim_cur == RLIM_INFINITY) {
                printf("unlimited\n");
            } else {
                printf("%lu\n", (unsigned long)rlim.rlim_cur);
            }
        } else {
            printf("unknown\n");
        }

#ifdef RLIMIT_NPROC
        printf("max user processes            (-u) ");
        if (getrlimit(RLIMIT_NPROC, &rlim) == 0) {
            if (rlim.rlim_cur == RLIM_INFINITY) {
                printf("unlimited\n");
            } else {
                printf("%lu\n", (unsigned long)rlim.rlim_cur);
            }
        } else {
            printf("unknown\n");
        }
#endif

#ifdef RLIMIT_AS
        printf("virtual memory          (kbytes, -v) ");
        if (getrlimit(RLIMIT_AS, &rlim) == 0) {
            if (rlim.rlim_cur == RLIM_INFINITY) {
                printf("unlimited\n");
            } else {
                printf("%lu\n", (unsigned long)(rlim.rlim_cur / 1024));
            }
        } else {
            printf("unknown\n");
        }
#endif

        return 0;
    }

    // Handle specific resource
    struct rlimit rlim;
    if (getrlimit(resource, &rlim) != 0) {
        int saved_errno = errno;
        shell_error_t *error = shell_error_create(
            SHELL_ERR_IO_ERROR, SHELL_SEVERITY_ERROR, SOURCE_LOC_UNKNOWN,
            "ulimit: getrlimit: %s", strerror(saved_errno));
        shell_error_display(error, stderr, isatty(STDERR_FILENO));
        shell_error_free(error);
        return 1;
    }

    if (limit_value == NULL) {
        // Display current limit
        rlim_t current = hard_limit ? rlim.rlim_max : rlim.rlim_cur;
        if (current == RLIM_INFINITY) {
            printf("unlimited\n");
        } else {
            // Convert to appropriate units
            switch (resource) {
            case RLIMIT_FSIZE:
#ifdef RLIMIT_CORE
            case RLIMIT_CORE:
#endif
                printf("%lu\n",
                       (unsigned long)(current / 512)); // 512-byte blocks
                break;
#ifdef RLIMIT_STACK
            case RLIMIT_STACK:
#endif
#ifdef RLIMIT_DATA
            case RLIMIT_DATA:
#endif
#ifdef RLIMIT_AS
            case RLIMIT_AS:
                printf("%lu\n",
                       (unsigned long)(current / 1024)); // 1024-byte blocks
                break;
#endif
            default:
                printf("%lu\n", (unsigned long)current);
                break;
            }
        }
        return 0;
    }

    // Set new limit
    rlim_t new_limit;
    if (strcmp(limit_value, "unlimited") == 0) {
        new_limit = RLIM_INFINITY;
    } else {
        char *endptr;
        long val = strtol(limit_value, &endptr, 10);
        if (*endptr != '\0' || val < 0) {
            fprintf(stderr, "ulimit: %s: invalid limit\n", limit_value);
            return 1;
        }

        // Convert from display units to bytes
        switch (resource) {
        case RLIMIT_FSIZE:
#ifdef RLIMIT_CORE
        case RLIMIT_CORE:
#endif
            new_limit = val * 512; // 512-byte blocks
            break;
#ifdef RLIMIT_STACK
        case RLIMIT_STACK:
#endif
#ifdef RLIMIT_DATA
        case RLIMIT_DATA:
#endif
#ifdef RLIMIT_AS
        case RLIMIT_AS:
            new_limit = val * 1024; // 1024-byte blocks
            break;
#endif
        default:
            new_limit = val;
            break;
        }
    }

    // Set the limit
    if (hard_limit) {
        rlim.rlim_max = new_limit;
        // Can't set hard limit higher than current hard limit without
        // privileges
        if (new_limit > rlim.rlim_max) {
            rlim.rlim_cur = rlim.rlim_max;
        }
    } else {
        rlim.rlim_cur = new_limit;
        // Can't set soft limit higher than hard limit
        if (new_limit > rlim.rlim_max) {
            rlim.rlim_cur = rlim.rlim_max;
        }
    }

    if (setrlimit(resource, &rlim) != 0) {
        int saved_errno = errno;
        shell_error_t *error = shell_error_create(
            SHELL_ERR_IO_ERROR, SHELL_SEVERITY_ERROR, SOURCE_LOC_UNKNOWN,
            "ulimit: setrlimit: %s", strerror(saved_errno));
        shell_error_display(error, stderr, isatty(STDERR_FILENO));
        shell_error_free(error);
        return 1;
    }

    return 0;
}
