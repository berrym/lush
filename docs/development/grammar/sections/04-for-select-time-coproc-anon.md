# Section 4 — for, select, time, coproc, anonymous functions

## Overview

This section documents the grammar for loop constructs (`for`), selection (`select`), timing (`time`), coprocess creation (`coproc`), and anonymous function definitions in lush. These are implemented as hand-written recursive descent parsers in `src/parser.c`.

## EBNF productions

```ebnf
for_statement ::= 'for' (for_arith | for_posix)

for_arith ::= '((' arith_init ';' arith_test ';' arith_update '))'
              separators 'do'
              separators command_body
              separators 'done'
              [trailing_redirections]

for_posix ::= WORD [in_clause] separators 'do'
              separators command_body
              separators 'done'
              [trailing_redirections]

in_clause ::= 'in' word_list
            | (* empty: defaults to "$@" *)

word_list ::= word {word}

arith_init ::= (* arithmetic expression string, may be empty *)
arith_test ::= (* arithmetic expression string, may be empty *)
arith_update ::= (* arithmetic expression string, may be empty *)

select_statement ::= 'select' WORD [in_clause] separators 'do'
                     separators command_body
                     separators 'done'
                     [trailing_redirections]

time_command ::= 'time' ['-p'] pipeline

coproc_command ::= 'coproc' [NAME] pipeline

anon_function ::= '(' ')' '{' command_body '}'

separators ::= { (';' | NEWLINE | WHITESPACE) }

trailing_redirections ::= redirection {redirection}
```

## AST nodes produced

| Node type | Produced by | Notes |
|-----------|-------------|-------|
| `NODE_FOR` | POSIX `for var in list` | Children: [0] word_list (NODE_VAR container), [1] body; val.str = variable name |
| `NODE_FOR_ARITH` | C-style `for ((;;))` | Children: [0] NODE_ARITH_EXP (init), [1] NODE_ARITH_EXP (test), [2] NODE_ARITH_EXP (update), [3] body; each expression may be empty string |
| `NODE_SELECT` | `select var in list` | Children: [0] optional word_list (NODE_VAR container), [1] body; val.str = variable name |
| `NODE_TIME` | `time [-p] pipeline` | Children: [0] pipeline; val.sint = 1 if `-p` flag present, 0 otherwise |
| `NODE_COPROC` | `coproc [name] cmd` | Children: [0] command (pipeline); val.str = coprocess name (NULL for default "COPROC") |
| `NODE_ANON_FUNCTION` | `() { body }` | Children: [0] brace group body; defined in Zsh-style shell modes only |
| `NODE_ARITH_EXP` | Arithmetic expression (child of FOR_ARITH) | val.str = raw expression text, preserving operators like `<=` |
| `NODE_VAR` | Word list container | Used as container for word children in FOR and SELECT; not semantic |

## Context-sensitive behavior and lexer interactions

### Reserved words
- `for`, `in`, `do`, `done`: Keywords for POSIX and C-style for loops
- `select`: Keyword introducing interactive selection loop
- `time`: Prefix keyword for timing commands
- `coproc`: Prefix keyword for coprocess creation (requires `FEATURE_COPROC` flag)

### Distinguishing `for ((` from `for var`

**Location:** `parse_for_statement()` at parser.c:2569, lines 2582–2593

Immediately after consuming `TOK_FOR`, the parser checks for `TOK_DOUBLE_LPAREN`:
- If present, parses C-style arithmetic for: `for ((init; test; update)); do ... done`
- If not present, parses POSIX for: `for var in [words]; do ... done`

No backtracking; the lookahead is deterministic.

### C-style for loop expression parsing

**Location:** parser.c:2610–2762

Expressions are extracted **directly from raw input text** (not from tokenizer output) to preserve operator tokens like `<=` that the tokenizer splits into `<` and `=`:

1. **Parse init expression** (parser.c:2610–2639)
   - Scan tokens until first semicolon at paren depth 0
   - Track nested `(` `)` and `(( ))` to skip nested parentheses
   - Extract raw substring, trim whitespace
   - Expression may be **empty** (zero-length after trimming)

2. **Parse test expression** (parser.c:2670–2702)
   - Same algorithm as init
   - May be **empty**

3. **Parse update expression** (parser.c:2734–2762)
   - Scan until `))` at paren depth 0
   - May be **empty**

Each expression is stored as a child `NODE_ARITH_EXP` with `val.str` containing the raw text.

### POSIX for loop: mandatory vs optional `in`

**Location:** parser.c:2899–2916

After consuming variable name and checking for `TOK_IN`:

- **If `in` present:** Parse the word list (parser.c:2928–3058)
- **If `in` absent AND next token is `;` / `\n` / `do`:** Implicitly iterate "$@" (parser.c:2904–2916)
  - Parser creates a synthetic NODE_VAR child with val.str = `"$@"`
  - This follows POSIX specification for omitted word list
- **If `in` absent AND next token is something else:** Error

### Word-list reassembly in POSIX for

**Location:** parser.c:2928–3058

The tokenizer splits `NAME=VALUE` constructs into separate tokens (`NAME`, `=`, `VALUE`). The parser reassembles them by:

1. Collecting adjacent tokens without intervening whitespace
2. Handling `WORD=VALUE` patterns (parser.c:2993–3050)
   - If `=` is adjacent to previous token (no whitespace gap), combine
   - If value is adjacent to `=`, append it
   - Otherwise, word ends with `=` (e.g., `empty=`)
3. Creating word nodes with combined strings

This allows `for i in a=1 b=2 c=d=e; do ...` to parse correctly, treating `a=1`, `b=2`, `c=d=e` as three separate words.

### Anonymous function vs subshell disambiguation

**Location:** parser.c:891–920

When the parser encounters `TOK_LPAREN` in a primary position (not after a command name):

1. **Check feature flag:** `FEATURE_ANONYMOUS_FUNCTIONS` must be enabled (zsh mode)
2. **Lookahead 2 tokens:**
   - Peek at next token; if it's `TOK_RPAREN`, save parser position
   - Advance and check token after `)` (peek again)
   - If that token is `TOK_LBRACE`: **anonymous function** `() { body }`
   - Otherwise: **subshell** `( body )`
3. **Restore on mismatch:** If not anonymous function, restore tokenizer position and parse as subshell

This lookahead is **deterministic** and avoids backtracking by checking the 3-token sequence `(`, `)`, `{`.

### `select` statement structure

**Location:** parser.c:3116–3249

Mirrors POSIX `for` syntax:
- Requires variable name immediately after `select`
- Optional `in` clause (parser.c:3150–3206)
- If `in` absent, no implicit word list (unlike `for`)
- Word list parsing (parser.c:3161–3203): stops at `;`, `\n`, `do`, or EOF
- Body parsed with `parse_command_body(parser, TOK_DONE)` (same as for/while/until)
- Trailing redirections allowed (parser.c:3243–3247)

### `time` command parsing

**Location:** parser.c:3261–3292

- Consumes `TOK_TIME` keyword
- Checks for optional `-p` flag (POSIX format); stored as `val.sint`
  - 1 = `-p` present
  - 0 = `-p` absent
- **Parses pipeline, not simple command** (parser.c:3284)
  - Allows: `time ls | grep foo` (pipes a command)
  - Allows: `time (cmd1 && cmd2)` (compound commands)
- Single child: NODE_PIPELINE or NODE_PIPE

### `coproc` command parsing

**Location:** parser.c:3308–3381

**Feature-gated:** Requires `FEATURE_COPROC` flag (bash/zsh mode); else error

**Name detection algorithm** (parser.c:3340–3363):
- After `coproc`, peek at next token
- If it's word-like AND next-next is also word-like OR compound command starter (`{`, `(`, `while`, `until`, `for`, `if`, `case`, `select`), treat current as NAME
- Otherwise, current token starts the command (no explicit name, use default "COPROC")

**Examples:**
- `coproc myproc echo hello` → name="myproc", command=echo
- `coproc { echo hello; }` → name=NULL (default), command={...}
- `coproc myproc { echo; }` → name="myproc", command={...}

**Command:** Parsed via `parse_pipeline()` (parser.c:3370)
- Allows simple commands and pipelines
- Does NOT parse compound commands at top level; they must be recognized by the lookahead in name detection

**Return value:** val.str = coprocess name (or NULL for "COPROC" default)

### Anonymous function parsing

**Location:** parser.c:3394–3412

**Called after lookahead detection** (parser.c:891–920 ensures `() {` sequence)

- Current token is always `TOK_LBRACE` (already verified)
- Calls `parse_brace_group(parser)` to parse body
- Returns NODE_ANON_FUNCTION with brace group as child
- Features require `FEATURE_ANONYMOUS_FUNCTIONS` flag

## Notable behaviors and surprises

### 1. C-style for empty expressions
All three arithmetic expressions (`init`, `test`, `update`) may be **empty strings** (`""`), matching C and bash semantics:
```bash
for ((;;)) do echo loop; done      # All three empty
for ((i=0;;++i)) do ...; done      # Only test empty
for ((;;i++)) do ...; done         # init and test empty
```

### 2. Raw input preservation in for ((;;))
The parser reconstructs expressions from **raw input text** (not tokenizer output) to correctly handle multi-character operators:
```bash
for ((i <= 10;;++i)) do ...  # '<=' preserved, not split to '<' and '='
```
This is critical for later semantic analysis (parser.c:2595–2596 comment explains).

### 3. POSIX for omits `in` → defaults to "$@"
```bash
for i; do               # Equivalent to: for i in "$@"; do
  echo "$i"
done
```
The parser creates a synthetic word list containing `"$@"` (parser.c:2908–2914).

### 4. Word list `=` reassembly in for
The parser accepts and reassembles `WORD=VALUE` constructs by checking for token adjacency:
```bash
for i in a=1 b = 2 c=3 = 4; do echo "$i"; done
# Treats as: [a=1], [b], [=], [2], [c=3], [=], [4]
# (whitespace breaks adjacency)
```

### 5. select without `in` has NO implicit word list
Unlike `for`, `select` does not default to "$@" if `in` is omitted:
```bash
select x; do echo "$x"; done      # Error: select requires word list
select x in a b c; do echo "$x"; done  # OK
```
This is bash/zsh behavior (parser.c:3150–3206 shows no implicit fallback).

### 6. time -p flag is optional
```bash
time ls                  # OK, no -p
time -p ls               # OK, with -p (POSIX format output)
time -p ls | grep foo    # OK, -p with pipeline
```
The `-p` flag is recognized as a word-like token matching literal "-p" (parser.c:3273–3277).

### 7. coproc name detection uses lookahead
```bash
coproc name cmd          # name = "name", cmd = cmd
coproc cmd               # name = NULL, cmd = cmd
coproc name { cmd; }     # name = "name", cmd = { cmd; }
```
The parser must distinguish a NAME from the start of the command. If next-next token is word-like or a compound starter, current is treated as name (parser.c:3350–3358).

### 8. Anonymous functions are zsh-only
The `() { body }` syntax is only recognized when `FEATURE_ANONYMOUS_FUNCTIONS` is enabled (parser.c:893). In non-zsh modes:
```bash
() { echo hi; }          # Parsed as subshell with empty command list
# (Note: "()" is actually "()" subshell, "{" is unexpected)
# Real behavior: syntax error in most modes
```

### 9. Trailing redirections after `do...done`
All loop/control structures accept trailing redirections:
```bash
for i in a b c; do echo "$i"; done > output.txt 2>&1
select x in menu; do ...; done | tee log.txt
```
Parsed by `parse_trailing_redirections()` after the closing `done` (parser.c:2863, 3099, 3243).

### 10. Bash vs Zsh differences in lush
- **C-style for:** Both bash and zsh (parser.c:2582 check is before mode check)
- **select:** Both bash and zsh
- **time -p:** POSIX; recognized by both
- **coproc:** Bash/zsh only (feature-gated, parser.c:3314–3318)
- **Anonymous functions:** Zsh only (feature-gated, parser.c:893)

## Parsing algorithm specifics

### Arithmetic for loop expression extraction

The parser uses a simple algorithm to extract raw expressions while respecting nested parentheses:

```c
int paren_depth = 0;
while (!tokenizer_match(parser->tokenizer, TOK_EOF)) {
    token_t *tok = tokenizer_current(parser->tokenizer);
    
    // Track nesting of ( ) and (( ))
    if (tok->type == TOK_LPAREN || tok->type == TOK_DOUBLE_LPAREN) {
        paren_depth++;
    } else if (tok->type == TOK_RPAREN) {
        paren_depth--;
    } else if (tok->type == TOK_DOUBLE_RPAREN) {
        if (paren_depth > 0) paren_depth -= 2;
        else break;  // End of for (( ))
    }
    
    // Semicolon at depth 0 separates expressions
    if (tok->type == TOK_SEMICOLON && paren_depth == 0) {
        expr_end = tok->position;  // Capture position
        break;
    }
    
    tokenizer_advance(parser->tokenizer);
}

// Extract substring from raw input
if (expr_end > expr_start) {
    size_t len = expr_end - expr_start;
    char *expr = malloc(len + 1);
    memcpy(expr, input + expr_start, len);  // Copy from raw input
    expr[len] = '\0';
    trim_whitespace(expr);
} else {
    expr = strdup("");  // Empty expression
}
```

### Lookahead for anonymous function detection

The parser saves tokenizer state before advancing to check the 3-token sequence `(`, `)`, `{`:

```c
if (current->type == TOK_LPAREN) {
    if (shell_mode_allows(FEATURE_ANONYMOUS_FUNCTIONS)) {
        token_t *next = tokenizer_peek(parser->tokenizer);
        if (next && next->type == TOK_RPAREN) {
            // Save position
            size_t saved_pos = current->position;
            size_t saved_line = parser->tokenizer->line;
            size_t saved_col = parser->tokenizer->column;
            
            tokenizer_advance(parser->tokenizer);  // consume (
            tokenizer_advance(parser->tokenizer);  // consume )
            token_t *after_paren = tokenizer_current(parser->tokenizer);
            
            if (after_paren && after_paren->type == TOK_LBRACE) {
                return parse_anonymous_function(parser);  // It's anon func
            }
            
            // Restore and parse as subshell
            parser->tokenizer->position = saved_pos;
            parser->tokenizer->line = saved_line;
            parser->tokenizer->column = saved_col;
            tokenizer_refresh_from_position(parser->tokenizer);
        }
    }
    return parse_subshell(parser);
}
```

## Error handling

### Expected tokens
All parsing functions use `expect_token()` and `expect_token_with_help()` to enforce:
- `do` after for variable/in-clause and after select/time
- `done` to terminate loops
- `))` to close arithmetic for
- `{` after `()` in anonymous functions

Errors include context-sensitive help messages pointing to expected syntax.

### Feature flags
- `coproc` requires `FEATURE_COPROC` enabled (parser.c:3314–3318)
- Anonymous functions require `FEATURE_ANONYMOUS_FUNCTIONS` enabled (parser.c:893)
- Failures to enable result in syntax errors with feature disabled message

### Token adjacency checks
Word reassembly in POSIX for checks `position` and `length` of tokens to determine whitespace gaps (parser.c:2998–3004, 3023).

## Open questions

1. **Implicit "$@" in for:** Does the synthetic `"$@"` word get special treatment in execution, or is it literally expanded as a string? (Implementation detail; grammar accepts it.)

2. **Empty arithmetic expressions:** Are `for ((;;))` and `for ((;;))` both infinite loops, or does the parser reject empty test expressions? (Code accepts empty; semantic analysis decides.)

3. **coproc command scope:** Can `coproc` wrap a subshell or compound command, or only pipelines? Current code calls `parse_pipeline()`, but lookahead in name detection allows `{`, `(`, etc. (Lookahead inconsistency?)

4. **select behavior without `in`:** Bash 5+ allows `select x; do` to read from `REPLY` and menu; lush requires `in`. Is this intentional?

5. **Trailing redirections on for/select/time/coproc:** Do redirections apply to the entire loop/command, or only the final command in the body? (Likely the entire construct.)

6. **Word list in select:** Does select word list support all expansions (command substitution, arithmetic, variables)? Code suggests yes (lines 3169–3173); same as for.

---

**Document generated:** 2026-04-25
**Section:** 4 (for, select, time, coproc, anonymous functions)
**Parser version:** Hand-written recursive descent in `src/parser.c`
**Node types:** NODE_FOR, NODE_FOR_ARITH, NODE_SELECT, NODE_TIME, NODE_COPROC, NODE_ANON_FUNCTION
