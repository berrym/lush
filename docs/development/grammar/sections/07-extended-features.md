# Section 7 -- Extended Features

Covers `parse_arithmetic_command`, `parse_array_literal`, `parse_extended_test`, `parse_process_substitution` (parser.c:4101-end of file).

The architectural pattern in this section is the dominant story: rather than spinning up sub-grammars for `(( ))` and `[[ ]]`, the parser **collects raw tokens between the delimiters and reconstructs an expression string**, deferring real parsing to the runtime evaluator. Process substitution is the exception -- it is recognized at the lexer.

## EBNF productions

### Arithmetic command

```
arithmetic_command = "((" arith_token_seq "))"
                   ; (S7) inner content is collected verbatim and reconstructed
                   ;     as a string in NODE_ARITH_CMD.val.str

arith_token_seq    = { TOKEN }   ; until matching TOK_DOUBLE_RPAREN at paren_depth 0
```

Recognized when `parse_simple_command` sees TOK_DOUBLE_LPAREN (parser.c:879-881). Feature-gated on `FEATURE_ARITH_COMMAND`.

### Array literals

```
array_literal      = "(" { array_element [ separator ] } ")"
                   ; (S7) ONLY recognized in assignment context
                   ;     (after "=" or "+=" in an assignment)

array_element      = "[" index_token_seq "]" "=" value_token
                   | value_token

value_token        = WORD | STRING | EXPANDABLE_STRING | VARIABLE
                   | ARITH_EXP | COMMAND_SUB

separator          = whitespace | newline
```

Recognized after `=` or `+=` in scalar/array assignment (parser.c:1080-1098, 1313-1337).

### Extended test

```
extended_test      = "[[" test_token_seq "]]"
                   ; (S7) inner content reconstructed as a string in
                   ;     NODE_EXTENDED_TEST.val.str

test_token_seq     = { TOKEN }   ; until matching TOK_DOUBLE_RBRACKET at paren_depth 0
                   ; regex-mode flag (set after =~, cleared at && / ||)
                   ; controls space insertion during reconstruction
```

Recognized when `parse_simple_command` sees TOK_DOUBLE_LBRACKET (parser.c:885-887). Feature-gated on `FEATURE_EXTENDED_TEST`.

### Process substitution

```
process_substitution_in  = "<(" command_list (S1) ")"
process_substitution_out = ">(" command_list (S1) ")"
```

Tokenized as TOK_PROC_SUB_IN / TOK_PROC_SUB_OUT at the lexer (tokenizer.c:1335-1340, 1414-1419). Parsed wherever a word can appear in a simple command, plus as a redirection target. Feature-gated on `FEATURE_PROCESS_SUBSTITUTION` (parser.c:4625).

## AST nodes produced

| Node type | Produced by | Notes |
|---|---|---|
| NODE_ARITH_CMD | `parse_arithmetic_command` (parser.c:4115) | `val.str` = reconstructed expression |
| NODE_ARRAY_LITERAL | `parse_array_literal` (parser.c:4252) | Children = element nodes |
| NODE_EXTENDED_TEST | `parse_extended_test` (parser.c:4453) | `val.str` = reconstructed test expression |
| NODE_PROC_SUB_IN | `parse_process_substitution` (parser.c:4633) | `val.str` = `"<("`; children = inner commands |
| NODE_PROC_SUB_OUT | `parse_process_substitution` (parser.c:4633) | `val.str` = `">("`; children = inner commands |
| NODE_ARRAY_ASSIGN | parent of array literal in scalar assignment context (parser.c:1089) | |
| NODE_ARRAY_APPEND | parent for `+=(...)` form | |

## Context-sensitive behavior and lexer interactions

**Arithmetic -- no separate tokenizer.** `parse_arithmetic_command` (parser.c:4101-4225) walks the normal token stream until it reaches TOK_DOUBLE_RPAREN at `paren_depth == 0`. Each token's text is appended to a buffer with heuristic spacing:
- Spaces are inserted between most adjacent tokens.
- Operator-adjacent tokens skip the space (parser.c:4171-4189) -- checks whether token text begins/ends with an operator character.
- Nested `(` increments `paren_depth`; nested `)` decrements it (parser.c:4147-4153).
- The reconstructed string is trimmed (parser.c:4209-4219) and stored on NODE_ARITH_CMD.

The reconstructed string is not parsed during `parse_arithmetic_command` -- that is the runtime evaluator's job (see open questions).

**Array literal -- disambiguation from subshell is contextual.** TOK_LPAREN at simple-command position is *always* a subshell (parser.c:919). The literal is only entered when `parse_simple_command` is already mid-assignment and sees `=` followed by `(`:
- Scalar/positional assignment context, parser.c:1080-1098.
- Builtin-argument context (`declare -A map=(...)`) via word + `=` lookahead, parser.c:1313-1337.

There is therefore no ambiguity in the grammar -- the lookahead/state in `parse_simple_command` decides which production fires.

**Extended test -- same reconstruction trick, plus regex mode.** `parse_extended_test` (parser.c:4438-4589) is structurally similar to the arithmetic command:
- Walks tokens until TOK_DOUBLE_RBRACKET at depth 0 (parser.c:4483-4485).
- Tracks `paren_depth` for grouping parens inside the test (parser.c:4495-4501).
- Maintains an `in_regex` flag: set true after TOK_REGEX_MATCH (`=~`) at parser.c:4556, reset on `&&` / `||` at depth 0 (parser.c:4489-4492).
- When `in_regex` is true, **no space is inserted between tokens** (parser.c:4522). This preserves regex literals like `^hello$` that the tokenizer split into pieces.
- Otherwise spacing rules mirror arithmetic -- skip spaces around operators, parens (parser.c:4525-4544).

The operator vocabulary (`==`, `!=`, `=~`, `<`, `>`, `-z`, `-n`, `-f`, `-d`, ...) is **not** specially tokenized inside `[[ ]]`. Most file-test operators arrive as TOK_WORD; comparison operators arrive as TOK_REDIRECT_IN / TOK_REDIRECT_OUT / TOK_ASSIGN. Reconstruction puts them back into a string for the runtime test evaluator.

**Process substitution -- lexer-level recognition.** TOK_PROC_SUB_IN and TOK_PROC_SUB_OUT are produced by the tokenizer when it sees `<(` or `>(`. The parser:
- Recognizes them as command arguments (parser.c:1300-1309).
- Recognizes them as redirection targets (parser.c:1864-1875).
- Inside `parse_process_substitution`, calls `parse_logical_expression` repeatedly until TOK_RPAREN (parser.c:4647-4663). The body is a full command list, so nested constructs (pipelines, control structures) work.

Because the tokenizer commits to the two-character operator, there is no paren-depth game inside process substitution -- closing `)` is unambiguous.

## Notable behaviors and surprises

1. **Reconstruction loses original formatting.** `((a+b+c))` becomes `"a + b + c"` in `NODE_ARITH_CMD.val.str`. Functionally equivalent for evaluation, but means the AST cannot perfectly round-trip the source.
2. **Operator-spacing heuristic is a best-effort.** It works for the common cases but is text-pattern-based; pathological inputs (operator-named identifiers, exotic Unicode in word tokens) could trip it.
3. **`!` in `[[ ]]` is tokenized as TOK_WORD.** No dedicated TOK_NOT -- the test reconstructor treats `!` as just another token.
4. **Element types in array literals are limited.** `parse_array_literal` only emits typed children for WORD/STRING/EXPANDABLE_STRING/VARIABLE/ARITH_EXP/COMMAND_SUB. Unknown token types are skipped (parser.c:4398) -- including a nested `(`, so `arr=(a (b c) d)` does *not* produce a nested array.
5. **Indexed array elements are stringified.** `[index]=value` is collected as a single string `"[index]=value"` and stored on a child node (parser.c:4343-4356). The index is not pre-evaluated at parse time.
6. **Regex spaces survive only after `=~`.** `[[ a =~ ^x && b == c ]]` works correctly: regex mode while collecting `^x`, normal mode after the `&&`.
7. **No precedence parsing inside `[[ ]]`.** Operators are concatenated into the test string verbatim; the runtime evaluator owns precedence and arity.
8. **Process substitution accepts arbitrary command lists.** `<(for i in 1 2 3; do echo $i; done)` works because the body is parsed by `parse_logical_expression`.
9. **Process substitution as a redirection target.** `<` followed immediately by TOK_PROC_SUB_IN, and `>` followed by TOK_PROC_SUB_OUT, are explicitly handled as redirection forms (parser.c:1864-1875).
10. **Feature gates short-circuit at parse time.** Disabling `FEATURE_ARITH_COMMAND`, `FEATURE_EXTENDED_TEST`, or `FEATURE_PROCESS_SUBSTITUTION` causes the parser to refuse the construct outright, not silently downgrade.

## Open questions

1. **Where is the arithmetic string evaluated?** `NODE_ARITH_CMD.val.str` is a reconstructed expression; some runtime path must tokenize and evaluate it. Worth documenting that path so the full grammar of arithmetic expressions can be captured for fuzzing.
2. **Where is the extended-test string evaluated?** Same question for `NODE_EXTENDED_TEST.val.str`.
3. **Operator-character heuristic edge cases.** If the tokenizer emitted a word starting with `+`, the spacing logic might glue it to a previous token unexpectedly. Worth a fuzzer probe.
4. **Array element expansion.** Does `arr=( $(echo a b c) )` field-split the command substitution into three elements at parse time, or does that happen at execution? Code in `parse_array_literal` does not appear to split.
5. **Nested process substitution.** `<(cat <(echo nested))` -- the inner `)` closes the inner `<(`, the outer `)` closes the outer. Because the body is parsed by `parse_logical_expression`, nesting should work, but it is not exercised by the code in this section directly.
6. **Regex tokenization edge cases.** A regex containing `]]` (improbable but constructible) would terminate the test prematurely because the parser only checks for TOK_DOUBLE_RBRACKET at depth 0, not regex-aware matching.
