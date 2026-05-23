# Changelog

All notable changes to the Lush Shell project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.5.0-prerelease]

### Removed
- `return_value` builtin and the associated `__LUSH_RETURN__` stdout-marker
  scanner inside command substitution. The builtin was an early sketch
  of returning a typed value from a shell function; the typed-function
  form (`fn name(...) -> kind { ... }` with `let result = name(args)`
  capture) supersedes it. Retired before the call/return surface of the
  typed form lands so there is only one path to a structured return value.

### Added

#### Session 125: Hook System and Syntax Highlighting (2026-01-23)
- PROMPT_COMMAND support (bash 5.1+ style) - both string and array forms
- Hook function/array system: `precmd_functions+=()`, `preexec_functions+=()`, `chpwd_functions+=()`, `periodic_functions+=()`
- Simple hook arrays shorthand: `precmd+=()`, `preexec+=()`, `chpwd+=()` (FEATURE_SIMPLE_HOOK_ARRAYS)
- Function name syntax highlighting for all definition forms (ksh, hybrid, POSIX)
- Hook array variables highlighted as special variables
- Function completions - shell functions appear in command-position completions
- Directory completions with autocd - directories appear in empty-line completion when FEATURE_AUTO_CD enabled

### Changed
- License changed from GPL-3.0+ to MIT

### Fixed
- **Issue #70**: History expansion no longer triggers inside quoted strings (e.g., `echo "Hello!"` works correctly)

#### Context-Aware Error Management System
Rust-style structured error reporting with source locations, context chains, and intelligent suggestions:
- Structured error codes (E1000-E1499) organized by category
- Source location tracking (file, line, column) in AST nodes
- Context chains ("while parsing X, in function Y" breadcrumbs)
- "Did you mean?" suggestions with Unicode-aware Damerau-Levenshtein fuzzy matching
- Multi-error collection (parser collects multiple errors before stopping)
- Builtin + PATH suggestions for command-not-found errors

#### Unified Configuration System
- TOML configuration format replacing legacy INI
- XDG Base Directory compliance (`~/.config/lush/lushrc.toml`)
- Config registry with centralized storage and change notification
- Bidirectional sync between runtime commands and config registry
- Legacy migration from `~/.lushrc`
- Two-file config pattern: `lushrc.toml` (declarative) + `lushrc` (power-user escape hatch)

#### New Builtins
- **setopt/unsetopt** - Zsh-style shell option control
- **declare** - Array and integer variable support with `-a`, `-A`, `-i`, `-r`, `-x`, `-p` flags
- **let** - Arithmetic evaluation builtin

#### LLE (Lush Line Editor)
Native line editor replacing GNU Readline:
- 44 Emacs keybinding actions
- Context-aware completions for all builtins
- Real-time syntax highlighting with 45+ token types
- Multi-line editing with natural continuation
- Kill ring and undo/redo support
- Vi mode framework (in development)

#### Multi-Mode Shell Architecture
- POSIX mode (`set -o posix`) - Strict POSIX sh compliance
- Bash mode (`set -o bash`) - Bash 4.x compatibility
- Zsh mode (`set -o zsh`) - Zsh compatibility with glob qualifiers
- Lush mode (default) - All features enabled
- 35 feature flags for fine-grained control
- Shebang detection for automatic mode selection

#### Extended Language Support

**Arrays and Arithmetic**
- Indexed arrays: `arr=(a b c)`, `${arr[0]}`, `${arr[@]}`
- Associative arrays: `declare -A map`
- Arithmetic command: `(( count++ ))`
- Array length: `${#arr[@]}`

**Extended Tests**
- Extended test command: `[[ ]]`
- Pattern matching: `[[ $str == *.txt ]]`
- Regex matching: `[[ $str =~ pattern ]]` with BASH_REMATCH array
- Logical operators with short-circuit evaluation

**Process Substitution**
- Input substitution: `<(command)`
- Output substitution: `>(command)`

**Extended Parameter Expansion**
- Case modification: `${var^^}`, `${var,,}`
- Pattern substitution: `${var//old/new}`
- Indirect expansion: `${!var}`
- Transformations: `${var@Q}`

**Extended Globbing**
- Extended patterns: `?(pat)`, `*(pat)`, `+(pat)`, `!(pat)`

**Control Flow Extensions**
- Case fall-through: `;&`, `;;&`
- `select` loop
- `time` keyword

**Functions and Hooks**
- Nameref variables: `local -n ref=$1`
- Anonymous functions: `() { cmd; }`
- Glob qualifiers: `*(.)`, `*(/)`, `*(@)`, `*(*)`
- Hook functions: `precmd`, `preexec`, `chpwd`, `periodic`
- Hook arrays: `precmd_functions+=()`, `preexec_functions+=()`, etc.
- PROMPT_COMMAND array form (bash 5.1+ style)
- Plugin system foundation

#### Syntax Highlighting Enhancements
- Function name highlighting in all definition forms (ksh, hybrid, POSIX)
- Hook array variables highlighted as special variables
- Weak/strong symbol pattern for shell-dependent LLE features

### Changed
- Configuration file location now XDG-compliant
- All ~30 parser errors migrated to structured error system
- Executor errors include source location and context
- Builtin errors use structured error display
- Removed GNU Readline dependency (LLE is native)

### Fixed
- History expansion no longer triggers inside quoted strings (Issue #70)
- Feature persistence gap resolved via config registry
- Display syntax toggle works at runtime
- Parameter shadowing in display APIs
- Zero memory leaks (verified with macOS leaks tool)

---

## [1.3.1] - 2025-10-01

### Added
- Complete shell options integration with config system (24 POSIX options)
- Standardized dot notation configuration naming
- Backward compatibility for legacy underscore config names
- Enterprise-grade configuration documentation

### Changed
- All config options migrated to namespace.option format
- Documentation updated to reflect 1.3.0-dev feature set

---

## [1.3.0] - 2025-09-30

### Added

#### POSIX Shell Options (24 options)
- Core execution: `-e` (errexit), `-x` (xtrace), `-n` (noexec), `-u` (nounset), `-v` (verbose)
- File system: `-f` (noglob), `-h` (hashall), `-C` (noclobber), `-a` (allexport)
- Job control: `-m` (monitor), `-b` (notify), `-t` (onecmd)
- Interactive: `ignoreeof`, `nolog`, `emacs`, `vi`, `posix`
- Advanced: `pipefail`, `histexpand`, `history`, `interactive-comments`, `braceexpand`
- Security: `physical`, `privileged`

#### Physical Path Navigation
- Logical mode (default): Preserves symlinked paths
- Physical mode: Resolves all symbolic links
- PWD/OLDPWD management with symtable integration

#### Privileged/Restricted Shell Mode
- Command execution restrictions (block paths with `/`)
- Built-in command controls
- Environment variable protection (PATH, IFS, ENV, SHELL)
- File system access controls

#### Enhanced Builtins
- **read**: `-p` (prompt), `-r` (raw mode)
- **test**: `!` (negation), `-a` (AND), `-o` (OR)
- **type**: `-t` (type only), `-p` (path only), `-a` (show all)
- **printf**: Dynamic field width (`%*s`, `%.*s`)

#### Redirection Features
- Clobber override syntax: `>|`
- Pipeline failure detection: `-o pipefail`

---

## [1.2.5] - 2025-09-16

### Fixed
- Multiline parser issues (functions, case statements via stdin/pipe)
- Here document support (`<<` and `<<-` with variable expansion)
- Function persistence across commands
- Context tracking for compound commands

### Added
- Enhanced function parameter system with default values
- Advanced return values via `return_value "string"`
- Function introspection (`debug functions`, `debug function <name>`)

### Improved
- Shell compliance: 85% (134/136 tests passing)
- Test success rate: 98.5%

---

## [1.2.4] - 2025-09-15

### Changed
- Complete documentation overhaul with tested examples
- All function syntax examples verified working

---

## [1.2.2] - 2025-09-11

### Added
- Advanced Scripting Guide (1000+ lines)
- Configuration Mastery Guide (800+ lines)

### Changed
- Repository cleanup (removed legacy test scripts and outdated docs)
- All files updated to GPL-3.0+ license
- Correct copyright attribution to Michael Berry

### Fixed
- License consistency across all files
- Version consistency (all references to 1.2.2)
- Theme authorship attribution

---

## [1.2.1] - 2025-01-16

### Added
- Fish-like autosuggestions based on history
- Robust syntax highlighting with line wrapping support
- Terminal dimension detection
- Multi-line display management

### Fixed
- Autosuggestion clearing on continuation prompts
- History navigation artifacts
- UP/DOWN arrow key binding issues

---

## [1.2.0] - 2025-01-10

### Added
- Real-time syntax highlighting
- String literal highlighting (single and double quotes)
- Variable highlighting (`$VAR`, `${VAR}`)
- Number and operator highlighting
- Enhanced display mode (`--enhanced-display`)

### Changed
- Removed artificial length limits on syntax highlighting
- Enhanced readline integration

---

## [1.1.3] - 2025-01-10

### Added
- Display integration system with layered architecture
- Git integration for real-time branch/status display
- 6 professional themes
- Advanced context-aware tab completion

---

## [1.1.2] - 2024-12-15

### Added
- Git-aware tab completion
- SSH host completion
- Directory-only completion for cd

---

## [1.1.1] - 2024-12-01

### Added
- Professional theme system
- Git branch integration in prompts
- Dynamic theme switching

---

## [1.1.0] - 2024-11-15

### Added
- GNU Readline integration with history
- Persistent command history with deduplication
- Standard shell key bindings

---

## [1.0.0] - 2024-10-01

### Added
- Initial stable release
- POSIX compliance
- Essential built-in commands
- Multiline support (if, for, while, case)
- Job control
- Variable management
- I/O redirection and pipes
- Signal handling
- Meson build system

---

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
