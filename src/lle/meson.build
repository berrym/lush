# src/lle/meson.build
# LLE (Lush Line Editor) Build Configuration
#
# Modular directory structure organized by specification/subsystem.

# ============================================================================
# LLE Compilation Flags
# ============================================================================

# LLE-specific compile args
# Note: _DEFAULT_SOURCE, _XOPEN_SOURCE=700, _XOPEN_SOURCE_EXTENDED are set
# project-wide in the root meson.build via add_project_arguments().
# Only add flags here that are specific to LLE and not already set globally.
lle_c_args = [
  # POSIX.1-2008 for clock_gettime, strdup, strndup, etc.
  '-D_POSIX_C_SOURCE=200809L',
]

lle_sources = []

# ============================================================================
# EXTERNAL DEPENDENCIES
# ============================================================================

# Lush memory pool system
lle_sources += files('../lush_memory_pool.c')

# TOML parser (shared with main config system)
lle_sources += files('../toml_parser.c')

# libhashtable (Spec 05)
libhashtable_root = '../libhashtable'
lle_sources += files(
  libhashtable_root + '/ht.c',
  libhashtable_root + '/ht_fnv1a.c',
  libhashtable_root + '/ht_strdouble.c',
  libhashtable_root + '/ht_strfloat.c',
  libhashtable_root + '/ht_strint.c',
  libhashtable_root + '/ht_strstr.c',
)

# ============================================================================
# CORE MODULE (Spec 14, 15, 16, 17, 25)
# Foundation: Error handling, memory, performance, testing, hashtable, async
# ============================================================================

lle_sources += files(
  'core/error_handling.c',
  'core/memory_management.c',
  'core/arena.c',
  'core/performance.c',
  'core/testing.c',
  'core/hashtable.c',
  'core/async_worker.c',
  'core/git_command.c',
)

# ============================================================================
# UNICODE MODULE (Spec 03 - UTF-8/Grapheme Support)
# ============================================================================

lle_sources += files(
  'unicode/utf8_support.c',
  'unicode/utf8_index.c',
  'unicode/unicode_grapheme.c',
  'unicode/unicode_compare.c',
  'unicode/unicode_case.c',
  'unicode/grapheme_detector.c',
  'unicode/char_width.c',
)

# ============================================================================
# BUFFER MODULE (Spec 03 - Buffer Management)
# ============================================================================

lle_sources += files(
  'buffer/buffer_management.c',
  'buffer/buffer_validator.c',
  'buffer/cursor_manager.c',
  'buffer/change_tracker.c',
)

# ============================================================================
# EVENT MODULE (Spec 04 - Event System)
# ============================================================================

lle_sources += files(
  'event/event_system.c',
  'event/event_queue.c',
  'event/event_handlers.c',
  'event/event_stats.c',
  'event/event_filter.c',
  'event/event_timer.c',
)

# ============================================================================
# TERMINAL MODULE (Spec 02 - Terminal Abstraction)
# ============================================================================

lle_sources += files(
  'terminal/terminal_abstraction.c',
  'terminal/terminal_capabilities.c',
  'terminal/terminal_unix_interface.c',
  'terminal/terminal_internal_state.c',
  'terminal/terminal_display_generator.c',
  'terminal/terminal_input_processor.c',
  'terminal/terminal_error_handler.c',
  'terminal/terminal_perf_monitor.c',
  'terminal/terminal_signature_database.c',
  'terminal/terminal_lush_client.c',
)

# ============================================================================
# ADAPTIVE MODULE (Spec 26 - Adaptive Terminal Integration)
# ============================================================================

lle_sources += files(
  'adaptive/adaptive_terminal_detection.c',
  'adaptive/adaptive_context_initialization.c',
  'adaptive/adaptive_native_controller.c',
  'adaptive/adaptive_display_client_controller.c',
  'adaptive/adaptive_minimal_controller.c',
  'adaptive/adaptive_multiplexer_controller.c',
)

# ============================================================================
# INPUT MODULE (Spec 06 - Input Parsing)
# ============================================================================

lle_sources += files(
  'input/input_stream.c',
  'input/input_utf8_processor.c',
  'input/sequence_parser.c',
  'input/key_detector.c',
  'input/mouse_parser.c',
  'input/parser_state_machine.c',
  'input/input_parser_integration.c',
  'input/input_parser_error_recovery.c',
  'input/input_keybinding_integration.c',
  'input/input_widget_hooks.c',
)

# ============================================================================
# DISPLAY MODULE (Spec 08 - Display Integration)
# ============================================================================

lle_sources += files(
  'display/display_integration.c',
  'display/display_bridge.c',
  'display/render_controller.c',
  'display/render_pipeline.c',
  'display/render_cache.c',
  'display/syntax_highlighting.c',
)

# ============================================================================
# HISTORY MODULE (Spec 09 - History System)
# ============================================================================

lle_sources += files(
  'history/history_core.c',
  'history/history_storage.c',
  'history/history_index.c',
  'history/history_search.c',
  'history/history_interactive_search.c',
  'history/history_expansion.c',
  'history/history_lush_bridge.c',
  'history/history_buffer_integration.c',
  'history/history_buffer_bridge.c',
  'history/history_events.c',
  'history/history_forensics.c',
  'history/history_dedup.c',
  'history/history_multiline.c',
)

# ============================================================================
# MULTILINE MODULE (Spec 17, 22 - Multiline Editing)
# ============================================================================

lle_sources += files(
  'multiline/multiline_parser.c',
  'multiline/multiline_manager.c',
  'multiline/command_structure.c',
  'multiline/structure_analyzer.c',
  'multiline/reconstruction_engine.c',
  'multiline/formatting_engine.c',
  'multiline/edit_session_manager.c',
  'multiline/edit_cache.c',
)

# ============================================================================
# KEYBINDING MODULE (Spec 25 - Default Keybindings)
# ============================================================================

lle_sources += files(
  'keybinding/keybinding.c',
  'keybinding/keybinding_actions.c',
  'keybinding/keybinding_config.c',
  'keybinding/kill_ring.c',
)

# ============================================================================
# WIDGET MODULE (Spec 07 - Extensibility Framework)
# ============================================================================

lle_sources += files(
  'widget/widget_system.c',
  'widget/widget_hooks.c',
  'widget/builtin_widgets.c',
)

# ============================================================================
# COMPLETION MODULE (Spec 12 - Completion System)
# ============================================================================

lle_sources += files(
  'completion/completion_types.c',
  'completion/completion_sources.c',
  'completion/completion_generator.c',
  'completion/completion_menu_state.c',
  'completion/completion_menu_logic.c',
  'completion/completion_menu_renderer.c',
  'completion/context_analyzer.c',
  'completion/source_manager.c',
  'completion/completion_state.c',
  'completion/completion_system.c',
  'completion/custom_source.c',
  'completion/completion_config.c',
  'completion/ssh_hosts.c',
  'completion/builtin_completions.c',
)

# ============================================================================
# PROMPT MODULE (Spec 25 - Prompt/Theme System, Issue #21 Theme Files)
# ============================================================================

lle_sources += files(
  'prompt/template_engine.c',
  'prompt/segment.c',
  'prompt/theme.c',
  'prompt/theme_parser.c',
  'prompt/theme_loader.c',
  'prompt/composer.c',
  'prompt/prompt_expansion.c',
)

# ============================================================================
# SHELL INTEGRATION MODULE (Spec 26 - Initialization System)
# ============================================================================

# Shell event hub and safety utilities - no shell dependencies, included in liblle.a
lle_sources += files(
  'lle_shell_event_hub.c',
  'lle_safety.c',
  'notification.c',
)

# Shell integration init - depends on shell globals (config, etc.) and is
# NOT included in liblle.a. Added to lle_shell_sources which is linked
# only with the main shell executable.
lle_shell_sources = files(
  'lle_shell_integration.c',
  'lle_shell_hooks.c',
)

# ============================================================================
# LLE CORE - Main Entry Points
# ============================================================================

lle_sources += files(
  'lle_editor.c',
  'lle_readline.c',
  'lle_readline_state.c',
  'lle_watchdog.c',
)
