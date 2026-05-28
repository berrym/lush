# Pipeline Status Reporting

**Per-stage exit codes (`PIPESTATUS` / `pipestatus`) plus the
`pipeline-diagnostic` mode that surfaces stage failures through the
structured-error system.**

**Status**: shipped.
**Spec lineage**: `docs/SEMANTICS.md` §8 (formerly deferred line
"a modern alternative to `pipefail`").

---

## What it is

Every pipeline execution in lush publishes two facts to the caller:

1. **Per-stage exit codes** in two parallel array variables:
   - `PIPESTATUS` (bash idiom)
   - `pipestatus` (zsh/lush idiom)

   Both arrays are 0-indexed and contain the exit status of every
   stage in the pipeline, in left-to-right order. They are
   regenerated on every pipeline.

2. **Optional structured-error stream** under `set -o
   pipeline-diagnostic`: each non-zero stage queues a
   `SHELL_ERR_PIPELINE_STAGE_FAILED` error (code `E1123`) carrying
   the stage index, total stage count, and exit code. The overall
   pipeline returns the rightmost non-zero code, identical to
   pipefail.

```bash
true | false | true
echo "${PIPESTATUS[*]}"    # 0 1 0
echo "$?"                  # 0   (last stage exit; pipefail off)

set -o pipefail
true | false | true
echo "$?"                  # 1   (rightmost non-zero)

set -o pipeline-diagnostic
true | false | true
# error[E1123]: pipeline stage 2 of 3 exited 1
#   --> script.sh:N:COL
# exit=1
```

## Why it exists -- the Bourne pitfall this kills

Pipelines have always had a structural diagnostic problem. The
classic shell pipeline `a | b | c`:

- Exits with `c`'s status by default. If `a` died horribly,
  `c`'s `0` masks it.
- Or, with `set -o pipefail`, exits with the rightmost non-zero
  status. Better, but still one number. You can tell *that*
  something failed but not *which stage*.

`PIPESTATUS` has existed in bash since the late 1990s but is
under-used because (a) its scope is one pipeline -- the next command
overwrites it -- and (b) failure handling typically wants a
diagnostic stream, not an array the user has to remember to query.

The lush move is:

- **Always populate the array.** `PIPESTATUS` / `pipestatus` are
  unconditional. You don't have to opt in.
- **Offer a `pipeline-diagnostic` mode** that turns each non-zero
  stage into a structured error. Tools that consume the structured-
  error stream (`debug analyze`, the `(lush-debug)` prompt, CI
  parsers) see exactly which stage of which pipeline failed, on
  which line, with what code. The pipeline still returns one exit
  status, but the *forensics* are first-class.
- **Real N-stage execution.** A 4-stage pipeline forks 4 children
  with 3 pipes, waits each, collects 4 codes. Earlier implementations
  ran longer pipelines as right-nested 2-stage pipelines, which
  meant inner-stage exit codes were lost inside intermediate
  subshells. The pipeline executor was rewritten so per-stage data
  survives.

## Behavior matrix

Pipeline returns:

| `pipeline-diagnostic` | `pipefail` | Pipeline `$?` |
|-----------------------|------------|---------------|
| off                   | off        | exit code of the last (rightmost) stage |
| off                   | on         | rightmost non-zero exit code, else 0 |
| on                    | (either)   | rightmost non-zero exit code (`pipefail`-equivalent) plus per-stage structured errors |

`PIPESTATUS` / `pipestatus` array contents are **always** populated
the same way regardless of mode: one element per stage, left to right,
0-indexed.

## Examples

**Iterate per-stage codes:**

```bash
seq 1 5 | grep 9 | wc -l   # 0 1 0 -- grep found nothing

for s in ${PIPESTATUS[@]}; do
    echo "stage exited $s"
done
```

**Reach for a specific stage:**

```bash
some_producer | jq '.field' | some_sink

if (( PIPESTATUS[1] != 0 )); then
    echo "jq failed; data probably malformed" >&2
fi
```

**Use the diagnostic stream for CI:**

```bash
set -o pipeline-diagnostic

# Each failing stage now reports itself with a source location.
# A CI log parser tailing stderr sees E1123 entries and can
# associate them with their source line directly.

build_step | tests | publish
```

## Curated defaults by mode

| Mode  | `pipefail` | `pipeline-diagnostic` | Rationale |
|-------|-----------:|----------------------:|-----------|
| POSIX | off        | off                   | Pure POSIX behavior: pipeline exits with last stage's code. |
| Bash  | off        | off                   | Bash default; `set -o pipefail` is the established opt-in. |
| Zsh   | off        | off                   | Same as Bash. |
| Lush  | off        | off                   | Defaults match the bash/zsh consensus per the principled-deviation rule. Users opt into either or both. |

`PIPESTATUS` and `pipestatus` populate identically in every mode.

## Behavior under nested pipelines and `|&`

**N-stage pipelines** (`a | b | c | d`): all stages fork in parallel
under one parent. The parent collects each child's wait status, fills
the per-stage array, then applies the pipefail / pipeline-diagnostic
policy.

**`|&` (stderr-to-pipe)** is preserved per junction. A pipeline like
`producer |& filter | sink` forwards `producer`'s stderr through the
first junction but uses a normal stdout-only pipe at the second.

**Pipelines inside subshells, command substitution, or
`$(...)`**: each subshell maintains its own `PIPESTATUS`.
Returning from the subshell does not restore the outer scope's
`PIPESTATUS` -- the outer pipeline that ran the subshell sees the
subshell as a single stage and its `PIPESTATUS` reflects that.

## Gotchas

- **`PIPESTATUS` is overwritten by the next pipeline.** Capture it
  immediately after the pipeline if you need it later: `last_ps=("${PIPESTATUS[@]}")`.

- **`"${PIPESTATUS[@]}"` in a scalar slot raises `SHELL_ERR_TYPE_MISMATCH`**
  per the SEMANTICS §3.9 no-implicit-list-to-string rule. Use one of:
  - `"${PIPESTATUS[*]}"` -- explicit space-join.
  - `for s in ${PIPESTATUS[@]}; do ...; done` -- vector context.
  - `@PIPESTATUS` -- the same vector context, lush-mode short form
    (see `sigil-conventions.md`).

- **`pipeline-diagnostic` flips the exit-code rule.** When the mode
  is on, the pipeline returns the rightmost non-zero stage code
  regardless of whether `pipefail` is set. The two settings stack
  but `pipeline-diagnostic` implies pipefail's strict-exit policy.

- **`pipefail` is still the right tool for "fail the script if any
  stage failed"** without the diagnostic noise.
  `pipeline-diagnostic` is right when *which* stage failed matters
  to the consumer.

## See also

- `docs/SEMANTICS.md` §3.9 -- no implicit list-to-string coercion;
  why `"${PIPESTATUS[@]}"` in a scalar slot raises.
- `docs/features/sigil-conventions.md` -- the lush-mode `@`
  short form for vector context.
- `docs/CONFIGURATION.md` -- the four configuration surfaces;
  `pipeline-diagnostic` is a `set -o` option, the registry path is
  `shell.pipeline_diagnostic`.
- `docs/DEBUGGER_GUIDE.md` -- how the structured-error stream
  surfaces in the `(lush-debug)` prompt.
