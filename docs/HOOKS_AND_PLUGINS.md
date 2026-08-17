# Hooks and Plugins

**Extending Lush with hook functions and the plugin system**

---

## Table of Contents

1. [Overview](#overview)
2. [Hook Functions](#hook-functions)
3. [Hook Arrays](#hook-arrays)
4. [Common Use Cases](#common-use-cases)
5. [Plugin System](#plugin-system)
6. [Best Practices](#best-practices)

---

## Overview

Lush provides a hook system that lets you execute custom code at specific points in the shell's operation. This enables customization of shell behavior, integration with external tools, and creation of advanced workflows.

### Available Hooks

| Hook | Trigger |
|------|---------|
| `precmd` | After command completes, before prompt displays |
| `preexec` | After command entered, before execution |
| `chpwd` | After current directory changes |
| `periodic` | At regular intervals (controlled by `PERIOD`) |

### Hook System Features

- Zsh-compatible hook function syntax
- Hook arrays for multiple handlers
- Automatic error handling
- Performance monitoring integration

---

## Hook Functions

### precmd

The `precmd` function runs after each command completes, just before the prompt is displayed.

```bash
precmd() {
    # Update terminal title
    echo -ne "\033]0;${USER}@${HOSTNAME}: ${PWD}\007"
}
```

**Use cases:**
- Update terminal title
- Refresh prompt information
- Log command completion
- Update status displays

**Timing:** Runs after the previous command's exit status is set but before the prompt renders.

### preexec

The `preexec` function runs after a command is entered but before it executes. The command text is passed as an argument.

```bash
preexec() {
    local cmd="$1"
    echo "About to run: $cmd"
    
    # Log to file
    echo "$(date '+%Y-%m-%d %H:%M:%S') $cmd" >> ~/.command_log
}
```

**Use cases:**
- Command logging/auditing
- Command timing (start timer)
- Send notifications
- Modify environment before execution

**Arguments:**
- `$1`: The command line that will be executed

### chpwd

The `chpwd` function runs whenever the current directory changes (via `cd`, `pushd`, `popd`, etc.).

```bash
chpwd() {
    # Auto-ls on directory change
    ls
    
    # Or show directory info
    echo "Now in: $PWD"
    
    # Check for project files
    if [ -f "package.json" ]; then
        echo "Node.js project detected"
    fi
}
```

**Use cases:**
- Auto-list directory contents
- Activate/deactivate virtual environments
- Load project-specific configuration
- Update prompt with project info

**Note:** Runs for any directory change, including those in scripts.

### periodic

The `periodic` function runs at regular intervals, controlled by the `PERIOD` variable (in seconds).

```bash
# Run every 60 seconds
PERIOD=60

periodic() {
    # Check for new mail
    if [ -s "$MAIL" ]; then
        echo "You have mail"
    fi
    
    # Or check system status
    # ...
}
```

**Use cases:**
- Check for new mail
- Monitor system resources
- Fetch remote status updates
- Background sync operations

**Timing:** Checked during `precmd`. If `PERIOD` seconds have elapsed since the last call, `periodic` runs.

---

## Hook Arrays

For cases where you need multiple handlers for the same event, use hook arrays. This is useful when combining multiple plugins or configurations.

### Array Names

| Hook | Array |
|------|-------|
| `precmd` | `precmd_functions` |
| `preexec` | `preexec_functions` |
| `chpwd` | `chpwd_functions` |
| `periodic` | `periodic_functions` |

### Using Hook Arrays

```bash
# Define handler functions
update_title() {
    echo -ne "\033]0;${PWD}\007"
}

log_prompt() {
    logger -t shell "prompt displayed in $PWD"
}

check_todos() {
    if [ -f "TODO.md" ]; then
        echo "TODO.md found"
    fi
}

# Add to precmd array
precmd_functions+=(update_title)
precmd_functions+=(log_prompt)
precmd_functions+=(check_todos)

# All three run before each prompt
```

### Execution Order

1. Named function (`precmd`, `preexec`, etc.) runs first
2. Array functions run in order

```bash
precmd() {
    echo "Main precmd"  # Runs first
}

precmd_functions=(handler1 handler2)
# handler1 runs second, handler2 runs third
```

### Removing Handlers

```bash
# Remove specific handler
precmd_functions=(${precmd_functions[@]/handler1})

# Clear all handlers
precmd_functions=()
```

### Checking Handlers

```bash
# List current handlers
echo "precmd handlers: ${precmd_functions[@]}"

# Check if handler exists
if [[ " ${precmd_functions[@]} " =~ " handler1 " ]]; then
    echo "handler1 is registered"
fi
```

---

## Common Use Cases

### Command Timing

```bash
# Start timer in preexec, report in precmd
_cmd_start=0

preexec() {
    _cmd_start=$(date +%s)
}

precmd() {
    if [ $_cmd_start -gt 0 ]; then
        local elapsed=$(($(date +%s) - _cmd_start))
        if [ $elapsed -gt 5 ]; then
            echo "Command took ${elapsed}s"
        fi
        _cmd_start=0
    fi
}
```

### Terminal Title

```bash
precmd() {
    # Format: user@host: directory
    echo -ne "\033]0;${USER}@${HOSTNAME%%.*}: ${PWD/#$HOME/~}\007"
}

preexec() {
    # Show running command in title
    echo -ne "\033]0;$1\007"
}
```

### Virtual Environment Activation

```bash
chpwd() {
    # Python virtual environment
    if [ -f "venv/bin/activate" ]; then
        source venv/bin/activate
    elif [ -n "$VIRTUAL_ENV" ]; then
        deactivate
    fi
    
    # Node.js version management
    if [ -f ".nvmrc" ]; then
        nvm use 2>/dev/null
    fi
}
```

### Project Detection

```bash
chpwd() {
    # Git repository info
    if [ -d ".git" ]; then
        echo "Git repo: $(git config --get remote.origin.url 2>/dev/null || echo 'local')"
    fi
    
    # Project type
    if [ -f "Cargo.toml" ]; then
        echo "Rust project"
    elif [ -f "go.mod" ]; then
        echo "Go project"
    elif [ -f "package.json" ]; then
        echo "Node.js project"
    elif [ -f "Makefile" ]; then
        echo "Make project"
    fi
}
```

### Command Logging

```bash
preexec() {
    # Log all commands with timestamp
    local log_file="$HOME/.shell_history_log"
    echo "$(date '+%Y-%m-%d %H:%M:%S') [$$] $1" >> "$log_file"
}
```

### System Monitoring

```bash
PERIOD=300  # Every 5 minutes

periodic() {
    # Check disk space
    local usage=$(df -h / | awk 'NR==2 {print $5}' | tr -d '%')
    if [ "$usage" -gt 90 ]; then
        echo "WARNING: Disk usage at ${usage}%"
    fi
    
    # Check memory
    # ...
}
```

### Desktop Notifications

```bash
preexec() {
    _last_cmd="$1"
    _cmd_start=$(date +%s)
}

precmd() {
    local elapsed=$(($(date +%s) - _cmd_start))
    
    # Notify for long-running commands
    if [ $elapsed -gt 30 ]; then
        # macOS
        osascript -e "display notification \"Completed in ${elapsed}s\" with title \"$_last_cmd\""
        
        # Or Linux with notify-send
        # notify-send "$_last_cmd" "Completed in ${elapsed}s"
    fi
}
```

---

## Extending the Shell (without a Plugin Loader)

Lush does **not yet** ship a dedicated plugin loader / manager.
That work is tracked under "User extensibility / plugins -- Not yet
implemented" in the README. What lush does ship today is enough to
build the same patterns by composing existing mechanisms:

- **Hook functions** (this document) for lifecycle behavior.
- **`display lle widget`** for custom editing actions bound to keys.
- **`display lle hook`** for symmetric registration of widget-side
  hooks.
- **`display lle segment`** for custom prompt segments backed by
  shell variables.
- **`source`** of arbitrary shell scripts from
  `~/.config/lush/lushrc` for any extra setup.

See [LLE_GUIDE.md](LLE_GUIDE.md) "Customization" for the
widget/hook/segment trio. The patterns below show how to organize
an extension as a self-contained directory while we wait for the
formal plugin system.

### Convention: Organizing Extensions

Even without a loader, the directory layout below is the convention
extensions will eventually be packaged in:

```
~/.config/lush/extensions/my_ext/
  init.lush      # Main entry point sourced from lushrc
  README.md      # Documentation
```

### Loading

```bash
# In ~/.config/lush/lushrc
source ~/.config/lush/extensions/my_ext/init.lush
```

### Example Extension

```bash
# ~/.config/lush/extensions/git_status/init.lush

# Extension: git_status
# Description: Show git status as a custom LLE segment

_git_status_compute() {
    if git rev-parse --git-dir >/dev/null 2>&1; then
        local branch
        branch=$(git symbolic-ref --short HEAD 2>/dev/null)
        local dirty=""
        if [ -n "$(git status --porcelain 2>/dev/null)" ]; then
            dirty="*"
        fi
        GIT_STATUS_SEGMENT=" ($branch$dirty)"
    else
        GIT_STATUS_SEGMENT=""
    fi
}

# A precmd hook keeps the segment variable current
precmd_functions+=(_git_status_compute)

# Register a custom LLE segment backed by the variable
display lle segment add git_status GIT_STATUS_SEGMENT

# Provide an unload function
git_status_unload() {
    precmd_functions=("${precmd_functions[@]/_git_status_compute}")
    display lle segment remove git_status
    unset GIT_STATUS_SEGMENT
}
```

### Extension Best Practices

1. **Namespace your functions**: Prefix with the extension name.
2. **Provide an unload function**: Allow clean removal even without a
   manager.
3. **Don't pollute global scope**: Use `local` inside helpers; the
   exposed surface should be a small, named set of variables and
   functions.
4. **Handle missing dependencies**: Check `command -v` before using
   external tools.
5. **Document the extension**: Include a README beside `init.lush`.

### Toward a Formal Plugin System

The eventual plugin manager will add: enable/disable lifecycle,
dependency resolution, a permission model, and a repository surface.
Conventions in this document are designed to migrate forward -- an
extension organized as `~/.config/lush/extensions/<name>/init.lush`
will be a one-line move to whatever the manager ends up requiring.

---

## Best Practices

### Performance

Hook functions run frequently. Keep them fast:

```bash
# BAD: Slow hook
precmd() {
    # Running git status on every prompt is slow
    git_status=$(git status --porcelain 2>/dev/null)
}

# GOOD: Cache expensive operations
_cached_git_status=""
_cached_git_dir=""

precmd() {
    local git_dir=$(git rev-parse --git-dir 2>/dev/null)
    if [ "$git_dir" != "$_cached_git_dir" ]; then
        _cached_git_dir="$git_dir"
        _cached_git_status=$(git status --porcelain 2>/dev/null)
    fi
}
```

### Error Handling

Hooks should not break the shell:

```bash
precmd() {
    # Wrap in subshell or handle errors
    (
        set -e
        risky_operation
    ) 2>/dev/null || true
}
```

### Debugging Hooks

```bash
# Enable debugging to see hook execution
debug on 2

# Run a command to trigger hooks
ls

debug off
```

### Conditional Hooks

Only run hooks when needed:

```bash
# Only in interactive shells
precmd() {
    [[ $- == *i* ]] || return
    # ... interactive-only code
}

# Only in specific directories
chpwd() {
    [[ $PWD == ~/projects/* ]] || return
    # ... project-specific code
}
```

### Hook Ordering

Control execution order with arrays:

```bash
# Critical hooks first
precmd_functions=(critical_hook normal_hook optional_hook)

# Or insert at specific position
precmd_functions=(critical ${precmd_functions[@]} final_hook)
```

---

## See Also

- [USER_GUIDE.md](USER_GUIDE.md) - Complete shell reference
- [CONFIGURATION.md](CONFIGURATION.md) - Configuration reference
- [CONFIGURATION.md](CONFIGURATION.md) - Mode requirements for hooks
