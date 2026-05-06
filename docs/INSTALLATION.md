# Installation Guide

**Building and installing Lush v1.4.0**

---

## Table of Contents

1. [Quick Start](#quick-start)
2. [Requirements](#requirements)
3. [Platform-Specific Instructions](#platform-specific-instructions)
4. [Build Options](#build-options)
5. [Post-Installation](#post-installation)
6. [Troubleshooting](#troubleshooting)

---

## Quick Start

```bash
# Install dependencies (example for Ubuntu/Debian)
sudo apt install build-essential meson ninja-build

# Clone and build
git clone https://github.com/lush/lush.git
cd lush
meson setup build
ninja -C build

# Test
./build/lush --version

# Install (optional)
sudo ninja -C build install
```

---

## Requirements

### Build Requirements

| Requirement | Minimum | Recommended |
|-------------|---------|-------------|
| Meson | 0.53+ | 0.60+ |
| Ninja | 1.8+ | 1.10+ |
| C Compiler | GCC 7+ or Clang 8+ | GCC 11+ or Clang 14+ |
| pkg-config | Any | Latest |

### Runtime Requirements

| Requirement | Minimum | Notes |
|-------------|---------|-------|
| OS | Linux 3.x, macOS 10.12, FreeBSD 11 | Any modern Unix-like |
| Terminal | ANSI compatible | 256-color recommended |
| Memory | 64 MB | Typical usage ~5 MB |
| Disk | 10 MB | For binary and data |

### No Readline Required

Lush uses LLE (Lush Line Editor), a native line editor. GNU Readline is not required.

---

## Platform-Specific Instructions

### Ubuntu/Debian

```bash
# Install dependencies
sudo apt update
sudo apt install build-essential meson ninja-build pkg-config git

# Build
git clone https://github.com/lush/lush.git
cd lush
meson setup build
ninja -C build

# Install
sudo ninja -C build install
```

### Fedora/RHEL/CentOS

```bash
# Install dependencies
sudo dnf install gcc meson ninja-build pkgconf git

# Build
git clone https://github.com/lush/lush.git
cd lush
meson setup build
ninja -C build

# Install
sudo ninja -C build install
```

### Arch Linux

```bash
# Install dependencies
sudo pacman -S base-devel meson ninja pkgconf git

# Build
git clone https://github.com/lush/lush.git
cd lush
meson setup build
ninja -C build

# Install
sudo ninja -C build install
```

### macOS

```bash
# Install dependencies (Homebrew)
brew install meson ninja git

# Build
git clone https://github.com/lush/lush.git
cd lush
meson setup build
ninja -C build

# Install
ninja -C build install
```

Or with MacPorts:

```bash
sudo port install meson ninja git
```

### FreeBSD

```bash
# Install dependencies
pkg install meson ninja pkgconf git

# Build
git clone https://github.com/lush/lush.git
cd lush
meson setup build
ninja -C build

# Install
ninja -C build install
```

### OpenBSD

```bash
# Install dependencies
pkg_add meson ninja git

# Build
git clone https://github.com/lush/lush.git
cd lush
meson setup build
ninja -C build

# Install
ninja -C build install
```

---

## Build Options

### Build Types

```bash
# Debug build (default)
meson setup build --buildtype=debug

# Release build (optimized)
meson setup build --buildtype=release

# Release with debug info
meson setup build --buildtype=debugoptimized
```

### Installation Prefix

```bash
# Custom prefix
meson setup build --prefix=/usr/local

# User-local installation
meson setup build --prefix=$HOME/.local
```

### Reconfiguring

```bash
# Change options on existing build
meson configure build --buildtype=release

# Or reconfigure entirely
meson setup build --reconfigure --buildtype=release
```

### Feature Options

```bash
# View all options
meson configure build
```

---

## Post-Installation

### Verify Installation

```bash
# Check version
lush --version
# Output: lush 1.4.0

# Test basic functionality
lush -c 'echo "Hello from Lush"'

# Test LLE
lush -c 'display lle diagnostics'

# Test debugger
lush -c 'debug help'
```

### Add to Available Shells

```bash
# Add to /etc/shells (requires root)
which lush | sudo tee -a /etc/shells
```

### Set as Default Shell

```bash
# Change login shell
chsh -s $(which lush)
```

### Create Configuration

```bash
# Create startup file
cat > ~/.lushrc << 'EOF'
# Lush startup configuration

# Shell options
config set shell.emacs true

# Aliases
alias ll='ls -la'
alias gs='git status'

# Hooks
precmd() {
    # Update terminal title
    echo -ne "\033]0;${PWD}\007"
}
EOF
```

### PATH Setup (User Installation)

If installed to `~/.local`:

```bash
# Add to shell profile
echo 'export PATH="$HOME/.local/bin:$PATH"' >> ~/.profile
source ~/.profile
```

---

## Troubleshooting

### Build Errors

**meson: command not found**

```bash
# Install via package manager
sudo apt install meson    # Debian/Ubuntu
sudo dnf install meson    # Fedora
brew install meson        # macOS

# Or via pip
pip3 install --user meson ninja
```

**Meson version too old**

```bash
# Install latest via pip
pip3 install --user --upgrade meson
```

**Compiler not found**

```bash
# Install build tools
sudo apt install build-essential  # Debian/Ubuntu
sudo dnf groupinstall "Development Tools"  # Fedora
xcode-select --install  # macOS
```

### Runtime Issues

**Command not found after install**

```bash
# Check installation location
which lush

# If not found, check prefix
meson configure build | grep prefix

# Add to PATH if needed
export PATH="/usr/local/bin:$PATH"
```

**Display issues**

```bash
# Check terminal capabilities
echo $TERM
tput colors

# Ensure UTF-8 locale
export LANG=en_US.UTF-8
```

**Permission denied**

```bash
# System install requires root
sudo ninja -C build install

# Or install to user directory
meson setup build --prefix=$HOME/.local --reconfigure
ninja -C build install
```

### Platform-Specific Issues

**macOS: Command line tools missing**

```bash
xcode-select --install
```

**Linux: Old glibc**

Lush requires glibc 2.17+. On older systems, consider building statically or using a container.

---

## Uninstalling

```bash
# If installed with ninja install
sudo ninja -C build uninstall

# Or manually
sudo rm /usr/local/bin/lush
sudo rm -rf /usr/local/share/lush
```

---

## Building from Release Tarball

```bash
# Download release
curl -LO https://github.com/lush/lush/releases/download/v1.4.0/lush-1.4.0.tar.gz
tar xzf lush-1.4.0.tar.gz
cd lush-1.4.0

# Build
meson setup build
ninja -C build

# Install
sudo ninja -C build install
```

---

## See Also

- [GETTING_STARTED.md](GETTING_STARTED.md) - First steps after installation
- [USER_GUIDE.md](USER_GUIDE.md) - Complete feature reference
- [CONFIGURATION.md](CONFIGURATION.md) - Configuration options
