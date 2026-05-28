# Lush Integrated Debugger Guide

**The only shell with built-in interactive debugging capabilities**

**Version**: 1.5.0

---

## Table of Contents

1. [Introduction](#introduction)
2. [Quick Start](#quick-start)
3. [The (lush-debug) Prompt](#the-lush-debug-prompt)
4. [Debug Commands Reference](#debug-commands-reference)
5. [Debugging Workflows](#debugging-workflows)
6. [Variable Inspection](#variable-inspection)
7. [Execution Control](#execution-control)
8. [Performance Profiling](#performance-profiling)
9. [Static Analysis (debug analyze)](#static-analysis-debug-analyze)
10. [Best Practices](#best-practices)
11. [Troubleshooting](#troubleshooting)

---

## Introduction

Lush is the first and only shell to include a complete integrated
debugger. This unique capability lets you set breakpoints, halt and
step at the source-line level, and inspect typed values
(scalar / list / map) interactively, without any external tools.

The debugger is held to a contract by PHILOSOPHY.md section 7: it
keeps pace with the language. A change to the value model, the
scoping discipline, or the executor is not done until the debugger
can still see it. This guide describes what the debugger does today
(Tier 0-2 -- breakpoints, depth-aware stepping, kind-aware variable
inspection, the `type` command, predictive `debug analyze`).

### Why Integrated Debugging Matters

**Traditional shell debugging limitations:**
- Limited to `set -x` tracing with cluttered output
- No variable inspection during execution
- No breakpoints or step-by-step execution
- Difficult to understand complex script flow

**Lush integrated debugger advantages:**
- Interactive `(lush-debug)` prompt with line editing, history,
  reverse-search, and tab completion of the debug command vocabulary
- Source-line-accurate breakpoints with `node->loc.line` precision
- Depth-aware step / step-over / step-out
- Kind-aware variable inspection -- scalar / List / Map labels and
  element counts, not just stringified contents
- `type` / `t` command -- inspect a name's value kind explicitly
- `debug analyze` -- static analysis that predicts SEMANTICS section
  3.9 type-mismatch errors *before* the script runs
- `set -o errtrace` / `set -o functrace` -- inherit ERR / DEBUG /
  RETURN traps into function bodies
- Performance profiling via `debug profile`
- A framed left-gutter UI for debug output so it stays visually
  separate from script output on a shared terminal

### Who Benefits from Integrated Debugging

- **Developers**: Debug complex automation scripts
- **DevOps Engineers**: Troubleshoot deployment and CI/CD scripts
- **System Administrators**: Analyze and fix system scripts
- **Students/Educators**: Learn shell scripting interactively
- **Anyone**: Who writes shell scripts and wants to understand
  their behavior

---

## Quick Start

### Enable Debugging

The debugger is accessed through the built-in `debug` command:

```bash
# Show debugger help
debug help

# Enable debugging
debug on

# Enable with specific verbosity level (0-4)
debug on 2

# Check current debug status
debug

# Disable debugging
debug off
```

### Your First Debugging Session

```bash
# Create a simple test script
cat > first_debug.sh << 'EOF'
#!/usr/bin/env lush
counter=1
echo "Starting loop"
while [ $counter -le 3 ]; do
    echo "Iteration: $counter"
    counter=$((counter + 1))
done
echo "Loop complete"
EOF

# Debug it interactively
debug on
debug vars                    # Show all variables
source first_debug.sh
debug off
```

### Debug Output Example

`debug vars` renders inspection through the screen-buffer framed
gutter, with kind-aware labels for every binding:

```
│ ┌─ [Shell Variables] ───────────────────────────────────────────────────
│ scalar       = "hello"
│ arr          List    (3 elements)
│ m            Map     (2 elements)
│ └──────────────────────────────────────────────────────────────────────
│ ┌─ [System Variables] ──────────────────────────────────────────────────
│ PWD          = "/home/user/project"
│ ?            = "0"
│ $            = "12345"
│ └──────────────────────────────────────────────────────────────────────
```

The `Scalar` / `List` / `Map` labels are derived directly from the
symtable's value-kind tag (SEMANTICS section 7); there is no
stringification of structured values, and no implicit list-to-string
coercion (section 3.4). To dump the *elements* of a list or map, use
`debug print arr` -- the framed inspector renders each entry.

---

## The (lush-debug) Prompt

When execution halts at a breakpoint, lush enters an interactive
break prompt driven by LLE:

```
(lush-debug)
```

This is not `read -p`. It is the same line editor you use at the
interactive shell prompt, with:

- **History recall** -- arrow keys walk previous debug-prompt entries
- **Reverse search** -- Ctrl-R searches debug-prompt history
- **Tab completion** -- completes the (lush-debug) command vocabulary
  (`step`, `next`, `out`, `continue`, `print`, `vars`, `stack`, `type`,
  `watch`, `break`, `quit`, ...) with first-word switching when the
  source manager detects `lle_in_debug_prompt()`
- **Framed-gutter UI** -- responses render with a `│` left gutter so
  debugger output is visually distinct from script output that may
  follow on the same terminal

At the (lush-debug) prompt, in addition to the `debug` subcommands
listed below, two short-form commands are available:

| Prompt command | Equivalent |
|----------------|------------|
| `type <name>` / `t <name>` | Show *just* the kind label (Scalar / List / Map / Func / Nameref) without rendering contents |
| `step` / `s` | `debug step` -- step into |
| `next` / `n` | `debug next` -- step over (depth-aware) |
| `out` | step out of current function frame (depth-aware) |
| `continue` / `c` | `debug continue` |
| `print` / `p` | `debug print` |
| `vars` / `v` | `debug vars` |
| `stack` / `bt` | `debug stack` |
| `quit` / `q` | leave the debug prompt and continue |

The `type` / `t` shorthand resolves a name through the same
scope-chain walk as `debug print`, but only emits the SEMANTICS
section 7 kind label. It's the fastest way to confirm a name's kind
before you write an expression that depends on it.

---

## Debug Commands Reference

### Core Commands

| Command | Description | Example |
|---------|-------------|---------|
| `debug help` | Show complete command reference | `debug help` |
| `debug` | Show current debug status | `debug` |
| `debug on [level]` | Enable debugging (optional level 0-4) | `debug on 2` |
| `debug off` | Disable debugging | `debug off` |

### Debug Levels

| Level | Name | Description |
|-------|------|-------------|
| 0 | None | Debugging disabled |
| 1 | Basic | Basic debugging information |
| 2 | Verbose | Verbose debugging output |
| 3 | Trace | Trace execution with detailed logging |
| 4 | Profile | Full profiling with performance data |

### Variable Inspection

| Command | Description | Example |
|---------|-------------|---------|
| `debug vars` | Show all variables in current scope | `debug vars` |
| `debug print <var>` | Print specific variable value | `debug print PATH` |

### Execution Control

All three stepping commands are **depth-aware** -- they consult the
current call-stack depth and only stop when the executor returns to
the same or shallower depth, so step-over and step-out behave
correctly across nested function calls.

| Command | Description | Example |
|---------|-------------|---------|
| `debug step` | Step *into* the next statement (descends into functions and compound bodies) | `debug step` |
| `debug next` | Step *over* the next statement (executes function calls without descending) | `debug next` |
| `debug continue` | Continue until next breakpoint | `debug continue` |
| `out` (at break prompt) | Step *out* of the current function frame | `out` |

### Function Analysis

| Command | Description | Example |
|---------|-------------|---------|
| `debug functions` | List all defined functions | `debug functions` |
| `debug function <name>` | Show specific function definition | `debug function myFunc` |

### Execution Tracing

| Command | Description | Example |
|---------|-------------|---------|
| `debug trace on` | Enable execution tracing | `debug trace on` |
| `debug trace off` | Disable execution tracing | `debug trace off` |

### Performance Profiling

| Command | Description | Example |
|---------|-------------|---------|
| `debug profile on` | Enable performance profiling | `debug profile on` |
| `debug profile off` | Disable profiling | `debug profile off` |
| `debug profile report` | Show performance report | `debug profile report` |
| `debug profile reset` | Reset profiling data | `debug profile reset` |

### Breakpoints, Stack, Static Analysis

| Command | Description | Example |
|---------|-------------|---------|
| `debug break add <file> <line> [cond]` | Add a breakpoint, optionally guarded by a shell condition | `debug break add script.sh 15` |
| `debug break remove <id>` | Remove breakpoint by id | `debug break remove 1` |
| `debug break list` | List active breakpoints | `debug break list` |
| `debug break clear` | Clear all breakpoints | `debug break clear` |
| `debug stack` | Show the executor call stack with frame depth | `debug stack` |
| `debug analyze <script>` | Static analysis -- predictive type-mismatch, unquoted-expansion, exit-status, and style warnings | `debug analyze myscript.sh` |

Breakpoints are anchored on `node->loc.line` -- the parser-recorded
source line for each AST node -- so they hit at the exact source
location, not at an opaque command ordinal. The line tracker keeps
pace with continued lines (`\`), here-docs, and multi-line compound
statements automatically.

### ERR / DEBUG / RETURN traps

The debugger composes with bash-compatible trap inheritance:

| Option | Behavior |
|--------|----------|
| `set -o errtrace` (`set -E`) | `trap ERR` fires inside functions, command substitutions, and `( ... )` subshells |
| `set -o functrace` (`set -T`) | `trap DEBUG` / `trap RETURN` are inherited into function bodies |

These let a script self-instrument: install an ERR trap that calls
`debug stack` on any non-zero exit, or a DEBUG trap that logs each
command location. Both options are also available via `setopt`
(`err_trace`, `func_trace`) -- they are the same canonical knob (the
polyglot spelling rule of PHILOSOPHY section 2).

---

## Debugging Workflows

### Script Development Workflow

```bash
# 1. Enable debugging during development
debug on 2

# 2. Test your script with verbose output
./myscript.sh

# 3. Inspect variables when needed
debug vars

# 4. Check specific variable values
debug print myvar

# 5. Disable when satisfied
debug off
```

### Troubleshooting Existing Scripts

```bash
# 1. Enable tracing to see execution flow
debug trace on

# 2. Run the problematic script
./problematic_script.sh

# 3. Examine variables at failure point
debug vars

# 4. Analyze specific values
debug print error_var

# 5. Clean up
debug trace off
```

### Learning and Education Workflow

```bash
# 1. Enable verbose debugging
debug on 3

# 2. Step through educational examples
for i in 1 2 3; do
    echo "Processing: $i"
    result=$((i * 2))
    debug print i
    debug print result
done

# 3. Understand variable changes
debug vars
```

---

## Variable Inspection

Variable inspection is **kind-aware**. Lush's value model (SEMANTICS
section 7) distinguishes Scalar, List, and Map as first-class kinds;
the debugger renders them as such rather than coercing everything to
a quoted string.

### debug vars

Shows every binding reachable from the current scope, grouped by
category, with the kind label printed inline for non-scalars:

```
│ ┌─ [Shell Variables] ───────────────────────────────────────────────────
│ counter      = "3"
│ words        List    (5 elements)
│ env_map      Map     (12 elements)
│ greeter      Func
│ alias_ref    Nameref -> counter
│ └──────────────────────────────────────────────────────────────────────
```

The scope-chain walk follows the same rules as variable expansion:
local bindings shadow globals, and `nameref` entries are reported as
`Nameref -> <target>` without dereferencing them.

### debug print <name>

Renders the binding's contents, format chosen by kind:

```
│ ┌─ [Variable: counter] ──────────────────────────────────────────────────
│ Type:  Scalar
│ Value: "3"
│ Length: 1 characters
│ Scope: global
│ └──────────────────────────────────────────────────────────────────────

│ ┌─ [Variable: words] ────────────────────────────────────────────────────
│ Type:  List
│ Count: 5 elements
│ [0] = "alpha"
│ [1] = "beta"
│ [2] = "gamma"
│ [3] = "delta"
│ [4] = "epsilon"
│ Scope: global
│ └──────────────────────────────────────────────────────────────────────
```

Maps render with key=value rows; functions render the location of
the defining `function` form; namerefs render the target name and,
on the next row, the dereferenced binding.

### type / t (at the break prompt only)

The `(lush-debug)` prompt accepts the short-form `type <name>` /
`t <name>` to print **only** the kind label, without rendering
contents. Useful when you want to confirm a binding is, say, a
`List` before writing `${words[@]}` -- one line, no scroll.

Outside the (lush-debug) prompt, the regular `type` builtin retains
its POSIX command-locator behavior (`type ls`, `type cd`); the
typed-inspection sense is debug-prompt-local on purpose.

### Special variables

`debug print ?` shows the last exit status, `debug print $` shows
the shell PID. Positional parameters (`debug print 1`, `debug print
@`) are also resolved.

---

## Execution Control

### Step-by-Step Debugging

```bash
# Enable stepping mode
debug step

# Your script will pause at each command
# Use debug continue to proceed
# Use debug next to step over function calls
```

### Tracing Execution

```bash
# Enable detailed execution tracing
debug trace on

# Run commands to see detailed trace
for file in *.txt; do
    echo "Processing: $file"
done

# Example trace output:
[DEBUG] TRACE: ../src/executor.c:427 - COMMAND: for
[DEBUG] TRACE: ../src/executor.c:427 - COMMAND: echo
Processing: file1.txt
[DEBUG] TRACE: ../src/executor.c:427 - COMMAND: echo
Processing: file2.txt
```

### Conditional Debugging

```bash
# Debug only when specific conditions are met
if [ "$DEBUG_MODE" = "true" ]; then
    debug on 3
fi

# Your script logic here

if [ "$DEBUG_MODE" = "true" ]; then
    debug vars
    debug off
fi
```

---

## Performance Profiling

### Basic Profiling

```bash
# Enable performance profiling
debug profile on

# Run your script
./performance_test.sh

# View performance report
debug profile report

# Clean up
debug profile off
```

### Function-Level Profiling

```bash
# Profile specific functions
debug profile on

slow_function() {
    sleep 1
    echo "Slow operation complete"
}

fast_function() {
    echo "Quick operation"
}

# Call functions
slow_function
fast_function

# Check performance report
debug profile report
```

### Performance Analysis

The profiler provides insights into:
- **Function execution times**: How long each function takes
- **Call counts**: How many times functions are called
- **Resource usage**: Memory and CPU utilization patterns
- **Bottleneck identification**: Which parts of your script are slow

---

## Static Analysis (debug analyze)

`debug analyze <script>` runs the script through lush's static
analyzer without executing it. The analyzer reports findings in
categories:

- **type** -- value-model violations the executor would reject at
  runtime (SEMANTICS section 3.9 list-in-scalar-slot, illegal
  implicit list-to-string coercion, misapplied parameter
  transformations)
- **error** -- syntax errors, unmatched constructs
- **warning** -- likely-but-not-certain problems (unquoted
  expansions, missing exit-status checks, unsafe globs)
- **style** -- POSIX/idiom recommendations
- **portability** -- bash- or zsh-only constructs flagged when running
  with the other profile selected

The unique capability here is the **type** category. The analyzer
walks the AST and, where it can statically infer that a name is bound
to a List or Map, flags expressions that would treat it as a Scalar
*before the script runs*. This is the same engine rule the runtime
enforces (`SHELL_ERR_TYPE_MISMATCH` per SEMANTICS section 3.9); the
analyzer raises it at edit time instead of crash time.

```bash
$ debug analyze script.sh
script.sh:7  type     'arr' is List; cannot expand into scalar slot — use ${arr[@]} or ${arr[*]}
script.sh:12 warning  unquoted "$file" in test condition; quote to handle empty/space values
script.sh:18 style    prefer "$(...)" over backticks
```

The `analyze` and `lint` builtins are the same analyzer wired
directly into the shell (see BUILTIN_COMMANDS.md); `debug analyze` is
the debugger-facing entry point that also annotates the analyzer
output with file:line citations suitable for `debug break add`.

### Stack inspection

```bash
debug stack
```

Shows the executor call stack with one row per frame: function name,
caller location (`script.sh:42`), and frame depth. Frame depth is
what `debug next` and `out` use to decide when to halt -- the same
depth count is visible to you.

---

## Best Practices

### Development Best Practices

1. **Start with basic debugging**
   ```bash
   debug on 1    # Basic level first
   ```

2. **Use appropriate verbosity levels**
   ```bash
   debug on 1    # Development
   debug on 2    # Troubleshooting
   debug on 3    # Deep debugging
   ```

3. **Inspect variables strategically**
   ```bash
   debug vars           # Overview
   debug print key_var  # Specific inspection
   ```

4. **Clean up debugging code**
   ```bash
   debug off    # Always disable when done
   ```

### Production Debugging

1. **Use conditional debugging**
   ```bash
   if [ "$DEBUG" = "1" ]; then
       debug on 1
   fi
   ```

2. **Avoid high verbosity levels in production**
   ```bash
   # Avoid debug on 3 or 4 in production
   debug on 1    # Maximum for production
   ```

3. **Profile performance carefully**
   ```bash
   debug profile on
   # ... critical section ...
   debug profile report
   debug profile off
   ```

### Educational Use

1. **Start with simple examples**
   ```bash
   debug on 2
   echo "Hello, debugging!"
   debug vars
   ```

2. **Demonstrate variable changes**
   ```bash
   debug on 2
   var=1
   debug print var
   var=$((var + 1))
   debug print var
   ```

3. **Show execution flow**
   ```bash
   debug trace on
   if [ -f "file.txt" ]; then
       echo "File exists"
   else
       echo "File not found"
   fi
   debug trace off
   ```

---

## Troubleshooting

### Common Issues

**Debug commands not working:**
```bash
# Ensure you're using Lush
./build/lush
debug help    # Should show debug commands
```

**No debug output visible:**
```bash
# Check if debugging is enabled
debug           # Shows current status
debug on        # Enable if needed
```

**Too much debug output:**
```bash
# Reduce verbosity level
debug level 1   # Less verbose
debug off       # Disable completely
```

### Debug Output Interpretation

**Understanding trace output:**
```bash
[DEBUG] TRACE: ../src/executor.c:427 - COMMAND: echo
```
- **File location**: Where in Lush source the trace originates
- **Line number**: Specific line in source
- **Command**: The shell command being executed

**Variable inspection format:**
```bash
[DEBUG]   VAR      = 'value'
```
- **Variable name**: Left side
- **Value**: Right side in quotes
- **Scope**: Indicated in section headers

### Performance Considerations

**Debug overhead:**
- Level 1-2: Minimal performance impact
- Level 3: Moderate overhead from detailed tracing
- Level 4: Significant overhead from full profiling

**Memory usage:**
- Debug information is stored in memory
- Large scripts may use additional memory for debugging
- Use `debug off` to free debug resources

---

## Integration with Shell Features

### POSIX Options Compatibility

Debugging works seamlessly with all POSIX shell options:

```bash
# Strict error handling with debugging
set -eu
debug on 2
# Your script here
```

### Security Features

```bash
# Debugging in privileged mode
set -o privileged
debug on 1     # Limited debugging in secure mode
```

### Configuration Integration

```bash
# Use config system with debugging
config set autocorrect.enabled true
debug on
# Debugging shows autocorrection in action
```

---

## Example Scenarios

### Debugging a Deployment Script

```bash
#!/usr/bin/env lush

# Enable debugging for troubleshooting
if [ "$DEPLOY_DEBUG" = "1" ]; then
    debug on 2
fi

# Deployment logic
check_prerequisites() {
    debug print "Prerequisites check starting"
    # Check logic here
}

deploy_application() {
    debug vars    # Show all deployment variables
    # Deployment logic here
}

# Main deployment
debug trace on    # Trace critical operations
check_prerequisites
deploy_application
debug trace off

if [ "$DEPLOY_DEBUG" = "1" ]; then
    debug profile report
    debug off
fi
```

### Breakpoint Session With Typed Inspection

```bash
$ cat > demo.sh <<'EOF'
#!/usr/bin/env lush
words=(alpha beta gamma)
counter=0
for w in "${words[@]}"; do
    counter=$((counter + 1))
    echo "$counter: $w"
done
EOF

$ lush
$ debug break add demo.sh 5
Breakpoint 1 set at demo.sh:5
$ source demo.sh
1: alpha
[break] demo.sh:5
(lush-debug) t words
words: List (3 elements)
(lush-debug) print words
│ ┌─ [Variable: words] ────────────────────────────────────────────────────
│ Type:  List
│ Count: 3 elements
│ [0] = "alpha"
│ [1] = "beta"
│ [2] = "gamma"
│ └──────────────────────────────────────────────────────────────────────
(lush-debug) t counter
counter: Scalar
(lush-debug) print counter
counter = "1"
(lush-debug) next         # depth-aware step-over of echo
2: beta
[break] demo.sh:5
(lush-debug) continue
3: gamma
$
```

Two things to notice:

1. **`t` is one keystroke faster than `print`** when you only need to
   confirm a name's kind -- e.g., before writing `${words[@]}` you
   wanted to be sure `words` was a List, not a Scalar that happens to
   contain spaces.
2. **`next` halts at the *same source line*** -- the for-loop body --
   because depth-aware stepping recognizes that the loop iteration is
   at the same executor frame depth, not a deeper call.

### Performance Analysis Example

```bash
#!/usr/bin/env lush

# Performance testing with profiling
debug profile on

efficient_function() {
    echo "Fast operation"
}

slow_function() {
    # Simulate slow operation
    sleep 0.1
    echo "Slower operation"
}

# Test performance
for i in $(seq 1 5); do
    efficient_function
    slow_function
done

debug profile report
debug profile off
```

---

## Conclusion

The lush integrated debugger is bound by PHILOSOPHY.md section 7:
no change to the language is "done" until the debugger can still see
it. That contract is enforced by an integration-test gate
(`tests/unit/test_debug_integration.c` and companions); the debugger
is not a side project that drifts out of sync.

### What's done today (Tier 0-2)

- Line-accurate breakpoints anchored on `node->loc.line`
- LLE-driven `(lush-debug)` break prompt with history, Ctrl-R search,
  and tab completion of the prompt vocabulary
- Depth-aware step / next / out
- Kind-aware `debug vars` and `debug print` (Scalar / List / Map /
  Func / Nameref)
- `type` / `t` shorthand at the break prompt
- `debug analyze` predictive warnings -- catches SEMANTICS section
  3.9 type-mismatches *before* the script runs
- `set -o errtrace` / `set -o functrace` for ERR / DEBUG / RETURN
  trap inheritance
- `debug profile` for function-level timing
- Framed-gutter UI rendered through the screen buffer so debugger
  output is visually distinct from script output

### What's next

The two open obligations the philosophy section 7 rule will impose
work for next:

1. A typed-function form (SEMANTICS section 8) -- when it lands, the
   debugger must render parameter and return kinds.
2. Lexical scope resolution (SEMANTICS section 5.3) -- when it lands,
   `debug vars` must distinguish dynamic from lexical bindings.

Each carries a debugger obligation by the same rule.

---

## See Also

- [PHILOSOPHY.md](PHILOSOPHY.md) section 7 -- the debugger-keeps-pace
  rule and the gate that enforces it
- [SEMANTICS.md](SEMANTICS.md) sections 3.4, 3.9, 7 -- the
  value-kind model the debugger renders
- [BUILTIN_COMMANDS.md](BUILTIN_COMMANDS.md) -- `analyze` and `lint`
  builtins that share the static analyzer
- [CONFIGURATION.md](CONFIGURATION.md) -- `set -o errtrace` /
  `set -o functrace` and the four configuration surfaces