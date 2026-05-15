# Getting Started with Lush

**Your first steps with the shell that does everything differently**

---

## Table of Contents

1. [What is Lush?](#what-is-lush)
2. [Installation](#installation)
3. [First Steps](#first-steps)
4. [The Line Editor (LLE)](#the-line-editor-lle)
5. [Shell Modes](#shell-modes)
6. [Your First Script](#your-first-script)
7. [The Integrated Debugger](#the-integrated-debugger)
8. [Configuration](#configuration)
9. [Next Steps](#next-steps)

---

## What is Lush?

Lush is an advanced interactive shell that combines the familiarity of Bash and Zsh with capabilities found nowhere else.

### Three Things That Set Lush Apart

**1. LLE - The Lush Line Editor**

Every other shell wraps GNU Readline or implements something similar. Lush built its own line editor from scratch. LLE provides Emacs-style editing, context-aware completions for all 45 builtins, real-time syntax highlighting, and the foundation for features that aren't possible with traditional line editing libraries.

**2. Multi-Mode Shell Architecture**

Run in POSIX mode for strict compliance, Bash mode for compatibility with bash scripts, Zsh mode for zsh scripts, or Lush mode (the default) which combines the best features from all three plus Lush-specific extensions.

**3. Integrated Interactive Debugging**

No other shell offers this. Set breakpoints, step through scripts line by line, inspect variables at any point in execution, and profile performance - all from within the shell itself.

### Who Should Use Lush

- **Script developers** who want debugging that actually works
- **Power users** who want modern editing and completions
- **Teams** running scripts from mixed environments (bash, zsh, POSIX)
- **Anyone** who wants a shell that respects their time

---

## Installation

### Build Requirements

Lush has minimal dependencies:

```bash
# Ubuntu/Debian
sudo apt-get install build-essential meson ninja-build

# CentOS/RHEL/Fedora
sudo dnf install gcc meson ninja-build

# macOS (with Homebrew)
brew install meson ninja

# Arch Linux
sudo pacman -S base-devel meson ninja
```

Note: Lush does not require readline. LLE is a complete, native implementation.

### Build from Source

```bash
# Clone the repository
git clone https://github.com/lush/lush.git
cd lush

# Build with Meson
meson setup build
ninja -C build

# Verify the build
./build/lush --version
# Output: lush 1.4.0
```

### Optional: Install System-Wide

```bash
# Install to /usr/local (requires root)
sudo ninja -C build install

# Or install to custom location
meson setup build --prefix=/home/user/.local
ninja -C build install
```

---

## First Steps

### Starting Lush

```bash
# Start interactive session
./build/lush

# You'll see a prompt with context
[user@hostname] ~/projects/lush (main) $
```

The prompt shows your username, hostname, current directory, and git branch when applicable.

### Basic Interaction

```bash
# Navigation
pwd                    # Print working directory
cd ~/projects          # Change directory
ls -la                 # List files

# Variables
name="Lush"
echo "Welcome to $name"

# Environment
export EDITOR=vim
echo $EDITOR
```

### Getting Help

```bash
# General help
help

# Builtin-specific help
help cd
help set
help debug

# Display system information
display lle diagnostics    # LLE status
display features           # Enabled features
```

---

## The Line Editor (LLE)

LLE is what you interact with every time you type a command. It's not readline - it's a purpose-built editor designed for shell interaction.

### Emacs Mode (Default)

LLE ships with complete Emacs-style keybindings:

| Keybinding | Action |
|------------|--------|
| `Ctrl-A` | Beginning of line |
| `Ctrl-E` | End of line |
| `Ctrl-B` | Move backward one character |
| `Ctrl-F` | Move forward one character |
| `Alt-B` | Move backward one word |
| `Alt-F` | Move forward one word |
| `Ctrl-K` | Kill to end of line |
| `Ctrl-U` | Kill to beginning of line |
| `Ctrl-W` | Kill previous word |
| `Ctrl-Y` | Yank (paste) killed text |
| `Ctrl-D` | Delete character / EOF on empty line |
| `Ctrl-L` | Clear screen |
| `Ctrl-R` | Reverse history search |
| `Ctrl-P` / `Up` | Previous history |
| `Ctrl-N` / `Down` | Next history |
| `Tab` | Complete |
| `Ctrl-_` | Undo |

### Context-Aware Tab Completion

LLE knows what you're typing:

```bash
# Command completion
deb<Tab>              # Completes to "debug"

# Subcommand completion
debug <Tab>           # Shows: on off vars print trace profile ...

# Builtin-specific arguments
set -o <Tab>          # Shows all shell options
config set <Tab>      # Shows configuration sections
display <Tab>         # Shows: lle features themes

# File completion
cat ~/Doc<Tab>        # Completes paths

# Variable completion
echo $HO<Tab>         # Completes $HOME
```

All 45 shell builtins have context-aware completions that understand their specific options and arguments.

### Syntax Highlighting

As you type, LLE colors your input:

- **Commands**: Highlighted when valid, indicated when not found
- **Strings**: Quoted text in a distinct color
- **Variables**: `$var` and `${var}` highlighted
- **Operators**: Pipes, redirections, and control operators
- **Comments**: Dimmed or distinct color

This isn't cosmetic - it helps catch errors before you press Enter.

### Multi-Line Editing

Incomplete commands continue naturally:

```bash
$ for i in 1 2 3; do
>     echo "Number: $i"
> done
Number: 1
Number: 2
Number: 3
```

LLE understands shell syntax and provides continuation prompts appropriately.

---

## Shell Modes

Lush can behave like different shells depending on what you need.

### Available Modes

| Mode | Description |
|------|-------------|
| `lush` | Default. All features enabled. Best interactive experience. |
| `posix` | Strict POSIX sh compliance. For portable scripts. |
| `bash` | Bash compatibility. Arrays, `[[]]`, process substitution. |
| `zsh` | Zsh compatibility. Extended globbing, parameter expansion. |

### Setting the Mode

```bash
# Interactive - set mode
set -o lush      # Default mode (usually already set)
set -o posix       # Strict POSIX mode
set -o bash        # Bash compatibility
set -o zsh         # Zsh compatibility

# In scripts - shebang detection
#!/usr/bin/env lush          # Lush mode
#!/usr/bin/env lush --posix  # POSIX mode
```

### What Changes Between Modes

**POSIX mode** disables extensions for maximum portability:
- No arrays
- No `[[]]` (use `[ ]`)
- No process substitution
- No extended globbing

**Bash mode** enables Bash-compatible features:
- Indexed and associative arrays
- `[[]]` extended test
- Process substitution `<()` and `>()`
- Extended parameter expansion

**Zsh mode** adds Zsh-specific behaviors:
- Glob qualifiers
- Additional parameter expansion forms
- Zsh-style option names

**Lush mode** (default) provides everything:
- All Bash features
- All Zsh features
- Lush-specific extensions (hooks, enhanced debugging)
- Best interactive experience

### Recommendation

Use **Lush mode** for interactive work and scripts that will only run in Lush. Use **POSIX mode** for scripts that must be portable. Use **Bash/Zsh modes** when running scripts from those environments.

---

## Your First Script

### A Simple Script

```bash
#!/usr/bin/env lush

# hello.sh - First lush script
echo "Hello from Lush v1.4.0"

# Variables work as expected
name="World"
echo "Hello, $name!"

# Loops
for item in apple banana cherry; do
    echo "Fruit: $item"
done
```

Save as `hello.sh`, then:

```bash
chmod +x hello.sh
./hello.sh
```

### Using Extended Syntax

Lush mode enables modern shell features:

```bash
#!/usr/bin/env lush

# Arrays
fruits=(apple banana cherry)
echo "First fruit: ${fruits[0]}"
echo "All fruits: ${fruits[@]}"

# Associative arrays
declare -A colors
colors[apple]="red"
colors[banana]="yellow"
echo "Apple is ${colors[apple]}"

# Extended test
file="script.sh"
if [[ $file == *.sh ]]; then
    echo "This is a shell script"
fi

# Arithmetic
count=5
(( count++ ))
echo "Count is now: $count"

# Process substitution
diff <(ls dir1) <(ls dir2)

# Extended parameter expansion
text="hello world"
echo "${text^^}"        # HELLO WORLD
echo "${text//o/0}"     # hell0 w0rld
```

### Debugging Your Script

This is where Lush stands alone:

```bash
#!/usr/bin/env lush

# Enable debugging
debug on

# Your code runs with full visibility
for i in 1 2 3; do
    result=$((i * 10))
    echo "Result: $result"
done

# Check variables at any point
debug print result
debug vars

debug off
```

Run the script and watch the debugger trace execution, show variable values, and let you understand exactly what's happening.

---

## The Integrated Debugger

No other shell has this. The debugger is built into Lush, not bolted on.

### Quick Start

```bash
# Enable debugging
debug on

# Run commands - they're traced
ls -la
echo "test"

# Inspect state
debug vars           # Show all variables
debug print PATH     # Show specific variable

# Disable
debug off
```

### Debug Levels

```bash
debug on 1    # Basic tracing
debug on 2    # Detailed tracing
debug on 3    # Maximum verbosity
```

### Profiling

```bash
# Start profiling
debug profile on

# Run some commands
for i in $(seq 1 100); do
    echo $i > /dev/null
done

# See timing information
debug profile report
debug profile off
```

### In Scripts

```bash
#!/usr/bin/env lush

# Selective debugging
complex_function() {
    debug on 2
    # ... complex logic ...
    debug off
}

# Profile specific sections
debug profile on
expensive_operation
debug profile report
debug profile off
```

For comprehensive debugging documentation, see [DEBUGGER_GUIDE.md](DEBUGGER_GUIDE.md).

---

## Configuration

### The Config System

Lush uses a modern configuration system:

```bash
# View configuration
config show                    # All sections
config show shell              # Shell options
config show completion         # Completion settings
config show display            # Display settings

# Get specific values
config get shell.errexit
config get completion.enabled

# Set values
config set shell.errexit true
config set display.syntax_highlighting true
```

### Shell Options

Both modern and traditional syntax work:

```bash
# Modern syntax (self-documenting)
config set shell.errexit true    # Exit on error
config set shell.nounset true    # Error on unset variables
config set shell.xtrace true     # Trace execution

# Traditional POSIX syntax (also works)
set -e                           # Same as shell.errexit
set -u                           # Same as shell.nounset
set -x                           # Same as shell.xtrace
```

### Startup Configuration

Create `~/.lushrc` for persistent configuration:

```bash
# ~/.lushrc - Lush startup configuration

# Shell behavior
config set shell.errexit false
config set shell.emacs true

# Completion
config set completion.enabled true

# Display
config set display.syntax_highlighting true

# Aliases
alias ll='ls -la'
alias gs='git status'
alias gd='git diff'

# Functions
mkcd() {
    mkdir -p "$1" && cd "$1"
}

# Hook functions (Lush-specific)
precmd() {
    # Runs before each prompt
    :
}

preexec() {
    # Runs before each command
    :
}
```

---

## Next Steps

You now have a working Lush installation with:
- LLE for modern command-line editing
- Tab completion that understands context
- Syntax highlighting as you type
- Shell modes for any script compatibility needs
- Integrated debugging unavailable anywhere else

### Explore Further

| Document | What You'll Learn |
|----------|-------------------|
| [USER_GUIDE.md](USER_GUIDE.md) | Complete feature reference |
| [LLE_GUIDE.md](LLE_GUIDE.md) | Full LLE documentation |
| [EXTENDED_SYNTAX.md](EXTENDED_SYNTAX.md) | Arrays, `[[]]`, process substitution |
| [CONFIGURATION.md](CONFIGURATION.md) | Detailed mode documentation |
| [DEBUGGER_GUIDE.md](DEBUGGER_GUIDE.md) | Complete debugging reference |
| [BUILTIN_COMMANDS.md](BUILTIN_COMMANDS.md) | All 48 builtin commands |
| [HOOKS_AND_PLUGINS.md](HOOKS_AND_PLUGINS.md) | Hook system and plugins |

### Try These

1. **Explore completions**: Type partial commands and press Tab
2. **Try the debugger**: `debug on`, run commands, `debug vars`
3. **Test extended syntax**: Arrays, `[[]]`, `${var^^}`
4. **Set up your profile**: Create `~/.lushrc` with your preferences
5. **Read the LLE guide**: Master the keybindings

### Get Help

```bash
help              # General help
help <builtin>    # Specific builtin help
debug help        # Debugger help
display help      # Display system help
```

Welcome to Lush. You're now using a shell that was built to be worth using.
