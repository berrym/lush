# Corpus ingestion harness

The harness in `_harness/` automates the mechanical parts of bringing
upstream production scripts into `corpus/` -- hermeticity scanning,
standard transforms, provenance-header generation, oracle determinism
verification -- while keeping the human in the loop on script
selection.

## Goals

- **Scale**: curating 400+ scripts by hand is not realistic. The
  harness automates the parts that don't require judgment, so each
  ingest takes minutes rather than an hour.
- **Provenance**: every script tracks its upstream URL, commit hash,
  license, and exactly what was transformed during adaptation.
- **Auditability**: every hermeticity waiver lands in a top-level
  `corpus/WAIVERS.md` ledger; tighten policy by closing waivers.
- **Determinism guarantee**: an ingest cannot succeed until the
  adapted script runs reproducibly under its reference shell.

## Pipeline

For a candidate `path/to/upstream.sh`:

```
ingest.sh
  ├── scan-violations.sh          (1) detect non-hermetic constructs
  ├── hermeticize.sh              (2) apply standard transforms
  ├── header-template.sh          (3) generate provenance header
  ├── place under corpus/<bucket>/<set>/
  ├── append to corpus/<bucket>/<set>/SOURCES.md
  ├── append waivers to corpus/WAIVERS.md
  └── run reference shell twice, refuse on non-determinism
```

The order matters: scan rejects hard violations before any transform
runs, so a script that uses `sudo` or `curl` fails fast.

## Invocation

```sh
tests/real_world/_harness/ingest.sh \
    --source-url    "https://github.com/scop/bash-completion/blob/abc123/completions/ls" \
    --upstream-commit abc1234567 \
    --license       "GPL-2.0-or-later" \
    --bucket        bash \
    --upstream-set  bash-completion \
    [--rename-to    new-name.sh] \
    [--waive        "nondet:upstream uses $$ as a pidfile name; runner sets TEST_PID env var" ...] \
    /path/to/candidate.sh
```

The script ends up at
`tests/real_world/corpus/bash/bash-completion/<final-name>.sh`.

## Hermeticity policy

### Hard reject (no waiver accepted)

These categories cannot be adapted to satisfy the corpus invariants;
the harness refuses to ingest regardless of `--waive`:

- **`network`** -- `curl`, `wget`, `ftp`, `nc`, `ssh`, `scp`, `rsync`.
  Network access is non-hermetic by construction.
- **`sudo`** -- `sudo`, `doas`, `pkexec`, bare `su` invocations.
- **`external`** -- live VCS operations (`git clone`, `git fetch`,
  `git push`, `git pull` against a remote).

Scripts in these categories belong elsewhere (perhaps as inputs to a
separate integration-test corpus that explicitly sandboxes side effects).

### Waivable with explicit reason

These can be ingested when a `--waive "<category>:<reason>"` argument
documents why the construct is safe in this script:

- **`nondet`** -- `$RANDOM`, `$$`, `$(date ...)`, `/dev/urandom`,
  `$EPOCHSECONDS`. The harness auto-transforms most of these to
  fixed values; the waiver is needed only when the script uses a
  construct that the transform doesn't cover or in a way that
  matters semantically (e.g., the script intentionally needs a
  unique-per-run id).
- **`filesys`** -- writes outside `/tmp`, reads of `/proc/*` or
  `/sys/*`. Cross-platform fallback paths (`uname`, `getconf`,
  cached config) are preferable; waive only when no cross-platform
  alternative exists and the script will only ever run on Linux.

Waiver format: `--waive 'category:reason'`. The reason is required
and must be substantive ("upstream uses /proc/cpuinfo as a Linux-only
optimization; falls back to `nproc` on other platforms" -- not
"safe").

### Auto-transformed (no action needed)

`hermeticize.sh` applies these substitutions silently and records
them in the provenance header's `TRANSFORMS:` block:

| Construct        | Replacement              |
|------------------|--------------------------|
| `$RANDOM`        | `12345`                  |
| `$$`             | `99999`                  |
| `$(date ...)`    | `"1970-01-01T00:00:00Z"` |
| `` `date ...` `` | `"1970-01-01T00:00:00Z"` |
| `$EPOCHSECONDS`  | `0`                      |
| `$EPOCHREALTIME` | `0.000000`               |

## Locale

The oracle dry-run runs under `LC_ALL=C` always. Multi-locale
behavior testing is a separate concern with its own test surface, not
the corpus's job. Documented in the design discussion (memory:
`project-real-world-script-corpus`).

## $HOME and sandboxing

The harness expects the runner (i.e., `diff_oracle` and the scorecard
script) to set an ephemeral `HOME` for execution. Scripts that read
`$HOME` are allowed; scripts that write outside `$TMPDIR` are
rejected by the `filesys` scanner.

## Adding a new upstream set

For each new set (e.g., adding `prezto` alongside `oh-my-zsh`):

1. Pick the bucket (`posix` / `bash` / `zsh` / `lush`).
2. Ingest at least one script with `--upstream-set <name>`. The first
   ingest creates `corpus/<bucket>/<set>/SOURCES.md`.
3. Add a `corpus/<bucket>/<set>/LICENSE.txt` containing the upstream
   license text verbatim. The harness doesn't enforce this; it's a
   project rule. If the set's license differs per file, document the
   policy in `SOURCES.md`.

## When ingest fails

The harness exits non-zero with a specific code for each failure
class:

| Exit code | Reason | Recovery |
|-----------|--------|----------|
| 2 | Missing required argument or input file not found | Re-invoke with correct args |
| 3 | Unwaived hermeticity violations | Adapt the script or pass `--waive` |
| 4 | Final path already exists | Use `--rename-to`, or remove the existing file if it's a re-ingest |
| 5 | Oracle dry-run not deterministic | Investigate; this means the upstream script's own output isn't reproducible -- it's not a candidate for this corpus |

## What the harness does NOT do

- It does not fetch from upstream. You provide a local file path; the
  `--source-url` and `--upstream-commit` arguments record where it came
  from but the harness assumes you've already cloned / downloaded.
- It does not test against lush. That's the scorecard's job. The
  oracle dry-run verifies determinism only.
- It does not enforce license-text-presence. That's a manual policy
  check during a batch ingest.

## Manifest

Each upstream set has its own `SOURCES.md` under
`corpus/<bucket>/<set>/`. A top-level
`corpus/MANIFEST.md` summarizing every set is **maintained by hand**
when a new set lands -- the harness does not regenerate it.
