# Parser Notes — what EBNF cannot say

Companion to `LUSH_GRAMMAR.ebnf`. The grammar covers the syntactic shape of what `parser.c` accepts; this document covers everything pure CFG cannot express:

- The lexer's context-sensitive behavior (the bulk of the interesting story)
- The heredoc body collection dance
- Position-restoring lookahead patterns
- AST asymmetries (NODE_COMMAND_LIST vs sibling chains)
- Reconstruction-string subgrammars (arithmetic, extended test) — productions deferred to runtime
- Surprises and lush-specific behavior
- Open questions to resolve before the next phase

For per-section detail see `sections/01-top-level.md` … `sections/09-tokenizer.md`. This document synthesizes those into one place.

---

## 1. The lexer is the context engine

The most important thing to understand about lush's parser is that **almost all context-sensitivity lives in the tokenizer**, not the recursive-descent parser. The parser is structurally simple; the tokenizer is parameterized.

### 1.1 The `enable_keywords` flag (tokenizer.h:133)

A boolean on the tokenizer struct. When true, `classify_word()` (tokenizer.c:542–557) maps reserved-word strings to TOK_IF / TOK_THEN / TOK_DO / etc. When false, those same strings come out as TOK_WORD. The parser owns this flag — it toggles it via `tokenizer_enable_keywords()` to control where keywords are recognized.

In practice:
- Keywords are **on** for command-position parsing, body parsing, and most of the recursive descent.
- Keywords are **off** when parsing redirection targets (parser.c:1831–1839) so that filenames named `in`, `do`, `done`, `while`, etc. are accepted as words.

This is lush's primary mechanism for emulating bash's positional reservation. Bash decides "is `do` a keyword here?" by looking at parser context; lush does the same thing by toggling `enable_keywords` from the parser.

### 1.2 The `arith_cmd_depth` counter (tokenizer.h:134)

A counter, incremented on `((` (tokenizer.c:1561) and decremented on `))` (tokenizer.c:1641). Without it, `cat <(cat <(echo nested))` would tokenize the final `))` as TOK_DOUBLE_RPAREN (closing an arithmetic command) instead of two TOK_RPAREN tokens. The depth counter ensures `))` only collapses to TOK_DOUBLE_RPAREN when we are actually inside an arithmetic command. This is a **zsh-ism** that bash does not need.

### 1.3 Feature gates evaluated at lex time

`shell_mode_allows(FEATURE_X)` is consulted *during tokenization*, not just during parsing:

| Feature | Affects tokenization |
|---|---|
| `FEATURE_ARITH_COMMAND` | Recognition of `((` and `))` |
| `FEATURE_PROCESS_SUBSTITUTION` | Recognition of `<(`, `>(`, `\|&`, `&>>` |
| `FEATURE_EXTENDED_GLOB` | Recognition of `@(...)`, `?(...)`, `*(...)`, `+(...)`, `!(...)` |
| `FEATURE_INDEXED_ARRAYS` | Recognition of `var=(...)` as a single word, `arr+=(...)` |
| `FEATURE_BRACE_EXPANSION` | `{a,b,c}` and `{1..10}` as words instead of TOK_LBRACE + content |
| `FEATURE_EXTENDED_TEST` | Recognition of `[[` and `]]` |
| `FEATURE_REGEX_MATCH` | Recognition of `=~` |
| `FEATURE_GLOB_QUALIFIERS` | Suffix qualifiers like `*(.)` |

A feature flip therefore changes what the lexer emits, not just what the parser accepts. This is critical for the polyglot bash+zsh ambition: enabling zsh mode flips lex-time recognition for several constructs.

### 1.4 Two-token lookahead with re-tokenization

The tokenizer keeps `current` and `lookahead` (tokenizer.c:79–80, advanced at 135–149). The clever bit: `tokenizer_refresh_lookahead()` (tokenizer.c:415–434) saves the lookahead's start position, frees it, and re-tokenizes from that position. This lets the parser change `enable_keywords` and have the *same* source text reclassified.

This is the only place in the codebase where the same input text is tokenized twice. It makes some otherwise impossible context switches tractable — flip the flag, refresh the lookahead, and the parser sees the new classification.

### 1.5 Quote contexts at the lexer

- **Single quotes** (`'...'`): fully literal, including newlines. Unclosed → TOK_ERROR.
- **Double quotes** (`"..."`): expandable. Inside, `$(...)` and backticks are scanned to their matching closer and copied verbatim — *the tokenizer does not recurse into them*. Backslash escapes recognized for `\"`, `\$`, `` \` ``, `\\`, `\<newline>` (line continuation, removed entirely).
- **ANSI-C** (`$'...'`): tokenizer scans to closing `'` skipping `\x` sequences without interpreting; the actual `\n → newline` etc. is deferred to expansion.
- **Adjacent quote concatenation** (tokenizer.c:957–1043): `'a''b'`, `'a'$'b'`, `'hello'"world"` collapse into one token at the lexer.

The tokenizer never expands variables or runs command substitutions; it only marks tokens as expandable and hands the raw text to the parser/expansion layer.

---

## 2. Heredocs — the worst-case context-sensitive bit

`<<`, `<<-`, and `<<<` are recognized at the lexer; their **bodies** are read by `collect_heredoc_content()` (parser.c:2030–2227) from the raw input buffer, completely outside the token stream. This is the part of the language no pure CFG handles.

### 2.1 The dance

1. Parser sees TOK_HEREDOC / TOK_HEREDOC_STRIP / TOK_HERESTRING in `parse_redirection` (parser.c:1900–1955).
2. Next token is the delimiter. If it is TOK_STRING or TOK_EXPANDABLE_STRING, the body's expansion flag is set false; otherwise true.
3. Tokenizer is advanced past the delimiter token (parser.c:1918).
4. `collect_heredoc_content` is called (parser.c:1922).
5. That function reads forward through `tokenizer->input` (the raw buffer) line by line until it finds a line that exactly matches the unquoted delimiter (after stripping leading **tabs only** if `<<-`).
6. Body is returned as a string, stored on the heredoc node. The expansion flag is stored as a second child (NODE_VAR with value "1" or "0", parser.c:1945–1953).
7. `tokenizer->position` is updated past the delimiter line; line/column counters are recalculated; `tokenizer_refresh_from_position()` resets the tokenizer's lookahead from the new position.
8. The parser resumes normal token-stream operation.

### 2.2 Subtleties

- **`<<-` strips tabs only**, not whitespace. The check is literal `*line_content == '\t'` (parser.c:2164). Spaces are preserved.
- **Quoting any part of the delimiter disables expansion**. The implementation tracks this through the `target_token->type`. So `<<EOF`, `<<'EOF'`, `<<"EOF"`, `<<\EOF`, and `<<E"O"F` all behave per POSIX.
- **Multiple heredocs on one line work sequentially**: `cat <<A <<B` triggers two separate `collect_heredoc_content` calls. Each finds its own delimiter on a subsequent line.
- **EOF before delimiter**: the loop exits with all remaining input collected as the body, with **no error reported**. This is an open issue — bash diagnoses it. See open questions §10.
- **Heredoc body reading is immediate**, not deferred. This differs from some implementations that buffer pending heredocs and read them at the next newline.

---

## 3. Position-restoring lookahead patterns

Several disambiguations require looking at more than one token, and committing only if the pattern matches.

### 3.1 `()` — anonymous function vs subshell (parser.c:891–920)

`TOK_LPAREN` at command position normally opens a subshell. But if `FEATURE_ANONYMOUS_FUNCTIONS` is on (zsh mode), the parser must also recognize `() { body }`. Algorithm:

1. See TOK_LPAREN.
2. Peek lookahead. If not TOK_RPAREN → subshell, done.
3. Save tokenizer position (`position`, `line`, `column`).
4. Advance past `(` and `)`.
5. Look at current token. If TOK_LBRACE → call `parse_anonymous_function()`.
6. Otherwise restore the saved position and call `parse_subshell()`.

This is the rare case in lush of three-token effective lookahead via state save/restore.

### 3.2 `name()` — function definition vs simple command (parser.c:3752–3769, 955–956)

`is_function_definition` peeks two tokens: word-like + TOK_LPAREN. If matched, `parse_simple_command` hands off to `parse_function_definition`. Crucially, this dispatch happens **before** assignment detection, so `name() {…}` cannot be misread as `name` with no arguments.

### 3.3 `for ((` vs `for var` (parser.c:2582)

After consuming TOK_FOR, the parser checks for TOK_DOUBLE_LPAREN. Single-token lookahead, no save/restore needed.

### 3.4 `coproc` name detection (parser.c:3340–3363)

After `coproc`, the parser looks at the next two tokens. If next is word-like and next-next is also word-like or a compound-command starter (`{`, `(`, `while`, `until`, `for`, `if`, `case`, `select`), then current is the coproc name; otherwise current starts the command and the implicit name "COPROC" is used.

---

## 4. AST shape asymmetries to remember

The AST is not as uniform as one might hope. Three notable asymmetries:

### 4.1 `if`/`elif`/`else` bodies are wrapped; brace-group/subshell bodies are flat siblings

```
NODE_IF
├── condition (logical_expression)
├── then_body  ← NODE_COMMAND_LIST([cmd1, cmd2, …])
├── elif_cond  ← (flat — not recursive)
├── elif_body  ← NODE_COMMAND_LIST(...)
└── else_body  ← NODE_COMMAND_LIST(...)   [optional]
```

vs.

```
NODE_BRACE_GROUP
├── cmd1   (sibling)
├── cmd2   (sibling)
└── cmd3   (sibling)
```

Same shell semantics, different tree. Documented as an open question; possibly intentional for executor convenience.

### 4.2 `while`/`until` conditions are restricted to simple_command or pipeline, not full logical_expression

`while a && b; do ...` does **not** parse as expected. `a` is the condition; `&& b; do ...` is the body. This is a lush-specific choice — bash and zsh allow logical operators in conditions.

### 4.3 Multi-pattern case items are one node, not many

`pat1 | pat2 | pat3) cmds ;;` produces one NODE_CASE_ITEM with `val.str = "0pat1|pat2|pat3"` (single-byte terminator code prefix `'0'`/`'1'`/`'2'`, then patterns with `|` preserved). Executors must split.

---

## 5. Reconstruction-string subgrammars

`(( … ))` and `[[ … ]]` are not parsed into structured ASTs. The parser collects the inner tokens and reconstructs an expression *string*, stored in `val.str`, which a runtime evaluator must re-parse.

### 5.1 Arithmetic command (parser.c:4101–4225)

- Walks tokens until TOK_DOUBLE_RPAREN at `paren_depth == 0`.
- Concatenates token text with heuristic spacing — spaces inserted between most adjacent tokens, suppressed around operator-leading tokens (parser.c:4171–4189).
- Nested `()` tracked by depth.
- Result is a string. The actual arithmetic grammar (operators, precedence, ternary, assignment, `++`/`--`, etc.) is **not** part of the parser-level grammar. It belongs to whatever evaluator consumes `NODE_ARITH_CMD.val.str`.

### 5.2 Extended test (parser.c:4438–4589)

- Walks tokens until TOK_DOUBLE_RBRACKET at `paren_depth == 0`.
- Tracks an `in_regex` flag: set true after TOK_REGEX_MATCH (`=~`), reset on `&&` / `||` at depth 0.
- When `in_regex` is true, **no spaces are inserted** between tokens — preserves regex literals like `^hello$` that the tokenizer split.
- Otherwise normal spacing rules with operator/paren elision.
- Operator vocabulary is not specially tokenized: `==`, `!=`, `<`, `>`, `-z`, `-n`, `-f`, `-d`, etc. arrive as ordinary TOK_WORD / TOK_REDIRECT_IN / TOK_REDIRECT_OUT / TOK_ASSIGN / TOK_REGEX_MATCH and are concatenated back into the string.

**Implication for grammar work.** The full grammar of *what's accepted inside `(())` and `[[]]`* is invisible at this layer. It belongs to the runtime evaluators. A future grammar-spec phase would need to document those evaluators separately.

---

## 6. Numeric and FD-prefix tokenization

Numbers immediately followed by a redirection operator are **single tokens**, not number + operator:

- `2>` → TOK_REDIRECT_ERR (one token, fd embedded)
- `3<` → TOK_REDIRECT_IN_FD
- `2>>` → TOK_APPEND_ERR
- `2>&1`, `2>&-`, `2>&$VAR` → TOK_REDIRECT_FD with the fd, `&`, and target embedded
- `{varname}>` → TOK_REDIRECT_FD_ALLOC (entire `{name}>` is one token)

Whitespace-separated forms tokenize differently: `2 >` is TOK_NUMBER then TOK_REDIRECT_OUT.

For FD-duplication operators like `N>&M`, no separate target child is added to the AST — the target is encoded in the token text and parsed at runtime. For FD-allocation `{varname}>file`, a separate target child *is* added; for `{varname}>&-` and `{varname}>&N` it is not.

---

## 7. Permissive parser, strict tokenizer

Several constructs are **more permissive than bash/zsh** at the parse level:

1. **Assignment values accept keywords and operators.** `x=if y=then z=[` parses as three assignments without complaint. Bash would reject `y=then` at runtime when invoked, but the parser is silent. (parser.c:1066–1205)
2. **Function names outside POSIX mode have no character restriction.** `123() { … }` parses. So does any other word-like token used as a name.
3. **Keywords as arguments.** `echo if while for case` parses fine — keywords are accepted as argument tokens (parser.c:1400). They only trigger compound-command parsing in command position.
4. **Empty arithmetic-for expressions.** `for ((;;))` works, all three slots may be empty.
5. **Implicit `for` word list.** `for i; do ...` defaults to `"$@"` (parser.c:2904–2916). `select` does not.

Conversely, several constructs are **stricter than bash/zsh**:

1. **Function bodies must be brace groups.** `f() ( subshell )` and `f() if true; then …; fi` are rejected. Bash accepts both.
2. **No optional `(` before case patterns.** `case x in (pat) …` is rejected.
3. **`while`/`until` conditions are not logical_expression.** See §4.2.
4. **Function-definition trailing redirections are uncertain.** `parse_function_definition` does not appear to call `parse_trailing_redirections` directly. This may be a gap; see open questions.

---

## 8. Concatenation and the "adjacent token" rule

The parser frequently builds a single conceptual word from multiple adjacent tokens. The adjacency check is positional: `next_token->position == prev_token->position + strlen(prev_token->text)`.

Used in:

- **Argument concatenation** (parser.c:1393–1523): `hello${VAR}world` is three tokens (TOK_WORD + TOK_VARIABLE + TOK_WORD) merged into one NODE_STRING_EXPANDABLE.
- **Assignment value concatenation** (parser.c:1128–1179): `prefix_$(cmd)_suffix`. Single quotes are *preserved with quotes* during concatenation to defend against later expansion (parser.c:1165–1169).
- **For/select word list reassembly** (parser.c:2928–3058): handles `for i in a=1 b=2 c=d=e` correctly by checking adjacency on `=`.

A whitespace gap breaks the merge: `for i in a = 1` is three words `[a, =, 1]`, not one.

---

## 9. Recursion and error recovery

- **Depth limit 256**, enforced by `parser_enter_recursion` / `parser_exit_recursion` (parser.h:35) at logical_expression and pipeline entry points (parser.c:651–654, 762–765). Exceeding sets a parser error rather than crashing.
- **Errors are reported via `parser_error()` / `parser_has_error()`**; the parser does not attempt structural recovery. On error, partial ASTs are freed and NULL is returned (e.g., parser.c:571–575, 724–728).
- **No explicit error recovery for missing terminators.** Missing `}`, `)`, `then`, `fi`, `do`, `done`, `esac` produce errors with help context, but the parser does not attempt to skip to a synchronization point.

---

## 10. Open questions to resolve

These are gathered from all nine section reports. They are real questions, not stylistic — answering them is part of completing the parser map.

### Asymmetries and design intent
1. Why does `parse_if_body` use NODE_COMMAND_LIST while brace-group/subshell bodies are flat siblings? Intentional for executor convenience, or legacy?
2. Why are `while`/`until` conditions restricted to simple_command/pipeline rather than logical_expression? Avoiding ambiguity with bodies, or oversight?
3. Why are keywords accepted as arguments? Compatibility with `echo if`, or lexer design?

### Trailing redirections
4. Do trailing redirections after `function name() { … } > file` actually work? `parse_function_definition` does not appear to call `parse_trailing_redirections`. Likely gap.
5. Are trailing redirections on `for`/`select`/`time`/`coproc` applied to the entire loop, or only the final command in the body?

### Runtime-evaluator grammars
6. Where is `NODE_ARITH_CMD.val.str` evaluated? The arithmetic grammar lives in that evaluator and is currently undocumented.
7. Where is `NODE_EXTENDED_TEST.val.str` evaluated? Same issue.

### Heredoc edge cases
8. Heredoc with EOF before delimiter is silently accepted. Should be a diagnostic.
9. Does a heredoc work correctly when the line containing `<<` continues into a pipeline? The body reader scans forward through input but it is unclear whether intervening `|` / `&&` confuse it.
10. Does `<<<$(cmd)` field-split, and when is the substitution expanded?

### Tokenizer subtleties
11. The arithmetic / extended-test reconstruction uses operator-character heuristics for spacing. Are there pathological inputs where this glues tokens incorrectly (e.g., a TOK_WORD beginning with `+` being concatenated to the previous token)?
12. Two-token lookahead refresh re-tokenizes the same text. Should classifications be cached? Probably not worth it, but worth measuring.
13. `#` is in `is_word_char()`, so `file#1.txt` tokenizes as one word. Intentional flexibility, or accident?

### Feature-gate ambiguities
14. If `FEATURE_INDEXED_ARRAYS` is disabled, does `arr[0]=x` parse as an assignment to a literal variable named `arr[0]`? The code path suggests yes, which is surprising.
15. What happens to `<(cmd)` if `FEATURE_PROCESS_SUBSTITUTION` is disabled? The tokenizer falls back to TOK_REDIRECT_IN + TOK_LPAREN, which then tries to parse as a redirection followed by a subshell. Is that meaningful or just an error?

### Case statements
16. Patterns may contain variables (`case x in $y) …`). Are these expanded at parse or at match time? The AST stores them as literal tokens, so presumably runtime — worth confirming.
17. Tokenizer order is `;;&` before `;&` before `;;`. What about `;; &` (with a space)? Tokenizes as `;;` then `&`. Intentional.

---

## 11. What this enables

With the grammar and these notes in hand:

- **Grammar-based fuzzing** (Step 2 of the plan): generators consume the EBNF directly. Productions clearly mark feature gates so the fuzzer can target each feature subset. The reconstruction-string subgrammars (arithmetic, extended-test) are deliberately left abstract here — when their evaluators are documented, fuzzing can target them separately.
- **Differential testing**: every accepted/rejected input from the bash-compatible subset can be cross-checked with `bash -n`. Productions marked as lush-permissive (§7) are known divergence points and should be excluded from differential checks.
- **Future architectural decisions**: the lexer-as-context-engine pattern is deeply baked in. Any future move to a generated parser would either need to keep lush's lexer wholesale (probable), or replace it with one that supports parameterized recognition (e.g., tree-sitter's external scanners). A pure CFG generator is not viable for the language as it stands.

The grammar is descriptive, not aspirational. It captures `parser.c` as it is on this branch, including its quirks. Fixes to those quirks are out of scope for Step 1.
