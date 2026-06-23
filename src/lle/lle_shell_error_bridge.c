/**
 * @file lle_shell_error_bridge.c
 * @brief LLE fault sinks bridging to the shell's error and trace channels
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 *
 * Strong implementations of the LLE fault sinks, compiled into the shell
 * executable (the structured-error renderer and the debug trace channel are
 * executable-side, so the liblle router reaches them only through these
 * registered sinks). The user sink turns a fault into a shell error and
 * displays it; the developer sink emits a trace line.
 */

#include "lle/lle_shell_error_bridge.h"

#include "debug.h"
#include "lle/error_handling.h"
#include "lle/lle_fault_shell_map.h"
#include "shell_error.h"

#include <stdio.h>
#include <unistd.h>

/// User channel: render the fault through the shell's structured-error
/// display, the same surface shell errors use. The source location is left
/// unknown on purpose -- an LLE fault originates in editor internals, not in
/// the user's script, so the renderer shows a clean "error[CODE]: message"
/// without pointing at C source. The developer sink carries the C location.
static void lle_fault_user_sink(const lle_fault_t *fault) {
    const char *component = fault->component ? fault->component : "lle";
    const char *detail = fault->detail
                             ? fault->detail
                             : lle_generate_technical_details(fault->code);

    shell_error_t *err =
        shell_error_create(lle_fault_shell_code(fault->code),
                           lle_fault_shell_severity(fault->severity),
                           SOURCE_LOC_UNKNOWN, "%s: %s", component, detail);
    if (!err) {
        return;
    }

    shell_error_display(err, stderr, isatty(STDERR_FILENO) ? true : false);
    shell_error_free(err);
}

/// Developer channel: a trace line with the full C-level context. Silent
/// unless the debug context is at trace level; debug_trace_printf tolerates a
/// NULL context.
static void lle_fault_dev_sink(const lle_fault_t *fault) {
    debug_trace_printf(g_debug_context, "[LLE %s] %s: %s at %s:%d in %s()\n",
                       lle_error_code_to_string(fault->code),
                       fault->component ? fault->component : "?",
                       fault->detail ? fault->detail : "",
                       fault->file ? fault->file : "?", fault->line,
                       fault->function ? fault->function : "?");
}

void lle_shell_error_bridge_init(void) {
    lle_fault_set_user_sink(lle_fault_user_sink);
    lle_fault_set_dev_sink(lle_fault_dev_sink);
}

void lle_shell_error_bridge_shutdown(void) {
    lle_fault_set_user_sink(NULL);
    lle_fault_set_dev_sink(NULL);
}
