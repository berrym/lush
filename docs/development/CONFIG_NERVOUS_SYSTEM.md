# The Configuration Nervous System

> Status: **foundation proven, migration in progress.** The keystone (bindings)
> and the layered store (precedence + provenance) are implemented and verified
> in the running shell on the `history.*` section, full suite green. The rest of
> this document distinguishes **PROVEN** (built and tested) from **PLANNED**
> (designed, not yet built) at each point. It is the north star every future
> configuration decision is measured against.

## 1. Why

Shell configuration is the worst-documented, least-consistent, highest-friction
part of the shell ecosystem. Every shell invents several disjoint mechanisms
with incompatible syntax and scattered documentation: bash has `set`, `shopt`,
`bind`, `complete`, `PROMPT_COMMAND`, and `.inputrc`; zsh has `setopt`, `zstyle`,
`bindkey`, and the oh-my-zsh sprawl; fish improved matters with universal
variables and `fish_config`, and that single move is the most-loved thing about
fish. The cost is a brutal learning curve: customizing a shell means learning a
pile of unrelated, poorly-documented, inconsistent tools.

Lush takes the opposite stance. There is **one** configuration store — a central
nervous system — and a small set of discoverable, easy-to-use front-ends over
it. This is an **alternative, not a replacement**: power users keep editing the
TOML file by hand; beginners get a guided, discoverable path; both see the same
truth. The goal is to make a *powerful* shell *learnable to customize*, which is
a genuine differentiator. Lush distinguishes itself by **innovating**, not by
copying the older shells' fragmentation.

This vision is convergent with the best configuration systems in existence —
GNOME's GSettings/dconf, Nix's module system, Emacs' Customize, git's layered
config, VS Code's settings. We arrived at the same shape from first principles;
where this document is specific, it is because those systems proved the pattern
and we are stealing it deliberately (§8).

## 2. The Model

CREG (the config registry) is, in one sentence:

> a **schema-first, reactive, layered** configuration store — the single source
> of truth — where runtime, file (TOML), env, and mode-presets are *layers* over
> it with explicit precedence and provenance; subsystems *bind* to keys instead
> of hand-syncing; and every surface (the `config` CLI, the `display` front-end,
> a schema-generated wizard, the TOML file) is a generated or curated *view* of
> that one store.

Four pillars:

1. **Schema-first** — every option is declared once with type, default,
   range/enum, docs, and a discoverability tier. That one declaration drives
   validation, `config show`, completion, help, and (planned) the wizard.
   *(PLANNED: the full type vtable; today options carry type + default + help.)*
2. **Reactive via bindings** — a binding ties a key to the real runtime lvalue;
   the registry is its single writer and write-throughs every change. This
   replaces hand-written sync hooks and the entire "phantom sync" bug class.
   **(PROVEN.)**
3. **Layered** — each option holds an independent value per layer; the effective
   value is the highest-precedence present layer. Writes route to a layer by
   source. This gives precedence, provenance, and the mode-clobber fix for free.
   **(PROVEN.)**
4. **Single source of truth** — the registry *is* the truth. The legacy
   `config_options[]` table and the parallel C struct are retired section by
   section as each migrates. *(IN PROGRESS: `history.*` migrated; the global
   `config` struct survives as a passive binding target, not a peer source.)*

## 3. Principles (the durable rules)

Every configuration change is held against these:

- **One source of truth.** A value lives in the registry. Runtime fields are
  bound *caches*, never peer sources. Two write paths to the same value is a bug.
- **Declare once.** Type, default, docs, tier, binding — one declaration, many
  consumers. No fact about an option is stated twice.
- **No hand-written sync.** Bindings replace `sync_to_runtime`/`sync_from_runtime`.
  If you find yourself writing a function that copies registry→struct, stop.
- **Precedence is explicit, provenance is free.** Layers, not ad-hoc overrides.
  Every value can answer "where did I come from."
- **Settings are data; behavior is code (§7).** CREG stores settings and named
  tables; it never stores imperative behavior.
- **Discoverability is a content discipline.** Every key ships with a real
  description and a sensible tier, or it does not ship. A flat key dump is a
  failure even if it is technically complete.
- **Surfaces are views.** The `config` builtin, `display`, `setopt`, TOML, env,
  and the wizard are all views over the one store. Redundant surfaces are good
  (an app has both a settings GUI and a config file); a surface with its own
  private truth is not.
- **Migrate without breaking the shell.** Strangler migration: each step leaves
  the build green (werror) and behavior unchanged.

## 4. The mechanism (proven)

### 4.1 Bindings — the keystone

```c
config_registry_bind_boolean("history.enabled", &config.history_enabled);
config_registry_bind_enum("history.finder.match",
                          (int *)&config.history_finder_match,
                          history_finder_match_pairs, HISTORY_FINDER_MATCH_FUZZY);
```

A binding is a typed pointer to the runtime cell plus an optional `on_change`
side-effect hook. `config_registry_set` (the single writer) does, in order:
resolve the option, validate, write the addressed **layer slot**, recompute the
effective value, **write-through** the bound cell (mapping an enum-string to its
int once, here — deleting strcmp ladders), run `on_change` if present, then
notify subscribers. Every set path — interactive `config set`, TOML load, mode
preset — funnels through this, so bound cells stay current with **zero**
hand-written sync.

Two properties matter:

- **Phantom-sync is now a loud failure.** Binding an *unregistered* key returns
  `CREG_ERROR_NOT_FOUND`. The old hooks silently no-op'd on a forgotten or
  mistyped key; bindings cannot.
- **Hot paths stay free.** A binding makes the registry the *sole writer* of a
  plain field; readers (e.g. the renderer reading `config.tab_width`) keep
  reading the plain field at full speed. The slot resolution cost is paid only
  on `set`, never on read.

Proven on `history.*`: the section's two sync hooks and its enum strcmp ladders
were deleted; `config set`, per-mode defaults, and `lushrc.toml` all reach
`config.history_*` through the binding; full suite green.

### 4.2 Layers — precedence and provenance

```
DEFAULT < MODE < SYSTEM < USER(toml) < SESSION(config set)
```

Each option holds one value per layer (`creg_layer_view_t slots[]`). The
effective value is the highest present layer (a pure resolve). Writes route by
source: registration → DEFAULT, mode preset → MODE, `lushrc.toml` → USER,
interactive `config set` → SESSION.

This **structurally** fixes the mode-default clobber. `apply_mode_defaults`
clears the MODE layer wholesale and re-seeds it, so an interactive `config set`
(SESSION) sits above MODE and survives a mode switch, while a stale preset from
the previous mode falls away. No special-case code — the precedence does it.

`config reset KEY` clears the SESSION layer and re-resolves (the value falls
through to file / mode / default), rather than stamping the static default.

### 4.3 Provenance — `config explain`

Because layers are explicit, `config_registry_inspect` reports the effective
value, the winning layer, and every present layer's value + origin for free:

```
$ config explain history.finder.match
history.finder.match = prefix  (from session)
  layers, highest precedence first:
    -> session  = prefix         [config set (session)]
       mode     = substring      [mode preset]
       default  = fuzzy          [default]
```

This is the "lush knows why" guarantee applied to configuration itself — the
shadowed stack that answers *what is this, and where did it come from*. git's
`--show-origin` and VS Code's `inspect()` do this; no shell does.

## 5. Surfaces (all views over the one store)

- **`config` builtin** — speaks CREG keys natively (`get`/`set`/`explain`/
  `show`/`save`). The universal surface; can reach anything. *(PROVEN: get/set/
  explain route through the registry; `show` registry-awareness is PLANNED.)*
- **`display` builtin** — a specialized, ergonomic front-end to the layered
  display/LLE engine and its subsystems (autosuggestions, completion, syntax,
  history, themes, transient, pager, performance/health monitoring). Convenient
  and discoverable; every setting it changes syncs through CREG. It is
  *deliberately* a curated view — redundant with the raw `config` builtin the
  way a settings GUI is redundant with a config file, and just as worth having.
  *(IN PROGRESS: several subcommands synced; full coverage is the display-builtin
  workstream.)*
- **`setopt` / `set`** — the POSIX/bash/zsh-compatible surface, mapped onto CREG
  keys. *(PLANNED: route through the registry as thin projections of the engine
  store.)*
- **`lushrc.toml`** — the persisted serialization of the store, for power users
  who edit by hand. First-class, not an afterthought. `config save` materializes
  the runtime store into it; `config diff` (PLANNED) shows runtime vs saved.
- **env vars** — an override layer for `$LUSH_*` knobs. *(PLANNED.)*
- **Wizard** — a schema-generated, live-preview guided setup that walks the
  beginner tier and writes commented TOML. *(PLANNED; §7.)*

## 6. Config vs Code — the boundary

The most important line in the design. It resolves what does and does not belong
in CREG:

- **Settings and named-relation tables are data → CREG.** Scalars (booleans,
  ints, enums, strings), and keyed tables (keybindings, aliases, prompt segments,
  theme data). These are declarative and belong in the store.
- **Imperative behavior is code → the rc script.** Shell functions, hooks
  implemented as code, custom widgets implemented as logic. These are *executed*,
  not stored as data. CREG stores their **enablement and parameters** (is this
  hook on? with what threshold?), never the behavior itself.

Nix tries to make everything data and pays in complexity; bash makes everything
code and pays in zero introspection. Lush's sweet spot is the line above. A
nervous system signals the muscles; it does not store them. This is what keeps
CREG from becoming a junk drawer, and it is the answer to "how do
widgets/hooks/segments persist": their *config* persists in CREG; their *code*
lives in the script and is re-executed.

## 7. The features that make it a category better

- **Provenance** (`config explain`) — *(PROVEN.)* "What, and from where."
- **Scopes** — session (try) vs persisted (keep); `config save` materializes,
  `config diff` shows the delta. *(PARTIAL: SESSION/USER layers PROVEN; `config
  save` exists; `config diff` PLANNED.)*
- **Schema-driven validation** — one canonical parser/validator per type via the
  type vtable; bad values rejected with a precise, typed error. *(PLANNED: the
  vtable; today validation is per-key.)*
- **Versioning + migration** — the schema carries a version; older TOML migrates
  forward automatically, so options can be renamed/refactored without breaking
  users. This is what makes the system *evolvable* under heavy development.
  *(PLANNED.)*
- **Discoverability** — tiers (beginner/common/advanced/expert) + mandatory
  descriptions, fed to `config show`, completion, help, and the wizard from one
  schema. *(PLANNED: tiers; descriptions exist as `help` today.)*
- **The wizard** — generated *from the schema* (so it is never the hand-coded,
  shallow thing zsh ships), walking the beginner tier with plain-language
  explanations and a **live preview** (change it, watch the prompt/editor change
  now), writing commented TOML at the end. Built last, as a view, once the schema
  and reactivity are complete. *(PLANNED.)*

## 8. Research backbone (what each system contributed)

- **GNOME GSettings/dconf** — the closest proven analog: a schema-backed reactive
  store with *bindings* and a layered database, multiple surfaces over one
  schema. The binding model and the "compile = validate" idea come from here
  (we keep the validation as a CI/startup check and avoid the external
  `glib-compile-schemas` toolchain).
- **Nix/NixOS module system** — `mkOption {type; default; example; description}`:
  the gold standard for *schema-generates-typed-docs* and option precedence.
- **Emacs `defcustom`** — `:type`/`:set`/`:group`: the apply-on-change callback
  (our `on_change`) and "the declaration generates the UI" — 40 years of proof,
  with the cautionary tale of a clunky Customize UI to avoid.
- **git config** — multi-level layering and `--show-origin`: our layers and
  provenance.
- **VS Code settings** — scopes, file-and-GUI as equal views, schema-driven UI:
  our surfaces and scopes.
- **Kubernetes / systemd** — reconciliation (desired-state → reality) and
  drop-in layering: the reactive mental model and the layer cascade.

## 9. Migration strategy (strangler)

One section at a time, behind the unchanged `config_registry_get/set` API, build
green at every step:

1. Add the new mechanism (bindings, layers) to the registry core — done.
2. Migrate a section: bind its keys, delete its sync hooks, remove its rows from
   the legacy `config_options[]` table, drop now-dead enum machinery, update any
   tests that encoded the old semantics.
3. The section's keys now resolve through the registry; the `config` builtin and
   the `config_set/get_*` API reach them via the registry fallback.
4. Repeat. When the legacy table is empty, delete it and the struct duplication.

`history.*` is the proven first migration. The legacy table shrinks; the registry
becomes the sole store.

## 10. Roadmap

- **PROVEN:** bindings (keystone); layered slots; provenance + `config explain`;
  `history.*` migrated; `config_set/get_bool/int` registry-aware.
- **Next polish:** the type vtable (one parser/validator per type, first-class
  enum/range/list/map); the CI schema validator + the startup invariant
  (every bound cell == effective) that turns the *next* phantom-sync into a
  test failure.
- **Then:** migrate the remaining sections (shell, display, completion,
  autosuggestion, behavior, lle); retire the legacy table and struct duplication;
  make `config show` registry-aware; route `setopt`/env through layers.
- **Then:** the full `display` builtin coverage workstream (every subcommand
  bidirectionally synced); `config diff`; schema tiers + descriptions.
- **Last:** versioning/migration; the schema-generated, live-preview wizard.

## 11. Open questions (deliberate, not yet decided)

- **Composite/dynamic values.** Keybindings, aliases, widgets/hooks/segments are
  name→value tables, and the value model today is scalar. A map/dynamic-key type
  (a CREG `GVariant`-like) is needed for true `config save`/`show` coverage of
  those surfaces — vs keeping them in dedicated files with declarative
  re-execution. The config-vs-code boundary (§6) frames this: their *config* is
  data, their *behavior* is code.
- **Theme persistence key** — `display.lle.theme` (parallel to
  `display.lle.pager.*`); the dead legacy `prompt.*`/`prompt.theme*` family was
  removed rather than reused.
- **Reset granularity** — a `config reset [key|section|all]` surface, and whether
  a distinct `display lle defaults` exists alongside the editor-recovery
  `display lle reset`.
