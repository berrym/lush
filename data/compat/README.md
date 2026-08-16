# Shell Compatibility Database

This directory contains TOML files documenting behavioral differences between
POSIX sh, bash, zsh, and lush shells. The data is used by lush's static
analyzer to generate portability warnings.

## Directory Structure

```
data/compat/
+-- builtins/       # Built-in command differences (echo, test, etc.)
+-- expansion/      # Variable/parameter expansion differences
+-- quoting/        # Quoting and escaping differences
+-- syntax/         # Syntax differences (arrays, functions, etc.)
```

## Schema

Each TOML file contains one or more compatibility entries. Each entry documents
a specific behavioral difference between shells.

### Entry Format

```toml
[entry_id]
category = "builtin|expansion|quoting|syntax"
feature = "feature_name"
description = "Human-readable description of the difference"

[entry_id.behavior]
posix = "POSIX behavior description"
bash = "bash behavior description"
zsh = "zsh behavior description"
lush = "lush behavior description"

[entry_id.lint]
severity = "info|warning|error"
message = "Lint message shown to user"
suggestion = "How to write portable code"
pattern = "regex pattern to detect this construct"
```

### Fields

| Field | Required | Description |
|-------|----------|-------------|
| `category` | Yes | Category: builtin, expansion, quoting, syntax |
| `feature` | Yes | Feature name (e.g., "echo", "brace_expansion") |
| `description` | Yes | Human-readable description |
| `behavior.posix` | Yes | POSIX sh behavior |
| `behavior.bash` | Yes | bash behavior |
| `behavior.zsh` | Yes | zsh behavior |
| `behavior.lush` | Yes | lush behavior |
| `lint.severity` | Yes | info, warning, or error |
| `lint.message` | Yes | Message shown in lint output |
| `lint.suggestion` | No | Suggestion for portable alternative |
| `lint.pattern` | No | Regex to detect this construct |

### Severity Levels

- **info**: Informational difference, no action needed
- **warning**: May cause portability issues, review recommended
- **error**: Will definitely fail in target shell

### Example

```toml
[echo_escape_sequences]
category = "builtin"
feature = "echo"
description = "Escape sequence interpretation in echo"

[echo_escape_sequences.behavior]
posix = "Behavior is implementation-defined"
bash = "Requires -e flag for escape sequences"
zsh = "Interprets escape sequences by default (XSI)"
lush = "Interprets escape sequences by default (XSI)"

[echo_escape_sequences.lint]
severity = "warning"
message = "echo escape sequences behave differently across shells"
suggestion = "Use printf for portable escape sequence handling"
pattern = "echo\\s+.*\\\\[nrtabfv]"
```

## Usage

The compatibility database is loaded by `compat_load()` from `src/compat.c`.
The debug analyzer uses this data to check scripts for portability issues.

### Command-Line Options

- `--strict` - Treat compatibility warnings as errors
- `--target=<shell>` - Check compatibility against specific shell (posix, bash, zsh)

## Adding New Entries

1. Identify the appropriate category directory
2. Create or edit the relevant TOML file
3. Add an entry following the schema above
4. Test with `lush --target=posix your_script.sh`

## References

- POSIX Shell Command Language: https://pubs.opengroup.org/onlinepubs/9699919799/utilities/V3_chap02.html
- Bash Reference Manual: https://www.gnu.org/software/bash/manual/
- Zsh Documentation: https://zsh.sourceforge.io/Doc/
