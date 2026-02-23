project('lush',
        'c',
        default_options: ['c_std=c11',
                          'optimization=3',
                          'warning_level=2'
                         ],
        version: '1.5.0',
        license: 'MIT')

# Import filesystem module for directory checks
fs = import('fs')

# ============================================================================
# BUILD DIRECTORY ENFORCEMENT
# ============================================================================
# IMPORTANT: Always use 'build' as the build directory name.
# This is the standard convention and enables automatic detection of
# compile_commands.json by editors and tools.
#
# Correct usage:
#   meson setup build
#   meson compile -C build
#   ./build/lush
#
# The check below will error if you use a different build directory name.
# ============================================================================

# Get the build directory name (last component of the path)
build_dir_path = meson.current_build_dir()
build_dir_name = build_dir_path.split('/')[-1]

if build_dir_name != 'build'
    error('Build directory must be named "build", not "' + build_dir_name + '".\n' +
          'Please reconfigure with: meson setup build --wipe\n' +
          'Then build with: meson compile -C build')
endif

message('Build directory: ' + build_dir_name + ' ✓')

# Debug build configuration
debug_option = get_option('enable_debug')
if debug_option
    add_global_arguments('-DDEBUG', language : 'c')
    message('Debug build enabled - DEBUG macro active')
else
    message('Production build - DEBUG macro disabled')
endif

inc = include_directories('include',
                          'include/libhashtable',
                          'include/display',
                          'src')

src = ['src/builtins/alias.c',
       'src/builtins/builtins.c',
       'src/builtins/fc.c',
       'src/compat.c',
       'src/fixer.c',
       'src/config_registry.c',
       'src/posix_history.c',
       'src/arithmetic.c',
       'src/autocorrect.c',
       'src/libfuzzy/fuzzy_match.c',
       'src/config.c',
       'src/lush_memory_pool.c',
       'src/debug/debug_core.c',
       'src/debug/debug_trace.c',
       'src/debug/debug_breakpoints.c',
       'src/debug/debug_profile.c',
       'src/debug/debug_analysis.c',
       'src/display/base_terminal.c',
       'src/display/terminal_control.c',
       'src/display/layer_events.c',
       'src/display/prompt_layer.c',
       'src/display/command_layer.c',
       'src/display/composition_engine.c',
       'src/display/display_controller.c',
       'src/display/screen_buffer.c',
       'src/display/screen_buffer_menu.c',
       'src/display/autosuggestions_layer.c',
       'src/display_integration.c',
       'src/dirstack.c',
       'src/errors.c',
       'src/executor.c',
       'src/expand.c',
       'src/globals.c',
       'src/init.c',
       'src/input.c',
       'src/input_continuation.c',
       'src/redirection.c',
       'src/libhashtable/ht.c',
       'src/libhashtable/ht_fnv1a.c',
       'src/libhashtable/ht_strdouble.c',
       'src/libhashtable/ht_strfloat.c',
       'src/libhashtable/ht_strint.c',
       'src/libhashtable/ht_strstr.c',
       'src/lush.c',
       'src/node.c',
       'src/node_to_source.c',
       'src/shell_error.c',
       'src/opts.c',
       'src/parser.c',
       'src/posix_opts.c',
       'src/shell_mode.c',
       'src/lush_plugin.c',
       'src/signals.c',
       'src/strings.c',
       'src/symtable.c',
       'src/tokenizer.c',
      ]

add_project_arguments('-D_DEFAULT_SOURCE', language: 'c')
add_project_arguments('-D_XOPEN_SOURCE=700', language: 'c')
add_project_arguments('-D_XOPEN_SOURCE_EXTENDED', language: 'c')

# macOS requires _DARWIN_C_SOURCE for BSD-specific functions like pthread_threadid_np
if host_machine.system() == 'darwin'
  add_project_arguments('-D_DARWIN_C_SOURCE', language: 'c')
endif

# LLE is the sole line editor - no GNU readline dependency
message('Line Editor: LLE (Lush Line Editor)')

# Find ncurses dependency (required by LLE terminal capabilities)
ncurses_dep = dependency('ncurses', required: true)
# Some Linux distros (Fedora, RHEL) have ncurses.pc that defines -D_XOPEN_SOURCE=600,
# which conflicts with our project-wide -D_XOPEN_SOURCE=700. We create partial dependencies
# to strip those compile_args, preventing redefinition warnings:
# - ncurses_compile_dep: includes only (for static libraries)
# - ncurses_link_dep: includes + link_args (for executables and declared dependencies)
ncurses_compile_dep = ncurses_dep.partial_dependency(includes: true)
ncurses_link_dep = ncurses_dep.partial_dependency(includes: true, link_args: true)

# Math library (required for fmod in arithmetic/time operations)
# On Linux, libm is separate; on macOS/BSD it's part of libc
cc = meson.get_compiler('c')
libm = cc.find_library('m', required: false)

# ============================================================================
# Fuzzy Matching Library
# ============================================================================
# Build libfuzzy as a static library for fuzzy string matching.
# Used by LLE history search and other components.

fuzzy_lib = static_library('fuzzy',
                           sources: ['src/libfuzzy/fuzzy_match.c'],
                           include_directories: inc)

fuzzy_dep = declare_dependency(link_with: fuzzy_lib,
                               include_directories: inc)

# ============================================================================
# LLE (Lush Line Editor) Build System
# ============================================================================
# Build LLE as a static library (liblle.a) and link into lush executable.
# The src/lle/meson.build file automatically detects implemented modules
# via fs.exists() checks - no manual updates needed when adding modules.

# Include LLE build configuration
subdir('src/lle')

# Build LLE static library if any modules exist
if lle_sources.length() > 0
  message('Building LLE with ' + lle_sources.length().to_string() + ' module(s)')

  lle_lib = static_library('lle',
                           sources: lle_sources,
                           include_directories: inc,
                           c_args: lle_c_args,
                           dependencies: [ncurses_compile_dep, fuzzy_dep])

  lle_dep = declare_dependency(link_with: lle_lib,
                               include_directories: include_directories('include/lle'),
                               dependencies: [ncurses_link_dep])
else
  # LLE not yet implemented, create empty dependency
  message('LLE: No modules implemented yet (zero source files)')
  lle_dep = dependency('', required: false)
endif

# ============================================================================
# Display System Library (for LLE tests that need display system symbols)
# ============================================================================
# LLE display_bridge.c calls functions from the main display system
# (command_layer, prompt_layer, layer_events). Tests that link only against
# liblle.a need this library to resolve those symbols.
#
# This is a MINIMAL library containing only the core display layer components.
# It does NOT include config.c or other shell subsystems to avoid pulling in
# the entire shell dependency tree.

display_sources = [
  'src/display/base_terminal.c',
  'src/display/terminal_control.c',
  'src/display/layer_events.c',
  'src/display/prompt_layer.c',
  'src/display/command_layer.c',
  'src/display/composition_engine.c',
  'src/display/display_controller.c',
  'src/display/screen_buffer.c',
  'src/display/screen_buffer_menu.c',
  'src/display/autosuggestions_layer.c',
  'src/display_integration.c',
  'src/globals.c',
  'src/strings.c',
  'src/errors.c',
]

display_lib = static_library('display',
                             sources: display_sources,
                             include_directories: inc,
                             dependencies: [ncurses_compile_dep])

display_dep = declare_dependency(link_with: display_lib,
                                 include_directories: inc,
                                 dependencies: [ncurses_link_dep])

# Build lush executable with LLE
# NOTE: lle_shell_sources contains shell integration files that depend on
# shell globals (config, builtins, etc.) and cannot be in liblle.a
lush_exe = executable('lush',
                        src + lle_shell_sources,
                        include_directories: inc,
                        dependencies: [lle_dep, libm])

# ============================================================================
# TEST INFRASTRUCTURE
# ============================================================================

# LLE Functional Tests - standalone with mock memory pool
if lle_sources.length() > 0
  # Buffer operations functional tests
  # Uses mock memory pool to avoid full lush dependencies
  test_buffer_ops = executable('test_buffer_operations',
                               ['tests/lle/functional/buffer_operations_test.c',
                                'tests/lle/functional/test_memory_mock.c'],
                               include_directories: inc,
                               dependencies: [lle_dep])

  test('LLE Buffer Operations', test_buffer_ops,
       suite: 'lle-functional',
       timeout: 30)

  # Multiline manager functional tests
  # Tests multiline context and buffer analysis
  # NOTE: Includes hashtable sources because symtable.c depends on them
  test_multiline = executable('test_multiline_manager',
                              ['tests/lle/functional/multiline_manager_test.c',
                               'tests/lle/functional/test_memory_mock.c',
                               'src/input_continuation.c',
                               'src/symtable.c',
                               'src/shell_mode.c',
                               'src/libhashtable/ht.c',
                               'src/libhashtable/ht_fnv1a.c',
                               'src/libhashtable/ht_strstr.c',
                               'src/globals.c'],
                              include_directories: inc,
                              dependencies: [lle_dep])

  test('LLE Multiline Manager', test_multiline,
       suite: 'lle-functional',
       timeout: 30)

  # Subsystem integration tests
  # Tests interaction between multiple LLE subsystems:
  # - Buffer operations + UTF-8 index
  # - Buffer operations + cursor manager
  # - Buffer operations + validator
  # - Buffer operations + change tracker (undo/redo)
  # - End-to-end multi-subsystem scenarios
  test_integration = executable('test_subsystem_integration',
                                ['tests/lle/integration/subsystem_integration_test.c',
                                 'tests/lle/functional/test_memory_mock.c'],
                                include_directories: inc,
                                dependencies: [lle_dep])

  test('LLE Subsystem Integration', test_integration,
       suite: 'lle-integration',
       timeout: 60)

  # Display integration tests
  # Tests the complete integration flow through the display system:
  # - Display Bridge → Event Coordinator → Render Controller
  # - Render Controller → Pipeline → Cache → Output
  # - Terminal Adapter → Theme Integration
  # - Full rendering workflows with multi-component scenarios
  # NOTE: display_integration_test disabled - dirty_tracker removed
  # test_display_integration = executable('test_display_integration',
  #                                       ['tests/lle/integration/display_integration_test.c',
  #                                        'tests/lle/functional/display_test_stubs.c'],
  #                                       include_directories: inc,
  #                                       dependencies: [lle_dep, display_dep])
  #
  # test('LLE Display Integration', test_display_integration,
  #      suite: 'lle-integration',
  #      timeout: 60)

  # Input Parser Integration (Phase 7-9)
  # Tests event generation, keybinding integration, and error recovery
  if fs.exists('tests/lle/integration/input_parser_integration_test.c')
    test_input_parser_integration = executable('test_input_parser_integration',
                                              'tests/lle/integration/input_parser_integration_test.c',
                                              include_directories: inc,
                                              dependencies: [lle_dep])

    test('LLE Input Parser Integration', test_input_parser_integration,
         suite: 'lle-integration',
         timeout: 60)
  endif

  # End-to-end realistic scenario tests
  # Tests complete editing workflows that simulate real-world usage
  test_e2e_scenarios = executable('test_realistic_scenarios',
                                  ['tests/lle/e2e/realistic_scenarios_test.c',
                                   'tests/lle/functional/test_memory_mock.c'],
                                  include_directories: inc,
                                  dependencies: [lle_dep])

  test('LLE Realistic Scenarios', test_e2e_scenarios,
       suite: 'lle-e2e',
       timeout: 60)

  # Performance benchmarks
  # Validates operations meet spec performance requirements
  benchmark_perf = executable('benchmark_performance',
                              ['tests/lle/benchmarks/performance_benchmark.c',
                               'tests/lle/functional/test_memory_mock.c'],
                              include_directories: inc,
                              dependencies: [lle_dep])

  test('LLE Performance Benchmarks', benchmark_perf,
       suite: 'lle-benchmarks',
       timeout: 120)

  # Display integration performance benchmarks
  # Validates display operations meet Spec 08 performance requirements
  # NOTE: benchmark_display disabled - dirty_tracker removed
  # benchmark_display = executable('benchmark_display_performance',
  #                                ['tests/lle/benchmarks/display_performance_benchmark.c',
  #                                 'tests/lle/functional/display_test_stubs.c'],
  #                                include_directories: inc,
  #                                dependencies: [lle_dep, display_dep])
  #
  # test('LLE Display Performance', benchmark_display,
  #      suite: 'lle-benchmarks',
  #      timeout: 120)

  # Display integration stress tests
  # Week 8: Production validation - stress testing under extreme conditions
  # NOTE: stress_display disabled - dirty_tracker removed
  # stress_display = executable('stress_display_test',
  #                             ['tests/lle/stress/display_stress_test.c',
  #                              'tests/lle/functional/display_test_stubs.c'],
  #                             include_directories: inc,
  #                             dependencies: [lle_dep, display_dep])
  #
  # test('LLE Display Stress Tests', stress_display,
  #      suite: 'lle-stress',
  #      timeout: 300)

  # Watchdog and Safety System stress tests
  # Tests freeze detection, recovery mechanisms, and effectiveness metrics
  stress_watchdog = executable('stress_watchdog_test',
                               ['tests/lle/stress/watchdog_stress_test.c'],
                               include_directories: inc,
                               dependencies: [lle_dep, display_dep])

  test('LLE Watchdog Stress Tests', stress_watchdog,
       suite: 'lle-stress',
       timeout: 60)

  # Display Bridge Unit Tests (Spec 08)
  # Unit tests for display bridge initialization, cleanup, and error handling
  # Uses mocks for display_controller and memory pool (standard unit test practice)
  # Integration with real display system will be tested in integration tests
  # Note: test_memory_mock.c not needed - liblle.a provides real implementations
  test_display_bridge = executable('test_display_bridge',
                                   ['tests/lle/unit/test_display_bridge.c',
                                    'tests/lle/functional/display_test_stubs.c'],
                                   include_directories: inc,
                                   dependencies: [lle_dep, display_dep])

  test('LLE Display Bridge', test_display_bridge,
       suite: 'lle-unit',
       timeout: 30)

  # Event System Unit Tests (Spec 04 Phase 1)
  # Tests core event system infrastructure: lifecycle, events, queues, handlers
  test_event_system = executable('test_event_system',
                                 ['tests/lle/unit/test_event_system.c',
                                  'tests/lle/functional/test_memory_mock.c'],
                                 include_directories: inc,
                                 dependencies: [lle_dep])

  test('LLE Event System', test_event_system,
       suite: 'lle-unit',
       timeout: 30)

  # Event System Phase 2 Tests (Spec 04 Phase 2A-2D)
  # Tests filters, timers, enhanced stats, and priority queues
  test_event_phase2 = executable('test_event_phase2',
                                 ['tests/lle/unit/test_event_phase2.c',
                                  'tests/lle/functional/test_memory_mock.c'],
                                 include_directories: inc,
                                 dependencies: [lle_dep])

  test('LLE Event System Phase 2', test_event_phase2,
       suite: 'lle-unit',
       timeout: 30)

  # Terminal Capabilities Unit Tests (Spec 02 Phase 1)
  test_terminal_capabilities = executable('test_terminal_capabilities',
                                          'tests/lle/unit/test_terminal_capabilities.c',
                                          include_directories: inc,
                                          dependencies: [lle_dep])

  test('LLE Terminal Capabilities', test_terminal_capabilities,
       suite: 'lle-unit',
       timeout: 30)

  # Terminal State Management Unit Tests (Spec 02 Phase 2)
  test_terminal_state = executable('test_terminal_state',
                                   'tests/lle/unit/test_terminal_state.c',
                                   include_directories: inc,
                                   dependencies: [lle_dep])

  test('LLE Terminal State', test_terminal_state,
       suite: 'lle-unit',
       timeout: 30)

  # Terminal Event Reading Unit Tests (Spec 02 Phase 3)
  test_terminal_event_reading = executable('test_terminal_event_reading',
                                          'tests/lle/unit/test_terminal_event_reading.c',
                                          include_directories: inc,
                                          dependencies: [lle_dep])

  test('LLE Terminal Event Reading', test_terminal_event_reading,
       suite: 'lle-unit',
       timeout: 30)

  # F-Key Detection Integration Test (Spec 02 + Spec 06)
  # Tests F-key detection with full terminal_abstraction initialization
  # Validates production code path with parser + key_detector
  test_fkey_detection = executable('test_fkey_detection',
                                   'tests/lle/integration/test_fkey_detection.c',
                                   include_directories: inc,
                                   dependencies: [lle_dep])

  test('LLE F-Key Detection Integration', test_fkey_detection,
       suite: 'lle-integration',
       timeout: 30)

  # F-Key Manual Test Program (Spec 02 + Spec 06)
  # Interactive manual test for F-key detection in real terminals
  # Not registered as automated test - for manual verification only
  test_fkey_manual = executable('test_fkey_manual',
                                'tests/lle/manual/test_fkey_manual.c',
                                include_directories: inc,
                                dependencies: [lle_dep])

  # Render Controller Unit Tests (Spec 08)
  # Unit tests for render controller initialization, cleanup, and sub-component setup
  # Tests sub-components: buffer renderer, cursor renderer, frame scheduler, cache, etc.
  test_render_controller = executable('test_render_controller',
                                      ['tests/lle/unit/test_render_controller.c',
                                       'tests/lle/functional/display_test_stubs.c'],
                                      include_directories: inc,
                                      dependencies: [lle_dep, display_dep])

  test('LLE Render Controller', test_render_controller,
       suite: 'lle-unit',
       timeout: 30)



  # Syntax highlighting unit tests (Spec 11)
  test_lle_syntax_highlighting = executable('test_lle_syntax_highlighting',
                                            'tests/lle/unit/test_lle_syntax_highlighting.c',
                                            include_directories: inc,
                                            dependencies: [lle_dep])

  test('LLE Syntax Highlighting', test_lle_syntax_highlighting,
       suite: 'lle-unit',
       timeout: 30)

  # Render pipeline unit tests
  test_render_pipeline = executable('test_render_pipeline',
                                    ['tests/lle/unit/test_render_pipeline.c',
                                     'tests/lle/functional/display_test_stubs.c'],
                                    include_directories: inc,
                                    dependencies: [lle_dep, display_dep])

  test('LLE Render Pipeline', test_render_pipeline,
       suite: 'lle-unit',
       timeout: 30)

  # Hashtable wrapper unit tests (Spec 05)
  test_hashtable = executable('test_hashtable',
                              'tests/lle/unit/test_hashtable.c',
                              include_directories: inc,
                              dependencies: [lle_dep])

  test('LLE Hashtable Wrapper', test_hashtable,
       suite: 'lle-unit',
       timeout: 30)

  # Async Worker Unit Tests (Spec 25 Section 7)
  # Tests async worker thread pool for non-blocking git status
  test_async_worker = executable('test_async_worker',
                                 'tests/lle/unit/test_async_worker.c',
                                 include_directories: inc,
                                 dependencies: [lle_dep])

  test('LLE Async Worker', test_async_worker,
       suite: 'lle-unit',
       timeout: 30)

  # Template Engine Unit Tests (Spec 25 Section 6)
  # Tests template parsing and rendering with segments, conditionals, colors
  test_template_engine = executable('test_template_engine',
                                    'tests/lle/unit/test_template_engine.c',
                                    include_directories: inc,
                                    dependencies: [lle_dep])

  test('LLE Template Engine', test_template_engine,
       suite: 'lle-unit',
       timeout: 30)

  # Prompt Expansion Unit Tests (Spec 28 Phase 1)
  # Tests unified bash/zsh/LLE prompt expansion engine
  test_prompt_expansion = executable('test_prompt_expansion',
                                     'tests/lle/unit/test_prompt_expansion.c',
                                     include_directories: inc,
                                     dependencies: [lle_dep])

  test('LLE Prompt Expansion', test_prompt_expansion,
       suite: 'lle-unit',
       timeout: 30)

  # Segment System Unit Tests (Spec 25 Section 5)
  # Tests segment registry, prompt context, and built-in segments
  test_segment_system = executable('test_segment_system',
                                   'tests/lle/unit/test_segment_system.c',
                                   include_directories: inc,
                                   dependencies: [lle_dep])

  test('LLE Segment System', test_segment_system,
       suite: 'lle-unit',
       timeout: 30)

  # Theme Registry Unit Tests (Spec 25 Section 4)
  # Tests theme registry, theme creation, inheritance, and built-in themes
  test_theme_registry = executable('test_theme_registry',
                                   'tests/lle/unit/test_theme_registry.c',
                                   include_directories: inc,
                                   dependencies: [lle_dep])

  test('LLE Theme Registry', test_theme_registry,
       suite: 'lle-unit',
       timeout: 30)

  # Prompt Composer Unit Tests (Spec 25 - Template/Segment/Theme Integration)
  # Tests the integration layer that connects template engine, segments, and themes
  test_prompt_composer = executable('test_prompt_composer',
                                    ['tests/lle/unit/test_prompt_composer.c',
                                     'tests/lle/functional/display_test_stubs.c'],
                                    include_directories: inc,
                                    dependencies: [lle_dep, display_dep])

  test('LLE Prompt Composer', test_prompt_composer,
       suite: 'lle-unit',
       timeout: 30)

  # Powerline Renderer Unit Tests
  # Tests powerline rendering (colored blocks, arrow separators, composer integration)
  test_powerline_renderer = executable('test_powerline_renderer',
                                       ['tests/lle/unit/test_powerline_renderer.c',
                                        'tests/lle/functional/display_test_stubs.c'],
                                       include_directories: inc,
                                       dependencies: [lle_dep, display_dep])

  test('LLE Powerline Renderer', test_powerline_renderer,
       suite: 'lle-unit',
       timeout: 30)

  # Render cache unit tests
  test_render_cache = executable('test_render_cache',
                                 'tests/lle/unit/test_render_cache.c',
                                 include_directories: inc,
                                 dependencies: [lle_dep])

  test('LLE Render Cache', test_render_cache,
       suite: 'lle-unit',
       timeout: 30)

  # Input Stream Unit Tests (Spec 06 Phase 1)
  # Unit tests for input stream buffering, flow control, and data management
  # Tests: init/destroy, buffer data, consume, peek, statistics, overflow handling
  test_input_stream = executable('test_input_stream',
                                 ['tests/lle/unit/test_input_stream.c',
                                  'tests/lle/functional/test_memory_mock.c'],
                                 include_directories: inc,
                                 dependencies: [lle_dep])

  test('LLE Input Stream', test_input_stream,
       suite: 'lle-unit',
       timeout: 30)

  # Input UTF-8 Processor Unit Tests (Spec 06 Phase 2)
  # Unit tests for streaming UTF-8 decoding and grapheme boundary detection
  # Tests: byte-by-byte processing, buffer processing, partial sequences, error recovery
  test_input_utf8_processor = executable('test_input_utf8_processor',
                                         ['tests/lle/unit/test_input_utf8_processor.c',
                                          'tests/lle/functional/test_memory_mock.c'],
                                         include_directories: inc,
                                         dependencies: [lle_dep])

  test('LLE Input UTF-8 Processor', test_input_utf8_processor,
       suite: 'lle-unit',
       timeout: 30)

  # Sequence Parser Unit Tests (Spec 06 Phase 3)
  # Unit tests for terminal escape sequence parsing (CSI, OSC, DCS, control chars)
  # Tests: state machine, CSI parameters, string sequences, incomplete sequences, error recovery
  test_sequence_parser = executable('test_sequence_parser',
                                    ['tests/lle/unit/test_sequence_parser.c',
                                     'tests/lle/functional/test_memory_mock.c'],
                                    include_directories: inc,
                                    dependencies: [lle_dep])

  test('LLE Sequence Parser', test_sequence_parser,
       suite: 'lle-unit',
       timeout: 30)

  # Key Detector Unit Tests (Spec 06 Phase 4)
  # Unit tests for key sequence detection and mapping
  # Tests: function keys, cursor keys, modifiers, control chars, partial sequences
  test_key_detector = executable('test_key_detector',
                                 ['tests/lle/unit/test_key_detector.c',
                                  'tests/lle/functional/test_memory_mock.c'],
                                 include_directories: inc,
                                 dependencies: [lle_dep])

  test('LLE Key Detector', test_key_detector,
       suite: 'lle-unit',
       timeout: 30)
endif

# Test: Mouse Parser
if fs.exists('tests/lle/unit/test_mouse_parser.c')
  test_mouse_parser = executable('test_mouse_parser',
                                 ['tests/lle/unit/test_mouse_parser.c',
                                  'tests/lle/functional/test_memory_mock.c'],
                                 include_directories: inc,
                                 dependencies: [lle_dep])
  test('LLE Mouse Parser', test_mouse_parser,
       suite: 'lle-unit',
       timeout: 30)
endif

# Test: Parser State Machine
if fs.exists('tests/lle/unit/test_parser_state_machine.c')
  test_parser_state_machine = executable('test_parser_state_machine',
                                         ['tests/lle/unit/test_parser_state_machine.c',
                                          'tests/lle/functional/test_memory_mock.c'],
                                         include_directories: inc,
                                         dependencies: [lle_dep])
  test('LLE Parser State Machine', test_parser_state_machine,
       suite: 'lle-unit',
       timeout: 30)
endif

# ============================================================================
# LLE History System Tests (Spec 09)
# ============================================================================

  # History Core Functional Tests (Phase 1 Day 1)
  # Tests basic history operations: add, get, search, navigate
  if fs.exists('tests/lle/functional/test_history_phase1_day1.c')
    test_history_core = executable('test_history_phase1_day1',
                                   ['tests/lle/functional/test_history_phase1_day1.c',
                                    'tests/lle/functional/test_memory_mock.c'],
                                   include_directories: inc,
                                   dependencies: [lle_dep])
    test('LLE History Core', test_history_core,
         suite: 'lle-functional',
         timeout: 30)
  endif

  # History Indexing Functional Tests (Phase 1 Day 2)
  # Tests command indexing, prefix search, full-text search
  if fs.exists('tests/lle/functional/test_history_phase1_day2.c')
    test_history_indexing = executable('test_history_phase1_day2',
                                       ['tests/lle/functional/test_history_phase1_day2.c',
                                        'tests/lle/functional/test_memory_mock.c'],
                                       include_directories: inc,
                                       dependencies: [lle_dep])
    test('LLE History Indexing', test_history_indexing,
         suite: 'lle-functional',
         timeout: 30)
  endif

  # History Persistence Functional Tests (Phase 1 Day 3)
  # Tests file I/O, format conversion, import/export
  if fs.exists('tests/lle/functional/test_history_phase1_day3.c')
    test_history_persistence = executable('test_history_phase1_day3',
                                          ['tests/lle/functional/test_history_phase1_day3.c',
                                           'tests/lle/functional/test_memory_mock.c'],
                                          include_directories: inc,
                                          dependencies: [lle_dep])
    test('LLE History Persistence', test_history_persistence,
         suite: 'lle-functional',
         timeout: 30)
  endif

  # History Phase 1 Integration Tests
  # Tests integration between core, indexing, and persistence
  if fs.exists('tests/lle/integration/test_history_phase1_integration.c')
    test_history_phase1_integration = executable('test_history_phase1_integration',
                                                 ['tests/lle/integration/test_history_phase1_integration.c',
                                                  'tests/lle/functional/test_memory_mock.c'],
                                                 include_directories: inc,
                                                 dependencies: [lle_dep])
    test('LLE History Phase 1 Integration', test_history_phase1_integration,
         suite: 'lle-integration',
         timeout: 60)
  endif

  # History Phase 3 Day 8 Tests - Search Engine
  # Tests exact, prefix, substring, and fuzzy search functionality
  if fs.exists('tests/lle/functional/test_history_phase3_day8.c')
    test_history_phase3_day8 = executable('test_history_phase3_day8',
                                          ['tests/lle/functional/test_history_phase3_day8.c',
                                           'tests/lle/functional/test_memory_mock.c'],
                                          include_directories: inc,
                                          dependencies: [lle_dep])
    test('LLE History Phase 3 Day 8', test_history_phase3_day8,
         suite: 'lle-functional',
         timeout: 60)
  endif

  # History Phase 3 Day 9 Tests - Interactive Search
  # Tests Ctrl+R style reverse incremental search functionality
  if fs.exists('tests/lle/functional/test_history_phase3_day9.c')
    test_history_phase3_day9 = executable('test_history_phase3_day9',
                                          ['tests/lle/functional/test_history_phase3_day9.c',
                                           'tests/lle/functional/test_memory_mock.c'],
                                          include_directories: inc,
                                          dependencies: [lle_dep])
    test('LLE History Phase 3 Day 9', test_history_phase3_day9,
         suite: 'lle-functional',
         timeout: 60)
  endif

  # History Compliance Tests (Spec 09)
  # Validates compliance with Spec 09 requirements
  if fs.exists('tests/lle/compliance/spec_09_history_compliance.c')
    test_history_compliance = executable('spec_09_compliance',
                                         ['tests/lle/compliance/spec_09_history_compliance.c',
                                          'tests/lle/functional/test_memory_mock.c'],
                                         include_directories: inc,
                                         dependencies: [lle_dep])
    test('Spec 09 History Compliance', test_history_compliance,
         suite: 'lle-compliance',
         timeout: 60)
  endif

  # Completion System Compliance Tests (Spec 12)
  # Validates compliance with Spec 12 requirements
  if fs.exists('tests/lle/compliance/spec_12_completion_compliance.c')
    test_completion_compliance = executable('spec_12_compliance',
                                           ['tests/lle/compliance/spec_12_completion_compliance.c',
                                            'tests/lle/functional/test_memory_mock.c',
                                            'tests/lle/functional/test_completion_mock.c'],
                                           include_directories: inc,
                                           dependencies: [lle_dep])
    test('Spec 12 Completion Compliance', test_completion_compliance,
         suite: 'lle-compliance',
         timeout: 60)
  endif

  # Validates compliance with Spec 22 requirements
  if fs.exists('tests/lle/compliance/spec_22_history_buffer_compliance.c')
    test_spec22_compliance = executable('spec_22_compliance',
                                        ['tests/lle/compliance/spec_22_history_buffer_compliance.c',
                                         'tests/lle/functional/test_memory_mock.c'],
                                        include_directories: inc,
                                        dependencies: [lle_dep])
    test('Spec 22 History-Buffer Integration Compliance', test_spec22_compliance,
         suite: 'lle-compliance',
         timeout: 60)
  endif

  # Validates compliance with Spec 25 requirements
  if fs.exists('tests/lle/compliance/spec_25_keybinding_compliance.c')
    test_spec25_compliance = executable('spec_25_compliance',
                                        'tests/lle/compliance/spec_25_keybinding_compliance.c',
                                        include_directories: inc,
                                        dependencies: [lle_dep])
    test('Spec 25 Keybinding System Compliance', test_spec25_compliance,
         suite: 'lle-compliance',
         timeout: 60)
  endif

  # Spec 25 Section 5 Segment Compliance Test
  # Validates compliance with Spec 25 Section 5 Segment Architecture
  if fs.exists('tests/lle/compliance/spec_25_segment_compliance.c')
    test_spec25_segment_compliance = executable('spec_25_segment_compliance',
                                        'tests/lle/compliance/spec_25_segment_compliance.c',
                                        include_directories: inc,
                                        dependencies: [lle_dep])
    test('Spec 25 Segment System Compliance', test_spec25_segment_compliance,
         suite: 'lle-compliance',
         timeout: 60)
  endif

  # Spec 25 Section 4 Theme Compliance Test
  # Validates compliance with Spec 25 Section 4 Theme Registry
  if fs.exists('tests/lle/compliance/spec_25_theme_compliance.c')
    test_spec25_theme_compliance = executable('spec_25_theme_compliance',
                                        'tests/lle/compliance/spec_25_theme_compliance.c',
                                        include_directories: inc,
                                        dependencies: [lle_dep])
    test('Spec 25 Theme Registry Compliance', test_spec25_theme_compliance,
         suite: 'lle-compliance',
         timeout: 60)
  endif

  # Spec 25 Composer Compliance Test
  # Validates compliance with Spec 25 prompt composer requirements
  if fs.exists('tests/lle/compliance/spec_25_composer_compliance.c')
    test_spec25_composer_compliance = executable('spec_25_composer_compliance',
                                        ['tests/lle/compliance/spec_25_composer_compliance.c',
                                         'tests/lle/functional/display_test_stubs.c'],
                                        include_directories: inc,
                                        dependencies: [lle_dep, display_dep])
    test('Spec 25 Prompt Composer Compliance', test_spec25_composer_compliance,
         suite: 'lle-compliance',
         timeout: 60)
  endif

  # Spec 26 Compliance Test (Adaptive Terminal Integration)
  # Validates compliance with Spec 26 Phases 1-2 requirements
  if fs.exists('tests/lle/compliance/spec_26_adaptive_terminal_compliance.c')
    test_spec26_compliance = executable('spec_26_compliance',
                                        'tests/lle/compliance/spec_26_adaptive_terminal_compliance.c',
                                        include_directories: inc,
                                        dependencies: [lle_dep])
    test('Spec 26 Adaptive Terminal Integration Compliance', test_spec26_compliance,
         suite: 'lle-compliance',
         timeout: 60)
  endif

  # History Phase 4 Complete Tests
  # Tests Phase 4 features: forensics, deduplication, multiline
  # NOTE: Includes input_continuation.c for multiline support
  if fs.exists('tests/lle/functional/test_history_phase4_complete.c')
    test_history_phase4_complete = executable('test_history_phase4_complete',
                                              ['tests/lle/functional/test_history_phase4_complete.c',
                                               'tests/lle/functional/test_memory_mock.c',
                                               'src/input_continuation.c',
                                               'src/symtable.c',
                                               'src/shell_mode.c',
                                               'src/libhashtable/ht.c',
                                               'src/libhashtable/ht_fnv1a.c',
                                               'src/libhashtable/ht_strstr.c',
                                               'src/globals.c'],
                                              include_directories: inc,
                                              dependencies: [lle_dep])
    test('LLE History Phase 4 Complete', test_history_phase4_complete,
         suite: 'lle-functional',
         timeout: 60)
  endif

  # ============================================================================
  # UNICODE CASE AND COMPARISON TESTS
  # ============================================================================

  # Unicode Case Conversion and Comparison Unit Tests
  # Tests unicode_case.h and unicode_compare.h APIs
  if fs.exists('tests/lle/unit/test_unicode_case_compare.c')
    test_unicode_case_compare = executable('test_unicode_case_compare',
                                           'tests/lle/unit/test_unicode_case_compare.c',
                                           include_directories: inc,
                                           dependencies: [lle_dep])
    test('LLE Unicode Case and Compare', test_unicode_case_compare,
         suite: 'lle-unit',
         timeout: 30)
  endif

  # ============================================================================
  # SPEC 25: DEFAULT KEYBINDINGS TESTS
  # ============================================================================

  # Kill Ring Unit Tests
  # Tests GNU Readline compatible kill/yank operations
  if fs.exists('tests/lle/unit/test_kill_ring.c')
    test_kill_ring = executable('test_kill_ring',
                                'tests/lle/unit/test_kill_ring.c',
                                include_directories: inc,
                                dependencies: [lle_dep])
    test('LLE Kill Ring', test_kill_ring,
         suite: 'lle-unit',
         timeout: 30)
  endif

  # Keybinding Engine Unit Tests
  # Tests keybinding manager, key parsing, bind/unbind, mode switching
  if fs.exists('tests/lle/unit/test_keybinding.c')
    test_keybinding = executable('test_keybinding',
                                 ['tests/lle/unit/test_keybinding.c',
                                  'tests/lle/functional/display_test_stubs.c'],
                                 include_directories: inc,
                                 dependencies: [lle_dep, display_dep])
    test('LLE Keybinding Engine', test_keybinding,
         suite: 'lle-unit',
         timeout: 30)
  endif

  # Widget System Unit Tests (Spec 07)
  # Tests widget registry: init, register, lookup, execute, unregister, enable/disable
  if fs.exists('tests/lle/unit/test_widget_system.c')
    test_widget_system = executable('test_widget_system',
                                    ['tests/lle/unit/test_widget_system.c',
                                     'tests/lle/functional/test_memory_mock.c'],
                                    include_directories: inc,
                                    dependencies: [lle_dep])
    test('LLE Widget System', test_widget_system,
         suite: 'lle-unit',
         timeout: 30)
  endif

  # Widget Hooks Manager Unit Tests (Spec 07)
  # Tests hooks manager: register, unregister, trigger, enable/disable, hook types
  if fs.exists('tests/lle/unit/test_widget_hooks.c')
    test_widget_hooks = executable('test_widget_hooks',
                                   ['tests/lle/unit/test_widget_hooks.c',
                                    'tests/lle/functional/test_memory_mock.c'],
                                   include_directories: inc,
                                   dependencies: [lle_dep])
    test('LLE Widget Hooks Manager', test_widget_hooks,
         suite: 'lle-unit',
         timeout: 30)
  endif

  # Completion Types Unit Tests (Spec 12 Phase 1)
  # Tests type classification, item/result management, sorting
  if fs.exists('tests/lle/unit/test_completion_types.c')
    test_completion_types = executable('test_completion_types',
                                       'tests/lle/unit/test_completion_types.c',
                                       include_directories: inc,
                                       link_with: lle_lib,
                                       dependencies: [ncurses_link_dep])
    test('LLE Completion Types', test_completion_types,
         suite: 'lle-unit',
         timeout: 30)
  endif

  # UTF-8 Movement Functions Test
  # Tests cursor_manager integration for UTF-8 character movement
  # Tests: lle_forward_char, lle_backward_char, lle_forward_word, lle_backward_word
  # Coverage: ASCII, 2-byte (Latin), 3-byte (CJK), 4-byte (Emoji), mixed content
  # Uses real LLE library with display_dep for shell symbol resolution
  if fs.exists('tests/lle/test_utf8_movement.c')
    test_utf8_movement = executable('test_utf8_movement',
                                    ['tests/lle/test_utf8_movement.c',
                                     'tests/lle/functional/display_test_stubs.c'],
                                    include_directories: inc,
                                    dependencies: [lle_dep, display_dep])
    test('LLE UTF-8 Movement Functions', test_utf8_movement,
         suite: 'lle-functional',
         timeout: 30)
  endif

  # Adaptive Terminal Detection Unit Tests (Spec 26 Phase 1)
  # Tests detection system, signature matching, capability probing
  if fs.exists('tests/lle/unit/test_adaptive_detection.c')
    test_adaptive_detection = executable('test_adaptive_detection',
                                         'tests/lle/unit/test_adaptive_detection.c',
                                         include_directories: inc,
                                         dependencies: [lle_dep])
    test('LLE Adaptive Detection', test_adaptive_detection,
         suite: 'lle-unit',
         timeout: 30)
  endif

  # Adaptive Terminal Controllers Unit Tests (Spec 26 Phase 2)
  # Tests all four controllers: native, enhanced, minimal, multiplexer
  if fs.exists('tests/lle/unit/test_adaptive_controllers.c')
    test_adaptive_controllers = executable('test_adaptive_controllers',
                                          'tests/lle/unit/test_adaptive_controllers.c',
                                          include_directories: inc,
                                          dependencies: [lle_dep])
    test('LLE Adaptive Controllers', test_adaptive_controllers,
         suite: 'lle-unit',
         timeout: 30)
  endif

  # Adaptive Terminal Fallback Unit Tests (Spec 26 Phase 3)
  # Tests graceful degradation and fallback hierarchy
  if fs.exists('tests/lle/unit/test_adaptive_fallback.c')
    test_adaptive_fallback = executable('test_adaptive_fallback',
                                       'tests/lle/unit/test_adaptive_fallback.c',
                                       include_directories: inc,
                                       dependencies: [lle_dep])
    test('LLE Adaptive Fallback', test_adaptive_fallback,
         suite: 'lle-unit',
         timeout: 30)
  endif

# ============================================================================
# LLE Manual Integration Test (CRITICAL VERIFICATION)
# ============================================================================
# This is NOT an automated test - it's a manual verification tool
# Purpose: Verify raw terminal input works at 11% implementation
# Usage: Run manually and observe behavior
# Exit: User must verify results manually
# NOTE: Standalone executable, no LLE dependencies (tests raw input only)
if fs.exists('tests/lle/integration/manual_input_test.c')
  manual_input_test = executable('manual_input_test',
                                  'tests/lle/integration/manual_input_test.c')
  # NOTE: Not added to automated test suite - must be run manually
endif

# Simple input test - absolutely minimal, no formatting
if fs.exists('tests/lle/integration/simple_input_test.c')
  simple_input_test = executable('simple_input_test',
                                  'tests/lle/integration/simple_input_test.c')
endif

# NOTE: For manual LLE testing, use the main lush binary

# ============================================================================
# LLE Completion Menu Renderer Unit Tests (Spec 12 Phase 5.1)
# Tests menu rendering, column layout, category headers, item formatting
if fs.exists('tests/unit/test_completion_menu_renderer.c')
  test_completion_menu_renderer = executable('test_completion_menu_renderer',
                                             'tests/unit/test_completion_menu_renderer.c',
                                             include_directories: inc,
                                             link_with: lle_lib)
  test('LLE Completion Menu Renderer', test_completion_menu_renderer,
       suite: 'unit',
       timeout: 30)
endif

# ============================================================================
# Shell Mode Unit Tests (Phase 0: Extended Language Support)
# Tests multi-mode architecture, feature matrix, overrides, shebang detection
if fs.exists('tests/unit/test_shell_mode.c')
  test_shell_mode = executable('test_shell_mode',
                               'tests/unit/test_shell_mode.c',
                               'src/shell_mode.c',
                               include_directories: inc)
  test('Shell Mode System', test_shell_mode,
       suite: 'unit',
       timeout: 30)
endif

# ============================================================================
# TOML Parser Unit Tests
# Tests the generic TOML parser used by config and theme systems
if fs.exists('tests/unit/test_toml_parser.c')
  test_toml_parser = executable('test_toml_parser',
                                'tests/unit/test_toml_parser.c',
                                include_directories: inc,
                                dependencies: [lle_dep])
  test('TOML Parser', test_toml_parser,
       suite: 'unit',
       timeout: 30)
endif

# ============================================================================
# Config Registry Unit Tests
# Tests the unified configuration registry
if fs.exists('tests/unit/test_config_registry.c')
  test_config_registry = executable('test_config_registry',
                                    'tests/unit/test_config_registry.c',
                                    'src/config_registry.c',
                                    include_directories: inc,
                                    dependencies: [lle_dep])
  test('Config Registry', test_config_registry,
       suite: 'unit',
       timeout: 30)
endif

# ============================================================================
# Tokenizer Unit Tests
# Tests shell tokenizer: token classification, operators, keywords, redirections
if fs.exists('tests/unit/test_tokenizer.c')
  test_tokenizer = executable('test_tokenizer',
                              'tests/unit/test_tokenizer.c',
                              'src/tokenizer.c',
                              'src/shell_mode.c',
                              include_directories: inc,
                              dependencies: [lle_dep])
  test('Tokenizer', test_tokenizer,
       suite: 'unit',
       timeout: 30)
endif

# ============================================================================
# Parser Unit Tests
# Tests shell parser: commands, pipelines, control structures, redirections
if fs.exists('tests/unit/test_parser.c')
  test_parser = executable('test_parser',
                           'tests/unit/test_parser.c',
                           'src/parser.c',
                           'src/tokenizer.c',
                           'src/node.c',
                           'src/shell_mode.c',
                           'src/shell_error.c',
                           'src/strings.c',
                           'src/symtable.c',
                           'src/libhashtable/ht.c',
                           'src/libhashtable/ht_fnv1a.c',
                           'src/libhashtable/ht_strstr.c',
                           'tests/unit/test_parser_stubs.c',
                           include_directories: inc,
                           dependencies: [lle_dep])
  test('Parser', test_parser,
       suite: 'unit',
       timeout: 30)
endif

# Parser Fuzzer Tests - Edge cases and comprehensive parser coverage
if fs.exists('tests/unit/test_parser_fuzzer.c')
  test_parser_fuzzer = executable('test_parser_fuzzer',
                           'tests/unit/test_parser_fuzzer.c',
                           'src/parser.c',
                           'src/tokenizer.c',
                           'src/node.c',
                           'src/shell_mode.c',
                           'src/shell_error.c',
                           'src/strings.c',
                           'src/symtable.c',
                           'src/libhashtable/ht.c',
                           'src/libhashtable/ht_fnv1a.c',
                           'src/libhashtable/ht_strstr.c',
                           'tests/unit/test_parser_stubs.c',
                           include_directories: inc,
                           dependencies: [lle_dep])
  test('Parser Fuzzer', test_parser_fuzzer,
       suite: 'unit',
       timeout: 60)
endif

# Parser Negative Tests - Comprehensive invalid syntax rejection tests
if fs.exists('tests/unit/test_parser_negative.c')
  test_parser_negative = executable('test_parser_negative',
                           'tests/unit/test_parser_negative.c',
                           'src/parser.c',
                           'src/tokenizer.c',
                           'src/node.c',
                           'src/shell_mode.c',
                           'src/shell_error.c',
                           'src/strings.c',
                           'src/symtable.c',
                           'src/libhashtable/ht.c',
                           'src/libhashtable/ht_fnv1a.c',
                           'src/libhashtable/ht_strstr.c',
                           'tests/unit/test_parser_stubs.c',
                           include_directories: inc,
                           dependencies: [lle_dep])
  test('Parser Negative', test_parser_negative,
       suite: 'unit',
       timeout: 60)
endif

# AST Round-Trip Tests - Verify parse → source → parse produces equivalent AST
if fs.exists('tests/unit/test_ast_roundtrip.c')
  test_ast_roundtrip = executable('test_ast_roundtrip',
                           'tests/unit/test_ast_roundtrip.c',
                           'src/parser.c',
                           'src/tokenizer.c',
                           'src/node.c',
                           'src/node_to_source.c',
                           'src/shell_mode.c',
                           'src/shell_error.c',
                           'src/strings.c',
                           'src/symtable.c',
                           'src/libhashtable/ht.c',
                           'src/libhashtable/ht_fnv1a.c',
                           'src/libhashtable/ht_strstr.c',
                           'tests/unit/test_parser_stubs.c',
                           include_directories: inc,
                           dependencies: [lle_dep])
  test('AST Round-Trip', test_ast_roundtrip,
       suite: 'unit',
       timeout: 60)
endif

# ============================================================================
# Symbol Table Unit Tests
# Tests variable scoping, arrays, namerefs, exports
if fs.exists('tests/unit/test_symtable.c')
  test_symtable = executable('test_symtable',
                             'tests/unit/test_symtable.c',
                             'src/symtable.c',
                             'src/libhashtable/ht.c',
                             'src/libhashtable/ht_fnv1a.c',
                             'src/libhashtable/ht_strstr.c',
                             'tests/unit/test_symtable_stubs.c',
                             include_directories: inc)
  test('Symbol Table', test_symtable,
       suite: 'unit',
       timeout: 30)
endif

# ============================================================================
# AST Node Unit Tests
# Tests node creation, child relationships, tree structure, memory management
if fs.exists('tests/unit/test_node.c')
  test_node = executable('test_node',
                         'tests/unit/test_node.c',
                         'src/node.c',
                         'tests/unit/test_node_stubs.c',
                         include_directories: inc)
  test('AST Node', test_node,
       suite: 'unit',
       timeout: 30)
endif

# ============================================================================
# Executor Integration Tests
# Tests command execution, builtins, control structures, expansion
# Uses all shell sources except lush.c (has main())
if fs.exists('tests/unit/test_executor.c')
  # Filter out lush.c from src list (contains main())
  test_executor_sources = []
  foreach s : src
    if not s.endswith('lush.c')
      test_executor_sources += s
    endif
  endforeach
  test_executor = executable('test_executor',
                             'tests/unit/test_executor.c',
                             'tests/unit/test_executor_stubs.c',
                             test_executor_sources + lle_shell_sources,
                             include_directories: inc,
                             dependencies: [lle_dep, libm])
  test('Executor', test_executor,
       suite: 'unit',
       timeout: 60)
endif

# ============================================================================
# Builtin Command Tests
# Tests individual builtin commands (test, cd, export, echo, etc.)
if fs.exists('tests/unit/test_builtins.c')
  # Reuse same source filtering as executor tests
  test_builtins_sources = []
  foreach s : src
    if not s.endswith('lush.c')
      test_builtins_sources += s
    endif
  endforeach
  test_builtins = executable('test_builtins',
                             'tests/unit/test_builtins.c',
                             'tests/unit/test_executor_stubs.c',
                             test_builtins_sources + lle_shell_sources,
                             include_directories: inc,
                             dependencies: [lle_dep, libm])
  test('Builtins', test_builtins,
       suite: 'unit',
       timeout: 60)
endif

# ============================================================================
# Expansion Tests
# Tests variable expansion, parameter expansion, arithmetic expansion
if fs.exists('tests/unit/test_expansion.c')
  # Reuse same source filtering as executor tests
  test_expansion_sources = []
  foreach s : src
    if not s.endswith('lush.c')
      test_expansion_sources += s
    endif
  endforeach
  test_expansion = executable('test_expansion',
                              'tests/unit/test_expansion.c',
                              'tests/unit/test_executor_stubs.c',
                              test_expansion_sources + lle_shell_sources,
                              include_directories: inc,
                              dependencies: [lle_dep, libm])
  test('Expansion', test_expansion,
       suite: 'unit',
       timeout: 60)
endif

# ============================================================================
# Fuzzy Match Library Tests
# Tests Levenshtein, Damerau-Levenshtein, Jaro-Winkler, subsequence matching
if fs.exists('tests/unit/test_fuzzy_match.c')
  test_fuzzy_sources = []
  foreach s : src
    if not s.endswith('lush.c')
      test_fuzzy_sources += s
    endif
  endforeach
  test_fuzzy_match = executable('test_fuzzy_match',
                                'tests/unit/test_fuzzy_match.c',
                                'tests/unit/test_executor_stubs.c',
                                test_fuzzy_sources + lle_shell_sources,
                                include_directories: inc,
                                dependencies: [lle_dep, libm])
  test('Fuzzy Match', test_fuzzy_match,
       suite: 'unit',
       timeout: 30)
endif

# ============================================================================
# Debug Subsystem Tests
# Tests debugger context, breakpoints, stack frames, profiling, analysis
if fs.exists('tests/unit/test_debug.c')
  test_debug_sources = []
  foreach s : src
    if not s.endswith('lush.c')
      test_debug_sources += s
    endif
  endforeach
  test_debug = executable('test_debug',
                          'tests/unit/test_debug.c',
                          'tests/unit/test_executor_stubs.c',
                          test_debug_sources + lle_shell_sources,
                          include_directories: inc,
                          dependencies: [lle_dep, libm])
  test('Debug', test_debug,
       suite: 'unit',
       timeout: 30)
endif

# ============================================================================
# FC Builtin Tests
# Tests fc command: substitution parsing, range resolution, editing
if fs.exists('tests/unit/test_fc.c')
  test_fc_sources = []
  foreach s : src
    if not s.endswith('lush.c')
      test_fc_sources += s
    endif
  endforeach
  test_fc = executable('test_fc',
                       'tests/unit/test_fc.c',
                       'tests/unit/test_executor_stubs.c',
                       test_fc_sources + lle_shell_sources,
                       include_directories: inc,
                       dependencies: [lle_dep, libm])
  test('FC Builtin', test_fc,
       suite: 'unit',
       timeout: 30)
endif

# ============================================================================
# Display Layer Tests
# Tests command layer, syntax highlighting, layer events, caching
if fs.exists('tests/unit/test_display.c')
  test_display_sources = []
  foreach s : src
    if not s.endswith('lush.c')
      test_display_sources += s
    endif
  endforeach
  test_display = executable('test_display',
                            'tests/unit/test_display.c',
                            'tests/unit/test_executor_stubs.c',
                            test_display_sources + lle_shell_sources,
                            include_directories: inc,
                            dependencies: [lle_dep, libm])
  test('Display', test_display,
       suite: 'unit',
       timeout: 120)
endif

# ============================================================================
# Autocorrect System Tests
# Tests autocorrection initialization, configuration, similarity scoring
if fs.exists('tests/unit/test_autocorrect.c')
  test_autocorrect_sources = []
  foreach s : src
    if not s.endswith('lush.c')
      test_autocorrect_sources += s
    endif
  endforeach
  test_autocorrect = executable('test_autocorrect',
                                'tests/unit/test_autocorrect.c',
                                'tests/unit/test_executor_stubs.c',
                                test_autocorrect_sources + lle_shell_sources,
                                include_directories: inc,
                                dependencies: [lle_dep, libm])
  test('Autocorrect', test_autocorrect,
       suite: 'unit',
       timeout: 30)
endif

# ============================================================================
# Directory Stack Tests
# Tests pushd/popd, directory rotation, stack management
if fs.exists('tests/unit/test_dirstack.c')
  test_dirstack_sources = []
  foreach s : src
    if not s.endswith('lush.c')
      test_dirstack_sources += s
    endif
  endforeach
  test_dirstack = executable('test_dirstack',
                             'tests/unit/test_dirstack.c',
                             'tests/unit/test_executor_stubs.c',
                             test_dirstack_sources + lle_shell_sources,
                             include_directories: inc,
                             dependencies: [lle_dep, libm])
  test('Dirstack', test_dirstack,
       suite: 'unit',
       timeout: 30)
endif

# ============================================================================
# POSIX History Manager Tests
# Tests history creation, entry management, ranges, file operations
if fs.exists('tests/unit/test_posix_history.c')
  test_posix_history_sources = []
  foreach s : src
    if not s.endswith('lush.c')
      test_posix_history_sources += s
    endif
  endforeach
  test_posix_history = executable('test_posix_history',
                                  'tests/unit/test_posix_history.c',
                                  'tests/unit/test_executor_stubs.c',
                                  test_posix_history_sources + lle_shell_sources,
                                  include_directories: inc,
                                  dependencies: [lle_dep, libm])
  test('POSIX History', test_posix_history,
       suite: 'unit',
       timeout: 30)
endif

# ============================================================================
# Debug Analysis Tests
# Tests debug analysis: issue tracking, script analysis, security/performance checks
if fs.exists('tests/unit/test_debug_analysis.c')
  test_debug_analysis_sources = []
  foreach s : src
    if not s.endswith('lush.c')
      test_debug_analysis_sources += s
    endif
  endforeach
  test_debug_analysis = executable('test_debug_analysis',
                                   'tests/unit/test_debug_analysis.c',
                                   'tests/unit/test_executor_stubs.c',
                                   test_debug_analysis_sources + lle_shell_sources,
                                   include_directories: inc,
                                   dependencies: [lle_dep, libm])
  test('Debug Analysis', test_debug_analysis,
       suite: 'unit',
       timeout: 30)
endif

# ============================================================================
# Debug Trace Tests
# Tests debug tracing: node/command/function tracing, stack frames, variable inspection
if fs.exists('tests/unit/test_debug_trace.c')
  test_debug_trace_sources = []
  foreach s : src
    if not s.endswith('lush.c')
      test_debug_trace_sources += s
    endif
  endforeach
  test_debug_trace = executable('test_debug_trace',
                                'tests/unit/test_debug_trace.c',
                                'tests/unit/test_executor_stubs.c',
                                test_debug_trace_sources + lle_shell_sources,
                                include_directories: inc,
                                dependencies: [lle_dep, libm])
  test('Debug Trace', test_debug_trace,
       suite: 'unit',
       timeout: 30)
endif

# ============================================================================
# Debug Breakpoints Tests
# Tests breakpoint management: add/remove, enable/disable, conditions, step execution
if fs.exists('tests/unit/test_debug_breakpoints.c')
  test_debug_breakpoints_sources = []
  foreach s : src
    if not s.endswith('lush.c')
      test_debug_breakpoints_sources += s
    endif
  endforeach
  test_debug_breakpoints = executable('test_debug_breakpoints',
                                      'tests/unit/test_debug_breakpoints.c',
                                      'tests/unit/test_executor_stubs.c',
                                      test_debug_breakpoints_sources + lle_shell_sources,
                                      include_directories: inc,
                                      dependencies: [lle_dep, libm])
  test('Debug Breakpoints', test_debug_breakpoints,
       suite: 'unit',
       timeout: 30)
endif

# ============================================================================
# Display Controller Tests
# Tests display controller: configuration, error handling, completion menu, autosuggestions
if fs.exists('tests/unit/test_display_controller.c')
  test_display_controller_sources = []
  foreach s : src
    if not s.endswith('lush.c')
      test_display_controller_sources += s
    endif
  endforeach
  test_display_controller = executable('test_display_controller',
                                       'tests/unit/test_display_controller.c',
                                       'tests/unit/test_executor_stubs.c',
                                       test_display_controller_sources + lle_shell_sources,
                                       include_directories: inc,
                                       dependencies: [lle_dep, libm])
  test('Display Controller', test_display_controller,
       suite: 'unit',
       timeout: 30)
endif

# ============================================================================
# Terminal Control Tests
# Tests terminal control: color utilities, error handling, capability flags, style flags
if fs.exists('tests/unit/test_terminal_control.c')
  test_terminal_control_sources = []
  foreach s : src
    if not s.endswith('lush.c')
      test_terminal_control_sources += s
    endif
  endforeach
  test_terminal_control = executable('test_terminal_control',
                                     'tests/unit/test_terminal_control.c',
                                     'tests/unit/test_executor_stubs.c',
                                     test_terminal_control_sources + lle_shell_sources,
                                     include_directories: inc,
                                     dependencies: [lle_dep, libm])
  test('Terminal Control', test_terminal_control,
       suite: 'unit',
       timeout: 30)
endif

# Screen Buffer Tests
# ============================================================================
if fs.exists('tests/unit/test_screen_buffer.c')
  test_screen_buffer_sources = []
  foreach s : src
    if not s.endswith('lush.c')
      test_screen_buffer_sources += s
    endif
  endforeach
  test_screen_buffer = executable('test_screen_buffer',
                                  'tests/unit/test_screen_buffer.c',
                                  'tests/unit/test_executor_stubs.c',
                                  test_screen_buffer_sources + lle_shell_sources,
                                  include_directories: inc,
                                  dependencies: [lle_dep, libm])
  test('Screen Buffer', test_screen_buffer,
       suite: 'unit',
       timeout: 30)
endif

# Composition Engine Tests
# ============================================================================
if fs.exists('tests/unit/test_composition_engine.c')
  test_composition_engine_sources = []
  foreach s : src
    if not s.endswith('lush.c')
      test_composition_engine_sources += s
    endif
  endforeach
  test_composition_engine = executable('test_composition_engine',
                                       'tests/unit/test_composition_engine.c',
                                       'tests/unit/test_executor_stubs.c',
                                       test_composition_engine_sources + lle_shell_sources,
                                       include_directories: inc,
                                       dependencies: [lle_dep, libm])
  test('Composition Engine', test_composition_engine,
       suite: 'unit',
       timeout: 30)
endif

# Prompt Layer Tests
# ============================================================================
if fs.exists('tests/unit/test_prompt_layer.c')
  test_prompt_layer_sources = []
  foreach s : src
    if not s.endswith('lush.c')
      test_prompt_layer_sources += s
    endif
  endforeach
  test_prompt_layer = executable('test_prompt_layer',
                                 'tests/unit/test_prompt_layer.c',
                                 'tests/unit/test_executor_stubs.c',
                                 test_prompt_layer_sources + lle_shell_sources,
                                 include_directories: inc,
                                 dependencies: [lle_dep, libm])
  test('Prompt Layer', test_prompt_layer,
       suite: 'unit',
       timeout: 30)
endif

# ============================================================================
# Fixer Module Tests
# Tests auto-fix linter: context lifecycle, fix collection, application, interactive mode
if fs.exists('tests/unit/test_fixer.c')
  test_fixer_sources = []
  foreach s : src
    if not s.endswith('lush.c')
      test_fixer_sources += s
    endif
  endforeach
  test_fixer = executable('test_fixer',
                          'tests/unit/test_fixer.c',
                          'tests/unit/test_executor_stubs.c',
                          test_fixer_sources + lle_shell_sources,
                          include_directories: inc,
                          dependencies: [lle_dep, libm])
  test('Fixer Module', test_fixer,
       suite: 'unit',
       timeout: 30)
endif

# Phase 4: Continuation Prompt Tests
# ============================================================================

# ============================================================================
# FUZZING TARGETS
# ============================================================================
# Fuzz testing for parser and tokenizer to find edge cases and crashes.
#
# Supports two fuzzing engines:
#   1. libFuzzer (Clang only) - fast, integrated sanitizers
#   2. AFL++ (GCC or Clang) - coverage-guided, fork server
#
# Build examples:
#   libFuzzer:
#     CC=clang meson setup build -Denable_fuzzing=true -Dfuzzer=libfuzzer --wipe
#     meson compile -C build fuzz_parser fuzz_tokenizer
#
#   AFL++:
#     CC=afl-clang-fast meson setup build -Denable_fuzzing=true -Dfuzzer=afl --wipe
#     meson compile -C build fuzz_parser_afl fuzz_tokenizer_afl
#
# Run examples:
#   libFuzzer:
#     ./build/fuzz_parser tests/fuzz/corpus/parser/ -max_len=4096
#
#   AFL++:
#     afl-fuzz -i tests/fuzz/corpus/parser/ -o findings/ -- ./build/fuzz_parser_afl
# ============================================================================

fuzzing_enabled = get_option('enable_fuzzing')
fuzzer_type = get_option('fuzzer')

if fuzzing_enabled
  message('Fuzzing enabled with engine: ' + fuzzer_type)

  # Common sources needed for fuzzing (parser + dependencies)
  # Uses fuzz_stubs.c for minimal implementations of executor/LLE functions
  fuzz_common_sources = [
    'src/parser.c',
    'src/tokenizer.c',
    'src/node.c',
    'src/shell_mode.c',
    'src/shell_error.c',
    'src/strings.c',
    'src/globals.c',
    'src/symtable.c',
    'src/libhashtable/ht.c',
    'src/libhashtable/ht_fnv1a.c',
    'src/libhashtable/ht_strstr.c',
    'tests/fuzz/fuzz_stubs.c',
  ]

  if fuzzer_type == 'libfuzzer'
    # ========================================================================
    # libFuzzer targets (Clang only)
    # ========================================================================
    # Requires: CC=clang and -fsanitize=fuzzer
    # libFuzzer provides its own main(), so we just define LLVMFuzzerTestOneInput
    #
    # Installation:
    #   macOS: brew install llvm && use /opt/homebrew/opt/llvm/bin/clang
    #   Linux: apt install clang libfuzzer-X-dev (or install full LLVM)
    #   Fedora: dnf install clang compiler-rt

    # Check that we're using Clang
    if cc.get_id() != 'clang'
      error('libFuzzer requires Clang. Use CC=clang or switch to -Dfuzzer=afl')
    endif

    # Detect LLVM installation path for proper libc++ linking
    # Homebrew installs to /usr/local/opt/llvm (Intel) or /opt/homebrew/opt/llvm (ARM)
    llvm_prefix = ''
    llvm_libcxx_path = ''

    # Try to find LLVM prefix from compiler path
    compiler_path = cc.cmd_array()[0]
    if compiler_path.contains('/llvm/')
      # Extract prefix (e.g., /usr/local/opt/llvm from /usr/local/opt/llvm/bin/clang)
      # Remove /bin/clang* from the end to get the prefix
      if compiler_path.contains('/bin/clang')
        # Find the /bin/ part and take everything before it
        llvm_prefix = compiler_path.split('/bin/')[0]
        llvm_libcxx_path = llvm_prefix / 'lib' / 'c++'
      endif
    endif

    # Check common Homebrew paths if not found from compiler
    if llvm_libcxx_path == ''
      foreach prefix : ['/usr/local/opt/llvm', '/opt/homebrew/opt/llvm']
        test_path = prefix / 'lib' / 'c++'
        if fs.is_dir(test_path)
          llvm_prefix = prefix
          llvm_libcxx_path = test_path
          break
        endif
      endforeach
    endif

    # Build link args - need libc++ path on macOS with Homebrew LLVM
    fuzz_link_args = ['-fsanitize=fuzzer,address,undefined']
    fuzz_rpath = ''
    if host_machine.system() == 'darwin' and llvm_libcxx_path != ''
      fuzz_link_args += ['-L' + llvm_libcxx_path]
      fuzz_rpath = llvm_libcxx_path
      message('Using LLVM libc++ from: ' + llvm_libcxx_path)
    endif

    # Check if libFuzzer runtime is available by testing a link
    fuzzer_test_code = '''
    #include <stdint.h>
    #include <stddef.h>
    int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) { return 0; }
    '''
    has_libfuzzer = cc.links(fuzzer_test_code,
                             args: fuzz_link_args,
                             name: 'libFuzzer runtime')

    if not has_libfuzzer
      error('libFuzzer runtime not found. Options:\n' +
            '  macOS: Install LLVM via Homebrew:\n' +
            '    brew install llvm\n' +
            '    CC=/usr/local/opt/llvm/bin/clang meson setup build -Denable_fuzzing=true\n' +
            '    (Use /opt/homebrew/opt/llvm/bin/clang on Apple Silicon)\n' +
            '  Linux: Install compiler-rt or libfuzzer package:\n' +
            '    Debian/Ubuntu: apt install libfuzzer-14-dev (adjust version)\n' +
            '    Fedora: dnf install compiler-rt\n' +
            '  Alternative: Use AFL++ instead:\n' +
            '    meson setup build -Denable_fuzzing=true -Dfuzzer=afl')
    endif

    # Fuzzer compile args: coverage instrumentation + sanitizers
    fuzz_args = [
      '-fsanitize=fuzzer,address,undefined',
      '-fno-omit-frame-pointer',
      '-g',
    ]

    # Parser fuzzer (libFuzzer)
    fuzz_parser = executable('fuzz_parser',
                             'tests/fuzz/fuzz_parser.c',
                             fuzz_common_sources,
                             include_directories: inc,
                             c_args: fuzz_args,
                             link_args: fuzz_link_args,
                             build_rpath: fuzz_rpath,
                             build_by_default: false)

    # Tokenizer fuzzer (libFuzzer)
    fuzz_tokenizer = executable('fuzz_tokenizer',
                                'tests/fuzz/fuzz_tokenizer.c',
                                fuzz_common_sources,
                                include_directories: inc,
                                c_args: fuzz_args,
                                link_args: fuzz_link_args,
                                build_rpath: fuzz_rpath,
                                build_by_default: false)

    message('libFuzzer targets: fuzz_parser, fuzz_tokenizer')

  elif fuzzer_type == 'afl'
    # ========================================================================
    # AFL++ targets (GCC or Clang)
    # ========================================================================
    # Requires: CC=afl-clang-fast or CC=afl-gcc-fast
    # AFL++ instruments via compiler wrapper, we provide main() via FUZZ_AFL_MAIN

    # AFL compile args: define FUZZ_AFL_MAIN to enable main() wrapper
    afl_args = [
      '-DFUZZ_AFL_MAIN',
      '-g',
    ]

    # Parser fuzzer (AFL++)
    fuzz_parser_afl = executable('fuzz_parser_afl',
                                 'tests/fuzz/fuzz_parser.c',
                                 fuzz_common_sources,
                                 include_directories: inc,
                                 c_args: afl_args,
                                 build_by_default: false)

    # Tokenizer fuzzer (AFL++)
    fuzz_tokenizer_afl = executable('fuzz_tokenizer_afl',
                                    'tests/fuzz/fuzz_tokenizer.c',
                                    fuzz_common_sources,
                                    include_directories: inc,
                                    c_args: afl_args,
                                    build_by_default: false)

    message('AFL++ targets: fuzz_parser_afl, fuzz_tokenizer_afl')
  endif
endif

# ============================================================================
# ADDITIONAL UNIT TESTS FOR COVERAGE IMPROVEMENT
# ============================================================================

# String Utilities Tests
if fs.exists('tests/unit/test_strings.c')
  test_strings_sources = []
  foreach s : src
    if not s.endswith('lush.c')
      test_strings_sources += s
    endif
  endforeach
  test_strings = executable('test_strings',
                            'tests/unit/test_strings.c',
                            'tests/unit/test_executor_stubs.c',
                            test_strings_sources + lle_shell_sources,
                            include_directories: inc,
                            dependencies: [lle_dep, libm])
  test('String Utilities', test_strings,
       suite: 'unit',
       timeout: 30)
endif

# Shell Error System Tests
if fs.exists('tests/unit/test_shell_error.c')
  test_shell_error_sources = []
  foreach s : src
    if not s.endswith('lush.c')
      test_shell_error_sources += s
    endif
  endforeach
  test_shell_error = executable('test_shell_error',
                                'tests/unit/test_shell_error.c',
                                'tests/unit/test_executor_stubs.c',
                                test_shell_error_sources + lle_shell_sources,
                                include_directories: inc,
                                dependencies: [lle_dep, libm])
  test('Shell Error System', test_shell_error,
       suite: 'unit',
       timeout: 30)
endif

# Memory Pool Tests
if fs.exists('tests/unit/test_memory_pool.c')
  test_memory_pool_sources = []
  foreach s : src
    if not s.endswith('lush.c')
      test_memory_pool_sources += s
    endif
  endforeach
  test_memory_pool = executable('test_memory_pool',
                                'tests/unit/test_memory_pool.c',
                                'tests/unit/test_executor_stubs.c',
                                test_memory_pool_sources + lle_shell_sources,
                                include_directories: inc,
                                dependencies: [lle_dep, libm])
  test('Memory Pool', test_memory_pool,
       suite: 'unit',
       timeout: 30)
endif

# Signal Handling Tests
if fs.exists('tests/unit/test_signals.c')
  test_signals_sources = []
  foreach s : src
    if not s.endswith('lush.c')
      test_signals_sources += s
    endif
  endforeach
  test_signals = executable('test_signals',
                            'tests/unit/test_signals.c',
                            'tests/unit/test_executor_stubs.c',
                            test_signals_sources + lle_shell_sources,
                            include_directories: inc,
                            dependencies: [lle_dep, libm])
  test('Signal Handling', test_signals,
       suite: 'unit',
       timeout: 30)
endif

# Alias System Tests
if fs.exists('tests/unit/test_alias.c')
  test_alias_sources = []
  foreach s : src
    if not s.endswith('lush.c')
      test_alias_sources += s
    endif
  endforeach
  test_alias = executable('test_alias',
                          'tests/unit/test_alias.c',
                          'tests/unit/test_executor_stubs.c',
                          test_alias_sources + lle_shell_sources,
                          include_directories: inc,
                          dependencies: [lle_dep, libm])
  test('Alias System', test_alias,
       suite: 'unit',
       timeout: 30)
endif

# Redirection Tests
if fs.exists('tests/unit/test_redirection.c')
  test_redirection_sources = []
  foreach s : src
    if not s.endswith('lush.c')
      test_redirection_sources += s
    endif
  endforeach
  test_redirection = executable('test_redirection',
                                'tests/unit/test_redirection.c',
                                'tests/unit/test_executor_stubs.c',
                                test_redirection_sources + lle_shell_sources,
                                include_directories: inc,
                                dependencies: [lle_dep, libm])
  test('Redirection', test_redirection,
       suite: 'unit',
       timeout: 30)
endif

# Compatibility Database Tests
if fs.exists('tests/unit/test_compat.c')
  test_compat_sources = []
  foreach s : src
    if not s.endswith('lush.c')
      test_compat_sources += s
    endif
  endforeach
  test_compat = executable('test_compat',
                           'tests/unit/test_compat.c',
                           'tests/unit/test_executor_stubs.c',
                           test_compat_sources + lle_shell_sources,
                           include_directories: inc,
                           dependencies: [lle_dep, libm])
  test('Compatibility Database', test_compat,
       suite: 'unit',
       timeout: 30)
endif

# Expand Module Tests
if fs.exists('tests/unit/test_expand.c')
  test_expand_sources = []
  foreach s : src
    if not s.endswith('lush.c')
      test_expand_sources += s
    endif
  endforeach
  test_expand = executable('test_expand',
                           'tests/unit/test_expand.c',
                           'tests/unit/test_executor_stubs.c',
                           test_expand_sources + lle_shell_sources,
                           include_directories: inc,
                           dependencies: [lle_dep, libm])
  test('Expand Module', test_expand,
       suite: 'unit',
       timeout: 30)
endif

# Hash Table Library Tests
if fs.exists('tests/unit/test_hashtable.c')
  test_hashtable = executable('test_hashtable_lib',
                              'tests/unit/test_hashtable.c',
                              'src/libhashtable/ht.c',
                              'src/libhashtable/ht_fnv1a.c',
                              'src/libhashtable/ht_strstr.c',
                              'src/libhashtable/ht_strint.c',
                              'src/libhashtable/ht_strfloat.c',
                              'src/libhashtable/ht_strdouble.c',
                              include_directories: inc,
                              dependencies: [libm])
  test('Hash Table Library', test_hashtable,
       suite: 'unit',
       timeout: 30)
endif

# Debug Core Tests
if fs.exists('tests/unit/test_debug_core.c')
  test_debug_core_sources = []
  foreach s : src
    if not s.endswith('lush.c')
      test_debug_core_sources += s
    endif
  endforeach
  test_debug_core = executable('test_debug_core',
                               'tests/unit/test_debug_core.c',
                               'tests/unit/test_executor_stubs.c',
                               test_debug_core_sources + lle_shell_sources,
                               include_directories: inc,
                               dependencies: [lle_dep, libm])
  test('Debug Core', test_debug_core,
       suite: 'unit',
       timeout: 30)
endif

# Node to Source Tests
if fs.exists('tests/unit/test_node_to_source.c')
  test_node_to_source = executable('test_node_to_source',
                                   'tests/unit/test_node_to_source.c',
                                   'src/node_to_source.c',
                                   'src/parser.c',
                                   'src/tokenizer.c',
                                   'src/node.c',
                                   'src/shell_mode.c',
                                   'src/shell_error.c',
                                   'src/strings.c',
                                   'src/symtable.c',
                                   'src/libhashtable/ht.c',
                                   'src/libhashtable/ht_fnv1a.c',
                                   'src/libhashtable/ht_strstr.c',
                                   'tests/unit/test_parser_stubs.c',
                                   include_directories: inc,
                                   dependencies: [lle_dep])
  test('Node to Source', test_node_to_source,
       suite: 'unit',
       timeout: 60)
endif

# Configuration System Tests
if fs.exists('tests/unit/test_config.c')
  test_config_sources = []
  foreach s : src
    if not s.endswith('lush.c')
      test_config_sources += s
    endif
  endforeach
  test_config = executable('test_config',
                           'tests/unit/test_config.c',
                           'tests/unit/test_executor_stubs.c',
                           test_config_sources + lle_shell_sources,
                           include_directories: inc,
                           dependencies: [lle_dep, libm])
  test('Configuration System', test_config,
       suite: 'unit',
       timeout: 30)
endif

# POSIX Options Tests
if fs.exists('tests/unit/test_posix_opts.c')
  test_posix_opts_sources = []
  foreach s : src
    if not s.endswith('lush.c')
      test_posix_opts_sources += s
    endif
  endforeach
  test_posix_opts = executable('test_posix_opts',
                               'tests/unit/test_posix_opts.c',
                               'tests/unit/test_executor_stubs.c',
                               test_posix_opts_sources + lle_shell_sources,
                               include_directories: inc,
                               dependencies: [lle_dep, libm])
  test('POSIX Options', test_posix_opts,
       suite: 'unit',
       timeout: 30)
endif

# Plugin System Tests
if fs.exists('tests/unit/test_lush_plugin.c')
  test_plugin_sources = []
  foreach s : src
    if not s.endswith('lush.c')
      test_plugin_sources += s
    endif
  endforeach
  test_lush_plugin = executable('test_lush_plugin',
                                'tests/unit/test_lush_plugin.c',
                                'tests/unit/test_executor_stubs.c',
                                test_plugin_sources + lle_shell_sources,
                                include_directories: inc,
                                dependencies: [lle_dep, libm])
  test('Plugin System', test_lush_plugin,
       suite: 'unit',
       timeout: 30)
endif

# Input Continuation Tests
if fs.exists('tests/unit/test_input_continuation.c')
  test_input_cont_sources = []
  foreach s : src
    if not s.endswith('lush.c')
      test_input_cont_sources += s
    endif
  endforeach
  test_input_continuation = executable('test_input_continuation',
                                       'tests/unit/test_input_continuation.c',
                                       'tests/unit/test_executor_stubs.c',
                                       test_input_cont_sources + lle_shell_sources,
                                       include_directories: inc,
                                       dependencies: [lle_dep, libm])
  test('Input Continuation', test_input_continuation,
       suite: 'unit',
       timeout: 30)
endif
