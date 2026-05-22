# Real-world script corpus

Scripts representative of patterns lush will encounter in actual use, run
through `diff_oracle` against the matching reference shell. Failures
here are 1.5.0 release-readiness signals: each one is something a real
user's script can't run.

## Layout

```
tests/real_world/
├── posix/   POSIX-portable patterns (oracle: dash)
├── bash/    Bash-leaning idioms     (oracle: bash)
├── zsh/     Zsh-leaning idioms      (oracle: zsh)
└── lush/    Polyglot intermix       (no oracle; verifies lush parses+runs)
```

Naming: `NNN_short_description.sh`, three-digit prefix grouped 100-199
for posix, 200-299 bash, 300-399 zsh, 400-499 lush.

## Running

The same `diff_oracle` binary that drives the small grammar-seed corpus
in `tests/fuzz/differential/corpus/` reads this corpus too -- subdirs
under any root are mode-tagged by parent dir name.

```sh
meson compile -C build diff_oracle
find tests/real_world -name '*.sh' | xargs ./build/diff_oracle
```

## Selection criteria

Scripts in this corpus must be:

- **Self-contained** -- no network, no `sudo`, no shared system files.
  Side effects only in `$TMPDIR` if any. `/tmp/<name>.pid` style is fine;
  `/var/run/...` is not.
- **Deterministic** -- no timing, `$RANDOM`, `$$`, or date-dependent
  output (or stripped from the diff oracle's compare).
- **Hermetic** -- no `command -v X` checks that depend on the runner's
  PATH (stub them with a fixed result), no `git` against a real repo,
  etc.
- **Representative** -- mirror an idiom that appears in real-world
  scripts from reputable sources (autoconf, init.d, common bash libs,
  oh-my-zsh, etc.). Don't include adversarial or pathological inputs --
  those go in `tests/fuzz/`.

## What "passes" means

`diff_oracle` produces JSON per script. A pass:
- `agree: true` -- stdout, stderr, and exit code match the oracle
- OR `agree: false, allowed: true` -- a deliberate known-divergence
  (curated in `tests/fuzz/differential/known_divergences.txt`)

Anything else is a 1.5.0 punch-list item.

## Adding scripts

For each addition, verify:

1. The script runs cleanly under its oracle (`dash script.sh` for posix,
   `bash script.sh` for bash, `zsh script.sh` for zsh).
2. The script's output is fully reproducible -- no `$RANDOM`, no
   timestamps that change between runs.
3. The script doesn't touch anything outside `$TMPDIR` / `/tmp`.

Then run `diff_oracle` on the new file alone to see whether lush matches.
