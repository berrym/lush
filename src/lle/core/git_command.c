/**
 * @file git_command.c
 * @brief Portable timed git command execution
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Implements timeout-safe command execution using POSIX primitives:
 * fork(), execl(), pipe(), select(), waitpid(), kill().
 *
 * All functions are safe to call from any thread (no global state modified).
 * Child processes are always reaped to prevent zombies.
 */

#include "lle/git_command.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <unistd.h>

/**
 * @brief Read all available data from fd into buffer with EINTR safety
 *
 * @param fd       File descriptor to read from
 * @param buf      Output buffer
 * @param buf_size Size of output buffer
 * @return Number of bytes read, or -1 on error
 */
static ssize_t read_all_available(int fd, char *buf, size_t buf_size) {
    if (!buf || buf_size == 0) {
        /* Drain fd without storing */
        char drain[256];
        ssize_t total = 0;
        for (;;) {
            ssize_t n = read(fd, drain, sizeof(drain));
            if (n > 0) {
                total += n;
            } else if (n == 0) {
                break; /* EOF */
            } else if (errno == EINTR) {
                continue;
            } else {
                break; /* Error */
            }
        }
        return total;
    }

    size_t pos = 0;
    for (;;) {
        size_t remaining = buf_size - 1 - pos; /* Reserve space for NUL */
        if (remaining == 0) {
            /* Buffer full - drain remaining to prevent child from blocking */
            char drain[256];
            for (;;) {
                ssize_t n = read(fd, drain, sizeof(drain));
                if (n <= 0 && errno != EINTR)
                    break;
            }
            break;
        }

        ssize_t n = read(fd, buf + pos, remaining);
        if (n > 0) {
            pos += (size_t)n;
        } else if (n == 0) {
            break; /* EOF */
        } else if (errno == EINTR) {
            continue;
        } else {
            break; /* Error */
        }
    }

    buf[pos] = '\0';
    return (ssize_t)pos;
}

/**
 * @brief Kill child process with escalation: SIGTERM -> SIGKILL
 *
 * @param pid Child process ID
 */
static void kill_child(pid_t pid) {
    kill(pid, SIGTERM);

    /* Give child 100ms to exit gracefully */
    struct timeval tv = {.tv_sec = 0, .tv_usec = 100000};
    select(0, NULL, NULL, NULL, &tv);

    int status;
    pid_t result = waitpid(pid, &status, WNOHANG);
    if (result == 0) {
        /* Still running - force kill */
        kill(pid, SIGKILL);
        waitpid(pid, &status, 0);
    }
}

git_cmd_result_t git_command_with_timeout(const char *cmd, char *output,
                                          size_t output_size,
                                          uint32_t timeout_ms) {
    git_cmd_result_t result = {.exit_status = -1, .timed_out = false};

    if (!cmd) {
        return result;
    }

    if (timeout_ms == 0) {
        timeout_ms = GIT_CMD_SYNC_TIMEOUT_MS;
    }

    if (output && output_size > 0) {
        output[0] = '\0';
    }

    /* Create pipe for capturing child stdout */
    int pipefd[2];
    if (pipe(pipefd) < 0) {
        return result;
    }

    pid_t pid = fork();
    if (pid < 0) {
        /* Fork failed */
        close(pipefd[0]);
        close(pipefd[1]);
        return result;
    }

    if (pid == 0) {
        /* Child process */
        close(pipefd[0]); /* Close read end */

        /* Redirect stdout to pipe */
        if (dup2(pipefd[1], STDOUT_FILENO) < 0) {
            _exit(127);
        }
        close(pipefd[1]);

        /* Redirect stderr to /dev/null */
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }

        execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
        _exit(127); /* exec failed */
    }

    /* Parent process */
    close(pipefd[1]); /* Close write end */

    /* Use select() with timeout to wait for child output */
    struct timeval tv;
    tv.tv_sec = (long)(timeout_ms / 1000);
    tv.tv_usec = (long)((timeout_ms % 1000) * 1000);

    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(pipefd[0], &readfds);

    int ready = select(pipefd[0] + 1, &readfds, NULL, NULL, &tv);

    if (ready < 0 && errno != EINTR) {
        /* select error */
        close(pipefd[0]);
        kill_child(pid);
        return result;
    }

    if (ready == 0) {
        /* Timeout - kill child */
        close(pipefd[0]);
        kill_child(pid);
        result.timed_out = true;
        return result;
    }

    /* Data available or EINTR (which means a signal arrived, proceed) */
    read_all_available(pipefd[0], output, output_size);
    close(pipefd[0]);

    /* Remove trailing newline from output */
    if (output && output_size > 0) {
        size_t len = strlen(output);
        while (len > 0 && (output[len - 1] == '\n' || output[len - 1] == '\r')) {
            output[--len] = '\0';
        }
    }

    /* Wait for child to exit */
    int status;
    pid_t waited;
    do {
        waited = waitpid(pid, &status, 0);
    } while (waited < 0 && errno == EINTR);

    if (waited > 0 && WIFEXITED(status)) {
        result.exit_status = WEXITSTATUS(status);
    }

    return result;
}

git_cmd_result_t git_command_in_dir(const char *dir, const char *args,
                                    char *output, size_t output_size,
                                    uint32_t timeout_ms) {
    if (!dir || !args) {
        git_cmd_result_t result = {.exit_status = -1, .timed_out = false};
        return result;
    }

    char cmd[1024];
    int n = snprintf(cmd, sizeof(cmd), "git -C '%s' %s 2>/dev/null", dir, args);
    if (n < 0 || (size_t)n >= sizeof(cmd)) {
        git_cmd_result_t result = {.exit_status = -1, .timed_out = false};
        return result;
    }

    return git_command_with_timeout(cmd, output, output_size, timeout_ms);
}
