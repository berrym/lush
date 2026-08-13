# Word CST Architecture & Bench-Test Plan — v2.1

**Status:** design, owner-reviewed. Foundation (Phase 1) landed; the bench (Phase 2 / Step 1) is the next build. This document is the authoritative reference; it supersedes the scratchpad drafts v1 and v2.

**Scope (hard):** the WORD subsystem only — `src/tokenizer.c` word emission, `src/parser.c` word/operand builders, `src/dequote.c`, `src/brace_match.c`, and the `src/executor.c` `expand_*` consumers + `build_argv`. The recursive-descent **statement** parser (if/while/case/pipelines/subshells/redirs-as-structure) is retained unchanged; it is not the defect.

**Thesis:** a word is a typed tree, and **the structure is the provenance**. A `$var` written under a double-quoted part is word-split- and glob-suppressed *by construction*, and stays that way *through* expansion, because each part evaluates in its own context. There is no parallel per-byte map, no `strlen` invariant, no threaded `in_double_quotes` bool, no side-channel `node_t` field. A byte-map cannot survive expansion; a tree can.

---

## 0. Why this exists (the defect being fixed)

lush does not represent quote context (unquoted / single / double) as a first-class, uniformly-carried property through the AST + evaluation pipeline. Today it is: carried implicitly by node *type* (`NODE_STRING_EXPANDABLE` vs `NODE_STRING_LITERAL` vs `NODE_VAR`); **lost** where ~8 builders collapse a word to `NODE_VAR` (redir/here-string/for-in target at `parser.c:3298`, case, array elements, typed-fn operands); re-derived ad hoc via ephemeral bool params; and in one case (`${var%"$suffix"}`) *accidental* — it worked only because the tokenizer mis-split `${...}` at an inner `"` and the parser re-fused the pieces. Three review rounds of patching (#654, abandoned) proved the defect is the state model, not the patches. The owner's ruling: fix the model, correct by construction. A per-byte provenance-map RFC was rejected because a byte-map on the pre-expansion string mis-aligns the instant `$x` → `a b c`, losing which *result* bytes are split-eligible.

**The chosen fix: a structured Word CST.** This is what Oil/OSH, fish, and nushell do. Recursive descent stays (it is the state of the art for shell *statement* grammars); only the lossy word node is replaced.

---

## 1. The dedicated type

### 1.1 `word_t` is a distinct top type (an ordered parts vector); `word_part_t` is the discriminated union

Two mutually-recursive types in a new `include/word.h` / `src/word.c`:

- **`word_t`** — a thin container: an ordered vector of `word_part_t*`, a whole-word source span, plus one optional key sub-Word (keyed array elements only; §3.4). Not itself a "part" — it imposes no quoting context. It is the unit that `node_t` references, that `parse_word` returns, and that `word_eval` consumes.
- **`word_part_t`** — a tagged discriminated union: a `kind` enum + a `union` selected by `kind`. A single lexical fragment inside a word.

`word_t` is distinct (not a `WP_GROUP` part) because the container's obligations differ from a part's: it owns a growable vector, it is the recursion unit for operands/subscripts/alternatives/elements, and it is the argument type of every consumer API. The DOUBLE/BARE groups *hold* a child `word_t` (their interior parts sequence); BRACE/ARRAY_LIT hold a *vector* of child `word_t` — so "a group holds child Words" is uniform across all group kinds.

### 1.2 The kind enum (15 structural kinds)

Structural kinds carry **evaluation context only** — the quoting/split/glob eligibility a parent imposes on children. Everything orthogonal (an operator's identity, a tilde's user, a glob's metachars, zsh flags) is scalar/text data riding on a part, never a kind. That rule keeps the enum small: the ~39 syntactic variants collapse onto 15 kinds.

```c
typedef enum {
    /* text leaves — hold BOTH the evaluated bytes and the source span */
    WP_LITERAL,     /* literal run: text = final bytes, no expansion            */
    WP_SINGLE,      /* '...' interior: dequoted, fully literal                  */
    WP_ANSIC,       /* $'...' interior: C-escape-decoded (LUSH_ESC_ANSI_C)      */

    /* single-body groups — hold ONE child word_t (their parts sequence)        */
    WP_DOUBLE,      /* "..."          : imposes D context on the body           */
    WP_BARE,        /* unquoted run   : imposes U context (split + glob)        */

    /* multi-body groups — hold a VECTOR of child word_t                        */
    WP_BRACE,       /* {a,b} {1..n}   : items = alternative Words               */
    WP_ARRAY_LIT,   /* =( ... )       : items = element Words (retires \x1F)    */

    /* parameter expansion — one kind; op/subscript/operands are typed fields   */
    WP_PARAM,       /* $name / ${...} : name + op code + operand/subscript Words */

    /* deferred-body leaves — body kept as raw text, re-parsed at eval          */
    WP_CMDSUB,      /* $(...)                                                    */
    WP_BACKTICK,    /* `...`  : distinct backslash rules -> distinct kind       */
    WP_ARITH,       /* $((expr))                                                 */

    /* other leaves                                                             */
    WP_TILDE,       /* ~ / ~user                                                */
    WP_PROCSUB_IN,  /* <(...)                                                    */
    WP_PROCSUB_OUT, /* >(...)                                                    */
    WP_KINDSIGIL,   /* @name (list) / %name (pair)                              */
} word_part_kind_t;
```

### 1.3 The C11 definitions

```c
typedef struct word      word_t;
typedef struct word_part word_part_t;

struct word_part {
    word_part_kind_t kind;      /* discriminant                                  */
    uint32_t src_off;           /* lossless CST: byte offset into original input */
    uint32_t src_len;           /* span length                                   */

    union {
        /* WP_LITERAL / WP_SINGLE / WP_ANSIC */
        struct {
            char *text;         /* evaluated bytes (owner)                       */
            bool  literal_meta; /* glob metacharacters in text are literal       */
        } leaf;

        /* WP_DOUBLE / WP_BARE : single interior parts-sequence */
        struct { word_t *body; } group;

        /* WP_BRACE / WP_ARRAY_LIT : multiple sub-Words */
        struct { word_t **items; uint32_t n_items; } multi;

        /* WP_PARAM : the only compound leaf */
        struct {
            char    *name;      /* parameter name (owner): "arr", "@", "1"       */
            int32_t  op;        /* index into param_operators[], -1 = bare ref   */
            uint32_t flags;     /* zsh flag bits (U/L/C/@/f/o/s/w/j...) — orthogonal */
            word_t  *subscript; /* [key] as a child Word, or NULL                */
            word_t  *operand;   /* :- / # / % / /pat operand Word, or NULL       */
            word_t  *operand2;  /* replacement Word for ${v/p/r}, or NULL        */
        } param;

        /* WP_CMDSUB / WP_BACKTICK / WP_ARITH */
        struct { char *body; } defer;

        /* WP_TILDE */
        struct { char *user; } tilde;   /* "" or NULL = $HOME; else target user  */

        /* WP_PROCSUB_IN / WP_PROCSUB_OUT */
        struct { char *body; } procsub;

        /* WP_KINDSIGIL */
        struct { char *name; char sigil; } kindsigil;  /* '@' list / '%' pair    */
    } u;
};

struct word {
    word_part_t **parts;        /* ordered parts vector (owner)                  */
    uint32_t      n_parts;
    uint32_t      cap_parts;
    uint32_t      src_off;      /* whole-word source span (lossless CST)         */
    uint32_t      src_len;
    word_t       *key;          /* keyed element key Word, else NULL (owner)     */
};
```

**Text leaves hold both** the *evaluated* bytes (`u.leaf.text`) and the *source span* (`src_off`/`src_len`). The evaluator reads `text`; the formatter/analyzer/LSP read the span to recover the exact original spelling (including quotes `text` already stripped). This is the lexical-vs-evaluation decoupling made concrete.

**`literal_meta` replaces the per-byte `U`/`E` map.** Glob-eligibility of literal bytes (`*.txt` globs; `\*` and `'*'` do not) collapses to **per-leaf** granularity, because `parse_word` creates a leaf boundary exactly where glob-eligibility changes: an escaped `\*` is its own `WP_LITERAL` with `literal_meta=true`; a raw `*` its own leaf with `literal_meta=false`; `WP_SINGLE`/`WP_ANSIC` are always `true`. One bool per leaf replaces an entire parallel byte array with its fragile `strlen` invariant.

**Derived, not stored.** The `#654` operand classification (VALUE vs PATTERN) is a pure function of `u.param.op` computed at eval. The null-word "quoted bit" is accumulated by the evaluator's per-field state, not stored per part. Anything derivable is derived.

### 1.4 PARAM layout

`${arr[key]:-word}` — subscript and operator are typed fields; each sub-expression is a full child Word, so quoting nests by construction:

```
word_part_t  kind=WP_PARAM  name="arr"  op=OP_DEFAULT
   ├─ .subscript → word_t   ── the [key], itself a full Word
   └─ .operand   → word_t   ── the :- operand, itself a full Word
```

A bare `$var` is `WP_PARAM name="var" op=-1`. `${v/p/r}` sets `op=OP_REPLACE`, `operand`=pattern, `operand2`=replacement. `${(U)v}` sets `u.param.flags`. This composes the existing `param_operators[]` taxonomy (`executor.c`) rather than re-inventing it.

### 1.5 Sizeof and ownership

The union is sized by `u.param` (~40 B); with `kind` + spans a `word_part_t` is ~56 B — roughly 40% of the `node_t` it replaces (~120–136 B), and it carries **one** owned-pointer discipline centralized in a single `word_copy`/`word_free`. All lifecycle is one co-located API in `src/word.c`:

```c
word_t      *word_new(uint32_t src_off, uint32_t src_len);
word_part_t *word_part_new(word_part_kind_t kind, uint32_t src_off, uint32_t src_len);
void         word_add_part(word_t *w, word_part_t *p);
void         word_set_body(word_part_t *group, word_t *body);
void         word_add_item(word_part_t *multi, word_t *item);
word_t      *word_copy(const word_t *w);   /* ONE recursive deep copy */
void         word_free(word_t *w);         /* ONE recursive free      */
```

`word_part_copy`/`word_part_free` are `switch (p->kind)` over the union with **no `default`** — so `-Wswitch-enum -Werror` fails the build the moment a new `WP_*` kind is unhandled. That is the field-omission bug class (#488 `magic_equal_value`, #498 `quote_prov`, the live pre-#657 `loc` drop) killed *by the compiler*, not by discipline.

### 1.6 Forward-compat seam for a later Option-B migration

All child traversal goes through accessor macros (`WORD_PART(w,i)`, `PART_BODY(p)`, `PART_ITEM(p,i)`, `PART_OPERAND(p)`, `PART_SUBSCRIPT(p)`), never raw derefs. Today these expand to pointer derefs. Under a future Option B (arena + relative offsets), child fields become `uint32_t` offsets, each macro expands to `arena_base + offset`, `word_copy` degenerates to an O(1) `memcpy`, and the tree becomes serializable — a **localized** edit to `word.h`/`word.c`, touching no call site. **This is designed-for, not built.** Note: lush already ships an arena allocator (`include/lle/arena.h`, `src/lle/core/arena.c`, currently LLE-scoped), so the Option-B substrate is proven infrastructure, not a hypothetical. Ownership now is Option A (typed pointers, O(N) recursive copy): words are 1–5 parts and ephemeral, so O(N) vs O(1) is irrelevant until serialization is actually wanted.

---

## 2. The complete variant set

The 15 kinds cover every word-level construct lush handles. Summary (full parse-source + semantics table in §2 of the v2 draft; unchanged here):

- **Leaves:** literal run; `'...'`; `$'...'` (C-escape); the `\X` escape (folded into a `WP_LITERAL`/`WP_BARE` leaf with `literal_meta`).
- **Groups:** `"..."` (D context); unquoted run (U context); `{a,b}`/`{1..n}` brace; `=( )` array literal.
- **Parameter expansion (one `WP_PARAM` kind, `op` selects):** simple ref; specials `$@ $* $# $? $$ $! $- $0..$9`; default/alt/assign/error; prefix/suffix trim (operand is a *pattern*); case-mod + zsh `(U)(L)(C)`; replace `${v/p/r}`; substring/slice incl. vector slices; length; indirection `${!n}`/`${(P)r}`; name-list `${!pre*}`/`${!pre@}`; transform `${v@Q...}` + zsh `:h:t:r:e`; subscript `${a[k]}`/`${a[@]}`/`${!a[@]}`; is-set `${+N}`; zsh flags; zsh bare subscript `$v[N]`.
- **Cmdsub/arith:** `$(...)`, `` `...` `` (distinct backslash rules), `$((expr))`.
- **Other:** tilde (incl. colon-segmented assignment tilde, replacing `magic_equal_value`); glob metachars + extglob + zsh `#`/`^` (literal bytes in a `WP_BARE`, glob applied post-assembly); glob qualifiers `*(.N)`/`"$f"(N)` (eval-time filter, replacing `glob_qualified`); procsub `<()`/`>()`; kind sigils `@`/`%`.

Each `WP_PARAM` operand/subscript is a **child `word_t`**, so a quoted operand nests correctly.

---

## 3. The `node_t` → `word_t` boundary (the seam — v2.1 REVISION)

Statements remain `node_t`. A `node_t`'s word-bearing slots become `word_t`. This section is the v2.1 correction of the v2 draft, which proposed a uniform `node->words[]` array; the owner rejected that as **false uniformity** — `node_t` is heterogeneous (a redir has one target, a for-in has N items, a command has a name plus args), and a positional array hides heterogeneous meaning behind `words[0]`/`words[1]` conventions paid for at 20+ executor use-sites. v2.1 uses **typed, named fields**, and makes copy-safety hold via a **single canonical copy pipeline**, not via a generic container.

### 3.1 Typed named fields, not a positional array

Each word-bearing node type gets typed, named `word_t*` / `word_t**` fields — `node->redir_target`, `node->cmd_name` + `node->cmd_args` + `node->n_args`, `node->for_list` + `node->n_list`, `node->case_subject`, `node->case_patterns`, `node->assign_rhs`, etc. Self-documenting at every executor use-site (`node->redir_target`, never `node->words[0]`).

### 3.2 Copy-safety via the single canonical `node_copy()` (Phase 1 foundation)

Typed fields alone do **not** fix copy-safety — the omission bug is forgetting to copy a field *at all*, and `word_copy()` vs `strdup()` changes nothing about that. What fixes it is that there is now **exactly one** deep-copy pipeline: `node_copy()` (landed in #657, merge `db7448ee`, which unified the two former drifted walkers `copy_ast_node`/`copy_node_simple`). The word fields are copied there, in one place. This is why Phase 1 came first: with two walkers, typed word-fields would have regrown the N-omissions-in-two-places trap; with one, they are handled once.

Two enforcement layers make omission a caught error rather than a latent bug:

1. **A single word-field copy helper**, `node_copy_word_fields(dst, src)`, switched on `node->type` in `word.c`/`executor.c`, called from the one `node_copy()`. (This is *not* the rejected v2 `node_copy_words` "keep two walkers in sync" scaffolding — there is one walker; this helper is a readability factoring of the word-field arm of that single pipeline, and it is the single place the switch lives.)
2. **A completeness test** asserting that for every node type where `node_has_words(type)` is true, a round-trip `node_copy()` reproduces every word field (deep, not aliased). This is the test-based guarantee that substitutes for the compiler guarantee the word layer gets from `-Wswitch-enum` — see §3.5.

`free_node_tree` gets the symmetric single `node_free_word_fields(node)` arm.

### 3.3 The deleted `node_t` side-fields — net copy-surface shrinks

At the P2 swap these are **deleted** from `node_t`:

| Deleted | Why it existed | Replaced by |
|---|---|---|
| `quote_prov` | per-byte U/S/D/E map on a flattened word | structural DOUBLE/SINGLE/BARE parts + `literal_meta` |
| `name_quoted` | argv[0] quotedness for null-word rule | structural: `cmd_name` is a Word; quotedness in its parts |
| `magic_equal_value` | assignment RHS with tilde provenance | structural: `WP_TILDE` parts in the assignment Word |
| `glob_qualified` | trailing `(N)` glob-qualifier bit | eval-time qualifier on the assembled BARE result |
| `\x1F` sentinel | array-element / fn-signature packing in `val.str` | `WP_ARRAY_LIT` items; structured fn-decl signature (R1) |

Net: four independently-omittable heap side-fields (plus the `\x1F` value-grammar overload) are removed; the added word fields are all copied by the one `node_copy()` pipeline. The `node_t` copy-surface *shrinks* and centralizes.

### 3.4 Slot map — every word-bearing context

`NODE_COMMAND` (name + args), `NODE_ASSIGN` (rhs), `NODE_ARRAY_ASSIGN`/`_APPEND`/`_LITERAL` (element Words; keyed element carries its key in `word_t.key`), `NODE_REDIR_*` (target — scalar; >1 field = ambiguous-redirect E-diag), here-string (body), `NODE_FOR`/`NODE_SELECT` (list items), `NODE_CASE` (subject), `NODE_CASE_ITEM` (`|`-pattern alternatives), `NODE_COND_UNARY`/`_BINARY` (operands; RHS of `==`/`!=`/`=~` = pattern context), `NODE_FN_CALL`/`_RETURN` (kind-value operands, §5.5), `NODE_FOR_ARITH` (arith expression Words). Keyed-element richness is pushed **down** into `word_t.key` so the node fields stay simple; the fn-decl signature (R1) is declaration metadata, not evaluated operands, so it is a structured field on the fn-decl node, not a `word_t`.

### 3.5 The inherent asymmetry (stated honestly)

Perfect compiler-enforced uniformity is achievable at the **word** layer (every part *is* a word part → the no-`default` switch is a total guarantee) but **not** at the **node** layer (only ~15 of ~70 node types bear words → enforcement is a *test*, not the compiler). Among {typed-at-use-site, compiler-enforced-copy-safe, no-generic-container} you get two of three; the rejected `words[]` bought the latter two by sacrificing the first, v2.1 buys the first and recovers the second as the §3.2 completeness test. This asymmetry is the shape of the problem, not a flaw in any design; the word CST is perfectly uniform, and the seam is as clean as `node_t`'s heterogeneity permits, with no hidden gotcha.

---

## 4. Parsing: one `parse_word`

`word_t *parse_word(tokenizer_t *tok, word_ctx_t ctx)` in a new `src/word_parse.c` replaces `collect_word_argument` (`parser.c:1333`) and the ~8 collapse builders. It takes the `tokenizer_t*` (the live token stream), not the `parser_t*`: `parse_word` needs only tokens plus the tokenizer's own lexer-feedback toggles, never the parser's statement state, so taking `tokenizer_t*` keeps the bench free of the whole `parser_t` construction surface and makes "does not take over statement parsing" a type-level guarantee. Two levels:

1. **Token-fusion loop** — consume the maximal run of adjacent no-whitespace word tokens; each becomes one or more **parts, appended, never merged**. `$x"$y"` fuses into one `word_t` = `[WP_BARE(WP_PARAM x), WP_DOUBLE(WP_PARAM y)]`, *keeping* the boundary the current parser destroys.
2. **Per-token structural scan** — for a compound payload, scan into sub-parts composing the shipped primitives: `lush_dequote_span` (unquoted/single/double/escape boundaries + each leaf's `literal_meta`), `lush_find_matching_brace` (`${...}`/`$(...)` boundary), `scan_subscript_bounds` (`[...]`), `lush_dollar_paren_is_arithmetic` (`$((` vs `$(`).

This kills the parser adjacency re-fusion (fusion appends parts, never retypes the aggregate).

> **CORRECTION (verified against the code, 2026-08-13).** An earlier revision of this section also claimed that `parse_word` kills the double-quote reader's char-by-char `${...}` mis-split, by taking the whole `${...}` as one `WP_PARAM` via `lush_find_matching_brace`. **It does not, and cannot.** `parse_word` is TOKEN-driven (`src/word_parse.c`, iterating `tokenizer_current`/`tokenizer_advance`) and breaks the word at whitespace, so by the time it runs the LEXER has already split the word — the tokenizer's double-quoted-string scanner has branches for `$(`, backtick, backslash and newline but none for `${`, so the first `"` inside an expansion ends the token. Proof: `set -- "read=[${m["a b"]}]"` yields `argc=2`. That is issue #654, it is a TOKENIZER defect, and it is not addressable from the CST — the fix belongs in the double-quoted-string scanner (attempted and parked: see #695 and `archive/654-tokenizer-dq-parked`). This correction is recorded because the original sentence is the one a planner acts on. Every part carries a source span from the token offsets. The statement parser is untouched except that each operand rule calls `parse_word(parser->tokenizer, ctx)` and attaches the returned `word_t` to its typed node field. The tokenizer still owns lexer-feedback (heredoc-pending, case-pattern position, arithmetic context); `parse_word` consumes token payloads, it does not take over lexing.

---

## 5. Evaluation

`char **word_eval(executor_t *e, const word_t *w, int *nfields)` in `src/word_eval.c` produces argv fields, subsuming `build_argv`'s per-word pipeline and the reduced copies in the collapse consumers. Decisions read `part->kind` + tree position, **never** scan result bytes.

- **Per-part context, through expansion:** `WP_LITERAL`/`SINGLE`/`ANSIC` append to the last field, never split; `WP_DOUBLE` evaluates its body in D context (interior expansions split/glob-suppressed *by construction*, sets the quoted bit); `WP_BARE` evaluates in U context (PARAM split iff `FEATURE_WORD_SPLIT_DEFAULT`, cmdsub split iff `FEATURE_CMDSUB_WORD_SPLIT` — the independent axis; result brace- then glob-eligible); `WP_PARAM` resolves via the existing `parse_parameter_expansion` with structural subscript/operands, `#654` VALUE/PATTERN policy as the operand dispatch (`pe_glob_suppress` applied *structurally* to the operand Word's quoted leaves, not via a threaded bool).
- **Field-split / glob / null-word:** compose `ifs_field_split`, `expand_glob_pattern` (quoted/`literal_meta` leaves contribute escaped metachars), and the null-word rule (drop a field iff empty AND quoted-bit unset) — reproducing current lush exactly, including its curations (nullglob-vanish, split-default-off, glob-wins-over-split).
- **Expansion survival, correct by construction:** `$x"$y"` with `x="a b"; y="c d"` → `["a", "b c d"]` — BARE splits `x`, DOUBLE appends `y` to the last field unsplit; no byte map, correct for any lengths.
- **Per-consumer return shapes (§5.5):** field-vector (`word_eval` — args, list items, elements, `[[ ]]`); scalar (`word_eval_scalar` — case subject, redir target, assignment RHS; >1-field redir = ambiguous-redirect diag); pattern (case patterns, `[[ == ]]` RHS — quoted metachars stay literal); **kind-value** (`word_eval_value` → `lush_value_view_t` — the typed-fn outlier, where a list/map crosses **un-flattened**, preserving the kind tag; §5.5 and R1).

---

## 6. Integration requirements (R1–R3) — MUST-HANDLE at the P2 swap

The cross-system impact sweep (read-only, verified) confirmed **display and LLE are fully decoupled** from the AST/word representation (zero `node.h`/`parser.h`/`tokenizer.h` includes in `src/display/` or `src/lle/`; the display highlighter is an independent UTF-8 pass, `lle_syntax_highlight`; LLE completion's `word_context.c` is a deliberate codepoint walker). P2's entire real blast radius is core shell. Three consumers require a designed contract **before** the sentinel/API they depend on is retired:

### R1 — Debugger / static-analyzer function-signature metadata (highest; PHILOSOPHY §7)

`src/debug/debug_analysis.c` (`static_fn_name`, `static_fn_return_kind`, ~lines 784/789/808) decodes the `"name\x1F<return_kind>\x1F<pN>:<kN>…"` signature packed into `node->val.str` (mirrored at `include/node.h:109`) via `strchr(encoded, '\x1f')`. This IS the shell's static analyzer (`debug analyze`), and PHILOSOPHY §7 (debugger-keeps-pace, gated by `tests/unit/test_debug_integration.c`) makes it non-optional. **Requirement:** replace the `\x1F` fn-signature packing with a structured fn-decl signature (names + kinds + return kind as typed fields on the fn-decl node), and give the analyzer a `word_part_t`-native / struct-native metadata accessor. Design this API **before** touching the fn encoding.

### R2 — `expand_arg_node` redirection boundary (keep a thin façade)

`src/redirection.c:410` and `:1838` call the public `expand_arg_node()` (`include/executor.h:672`) to expand redirection / here-string targets — the single external runtime consumer of the word-consumption façade. **Requirement:** keep `expand_arg_node` as a thin façade over `word_eval_scalar` (low-risk) rather than migrating these call sites, preserving the public signature across the swap.

### R3 — Array-literal `\x1F` sentinel → builtin handoff

`include/builtins.h:85`/`:110` (`builtin_bind_array_literal`, `builtin_array_name_is_append`) parse the parser-emitted `\x1F name=(...)` / `\x1F name+=(...)` sentinels. **Requirement:** define the new parser→builtin handoff (the `WP_ARRAY_LIT` items / a structured array-assign node) **before** retiring the array `\x1F` sentinel, and migrate these builtins to consume it.

**Explicit non-impacts (do not chase):** `token_t.quote_prov` / `token_t.glob_qualified` (tokenizer layer) *survive* P2 — they are the natural feed into `parse_word → word_part_t`. `symtable.c`'s `METADATA_SEPARATOR "\x1f"` is a distinct layer. The display highlighter's `is_glob_qualifier()` is a byte heuristic unrelated to `node_t.glob_qualified`. LLE's `0x1F` uses are Ctrl+_ / UTF-8 masks.

---

## 7. Bench-test-and-swap

lush's proven pattern (`brace_match.c`, `scan_subscript_bounds`, `lush_dequote_span`, and now `node_copy` #657): build beside, differential-prove to byte-parity, then one clean cutover. **Not** a live strangler — old and new word models never coexist in the live pipeline.

### 7.1 Build in isolation — `wordtool` (Phase 2 / Step 1)

`src/word.c` (type + `word_copy`/`word_free`) + `src/word_parse.c` + `src/word_eval.c` compile into a standalone meson target `wordtool` (+ a unit target), **not** wired into the REPL. `wordtool` reads a word/line, builds a `word_t` via `parse_word`, evaluates via `word_eval`, prints fields NUL-delimited, and (via `--reconstruct`) re-emits source from spans and byte-compares against input to prove the CST is lossless. It links the same `node.c`, `dequote.c`, `brace_match.c`, and the composed executor helpers. Production parser/executor untouched in this phase.

### 7.2 The differential harness

Each corpus line runs through four evaluators — **current lush** (`build_argv` via a `__emit_argv` debug builtin or `lush -c` printing NUL-delimited argv), **wordtool**, **bash**, **zsh** — byte-diffed, under `mode lush|bash|zsh|posix`, `set -f`, `IFS` variants, `shopt`/`setopt` toggles. A triage classifier buckets every diff: *bug* (new ≠ current lush — must be zero before swap), *expected curation* (matches lush, differs from bash/zsh where lush documents a curation — recorded with its SEMANTICS/CLAUDE.md citation), *peer disagreement* (bash and zsh already differ — lush's curated pick checked against its documented rationale). This reuses the #657-proven method (worktree of pre-change master, build both, byte-diff a corpus) at scale. Corpus categories (≥ tens of thousands of lines, seeded + fuzzed via `diff_oracle`): the 16 categories of the v2 draft plus a CST-reconstruction category.

### 7.3 Parity bar

Zero *bug*-bucket diffs across the whole corpus in all four modes; every bash/zsh diff classified with a written rationale; historical regression goldens (#488/#495/#498/#530/#631/#654) green; full `--reconstruct` parity; `wordtool` valgrind/ASan-clean under `detect_leaks=1` (#481 gate). The bar is "match current lush exactly" — lush's curations are the spec.

### 7.4 The clean cut (no build flag)

One cutover commit: point the ~8 builders + `collect_word_argument` at `parse_word`; add the typed word fields + the single `node_copy` word-field arm (§3.2); route `build_argv` and every consumer through `word_eval`/`_scalar`/`_value`; keep `expand_arg_node` as the R2 façade; land the R1 structured fn-signature + R3 array handoff; delete `quote_prov`/`name_quoted`/`magic_equal_value`/`glob_qualified` + the `\x1F` sentinels + the flat-`NODE_VAR` runtime re-parse. **No `-Dword_ast`, no `#ifdef`** — the harness is the net, rollback is a git branch. Because bench-proving reached byte-parity in isolation, the live suite lands green on the cutover.

---

## 8. Scope, risk, sequencing

**Honest size.** Large. New `src/word.c` (~300 LOC) + `word_parse.c` + `word_eval.c` (~2–3k LOC composing existing helpers) + `wordtool`/harness. Edits to tokenizer word emission, ~8 builders, `expand_arg_node`, `build_argv`, the for-in/array/select consumers, the single `node_copy` word arm, and the R1–R3 boundaries. Multi-week, adversarial-review-gated per PR.

**The "150 tests fail" valley — front-loaded into isolation, but not eliminated.** The harness removes the *word-semantics* valley (quoting/splitting/glob/PE/concatenation) with a fine-grained per-word oracle. It does **not** rehearse the tokenizer **lexer-feedback** seam (case-pattern position, heredoc bodies, arithmetic context), because `parse_word` consumes context-dependent tokens rather than replacing lexing — that ~30% integrates *live*, incrementally, per context swap, with the suite as backstop. That seam is the residual integration risk; eyes open on it.

**Sequencing — prove in the harness in ascending blast radius, then swap atomically:** (1) command args + `[[ ]]` — the core evaluator where structure already mostly exists; (2) redir target + here-string; (3) for-in / compact-for / select; (4) case subject + `|`-patterns; (5) array literals (`\x1F` retirement, R3); (6) PE operands + subscript keys (nested-Word proof, #631/#654 fixes); (7) typed-fn call/return (kind-value contract, R1 signature retirement). This is the *proving* order, not seven live steps; the production swap is one atomic commit after (7) proves out.

**Curation calls the owner ratifies at swap** (reproduce current lush; any move toward bash/zsh is explicit post-swap work): single-quote-in-DQ; quoted-glob-literal in PE operands + case patterns (SEMANTICS §3.6, the `archive/654` logic folded in as leaf evaluation); empty-quoted-word; glob-wins-over-split; brace-after-param ordering.

---

*Foundation landed: PR #657 (`db7448ee`) unified the two AST deep-copy walkers into one canonical `node_copy()`, the single seam the typed word fields attach to. Next: Phase 2 / Step 1 — stand up `word.c` + `wordtool` + the differential harness, prove the command-args + `[[ ]]` path to byte-parity in isolation.*
