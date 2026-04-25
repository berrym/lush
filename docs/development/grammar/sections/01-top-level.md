# Section 1 — Top-Level Grammar

Covers `parser_parse`, `parser_parse_command_line`, `skip_separators`, `parse_command_body`, `parse_if_body`, `parse_command_list`, `parse_logical_expression`, `parse_pipeline` (parser.c:496–866).

## EBNF productions

```
program            = { wsp | comment | newline } [ command_list ]

command_list       = logical_expression { separator logical_expression } [ separator ]
                   ; (§1) parse_command_list — does NOT consume control-structure
                   ;     terminators (TOK_DONE, TOK_FI, TOK_ELSE, TOK_ELIF)

separator          = ( ";" | newline ) { ";" | newline | wsp | comment }

logical_expression = pipeline { ( "&&" | "||" ) separator pipeline }
                   ; (§1) left-associative; `&&` and `||` share precedence

pipeline           = [ "!" wsp ] simple_command_or_compound
                     { ( "|" | "|&" ) [ newline ] simple_command_or_compound }
                     [ "&" ]
                   ; (§1) `!` is tokenized as TOK_WORD with text=="!", length==1
                   ; "&" backgrounds the entire pipeline

command_body       = { separator } [ logical_expression { separator logical_expression } { separator } ]
                   ; (§1) parameterized by terminator token; stops without consuming it

if_body            = { separator } [ logical_expression { separator logical_expression } { separator } ]
                   ; (§1) commands wrapped in NODE_COMMAND_LIST; stops at
                   ;     TOK_ELSE, TOK_ELIF, or TOK_FI

simple_command_or_compound = simple_command (§2)
                           | brace_group (§3) | subshell (§3)
                           | if_statement (§3) | while_statement (§3)
                           | until_statement (§3) | for_statement (§4)
                           | select_statement (§4) | time_command (§4)
                           | coproc (§4) | anonymous_function (§4)
                           | case_statement (§5) | function_definition (§6)
                           | arithmetic_command (§7) | extended_test (§7)
```

Terminal token references: `;` = TOK_SEMICOLON, `&&` = TOK_LOGICAL_AND, `||` = TOK_LOGICAL_OR, `|` = TOK_PIPE, `|&` = TOK_PIPE_STDERR, `&` = TOK_AND, `wsp` = TOK_WHITESPACE, `newline` = TOK_NEWLINE, `comment` = TOK_COMMENT.

## AST nodes produced

| Node type | Produced by | Notes |
|---|---|---|
| NODE_LOGICAL_AND | `parse_logical_expression` (parser.c:679–680) | Binary, left-associative chain |
| NODE_LOGICAL_OR | `parse_logical_expression` (parser.c:679–680) | Binary, same precedence as `&&` |
| NODE_PIPE | `parse_pipeline` (parser.c:805–816) | `val.sint`: 0 = `\|`, 1 = `\|&` |
| NODE_BACKGROUND | `parse_pipeline` (parser.c:827–835) | Wraps the entire pipeline |
| NODE_NEGATE | `parse_pipeline` (parser.c:840–848) | Wraps post-pipeline; binds tighter than `\|` and `&` |
| NODE_COMMAND_LIST | `parse_if_body` (parser.c:601–639) | Wrapper for if-statement bodies — children, not siblings |

`parse_command_list` and `parse_command_body` build sibling chains; `parse_if_body` is the asymmetric exception (uses NODE_COMMAND_LIST as a parent wrapper).

## Context-sensitive behavior and lexer interactions

**Keyword-mode flag.** The tokenizer carries `enable_keywords`. `TOK_DONE`, `TOK_FI`, `TOK_ELSE`, `TOK_ELIF` are only produced when this flag is set. Top-level body parsers rely on this — if keyword mode were off, terminators would arrive as TOK_WORD and control structures would never close. See §9 for the full mode story.

**Negation as a special-cased word.** There is no dedicated TOK_NOT. `parse_pipeline` recognizes negation only when the next token is TOK_WORD with `text[0] == '!'` and `length == 1` (parser.c:768–778). The tokenizer must therefore emit `!` as a solo token; merging it with a following word breaks negation.

**Multiline pipelines, asymmetric.** After a pipe operator the parser explicitly consumes TOK_NEWLINE / TOK_WHITESPACE (parser.c:792–796). It does *not* consume newlines *before* a pipe. Practical consequence:

```
cmd1 |          # OK — newline after `|` is swallowed
  cmd2

cmd1            # NOT a pipeline — newline ends the first expression
| cmd2          #   `| cmd2` then fails to parse as a fresh statement
```

**Terminator etiquette.** `parse_command_list` (parser.c:739–746) stops at control-structure terminators but leaves them on the token stream for the enclosing control-structure parser to consume. Each control-structure parser is therefore responsible for eating its own `done` / `fi` / `esac`.

**Recursion guard.** `parse_logical_expression` (parser.c:651–654) and `parse_pipeline` (parser.c:762–765) bracket their work with `parser_enter_recursion()` / `parser_exit_recursion()`. Max depth 256 (parser.h:35). Exceeding the cap sets a parser error rather than crashing.

**Separator-skip inconsistency.** Three separator-skipping idioms coexist:
- `skip_separators()` (parser.c:534) — `;`, `\n`, whitespace, comments
- Inline loop in `parse_command_list` (parser.c:712–716) — `;`, `\n`, comments (no whitespace)
- Inline calls inside `parse_if_body` (parser.c:612–613, 635) — uses `skip_separators`

Functionally equivalent for typical inputs, but a fuzzer can probably find input where the difference is observable.

## Notable behaviors and surprises

1. **Empty input is valid.** `parser_parse` returns NULL with no error on empty / whitespace-only input. Callers must tolerate NULL.
2. **`&&` and `||` have equal precedence, left-associative.** `a && b || c && d` parses as `(((a && b) || c) && d)` — same as bash.
3. **Negation binds tighter than `|` and `&`.** `! cmd | cmd2` parses as `(! cmd) | cmd2`. `cmd1 && cmd2 &` backgrounds only `cmd2`.
4. **`if_body` wraps in NODE_COMMAND_LIST; `command_body` does not.** Two different shapes for "body of a compound command". This is documented as an open question — the asymmetry is real.
5. **No error recovery beyond `return NULL`.** Lines 571–575 and 724–728 free the partial AST on error. Errors surface through `parser_error()` / `parser_has_error()` (parser.h).

## Open questions

1. Why does `parse_if_body` use NODE_COMMAND_LIST while `parse_command_body` builds sibling chains? Intentional asymmetry for executor convenience, or legacy?
2. Can a command literally named `!` (e.g., aliased) be invoked, or does the negation lookahead always claim it?
3. Is the separator-skipping inconsistency intentional (e.g., for comment placement inside compound bodies vs at top level)?
4. Are there inputs where the missing "newline before `|`" handling is a usability problem worth fixing?
