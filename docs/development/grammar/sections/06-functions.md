# Section 6 -- Function Definitions

Covers `is_function_definition`, `is_valid_posix_function_name`, `parse_function_definition` (parser.c:3752-4100).

## EBNF productions

```
function_definition  = posix_function_form
                     | keyword_function_form

posix_function_form  = NAME "(" [ parameters ] ")" brace_group_body

keyword_function_form
                     = "function" NAME [ "(" [ parameters ] ")" ] brace_group_body
                     ; (S6) parens are optional iff next token is "{"

parameters           = parameter { "," parameter }

parameter            = NAME [ "=" default_value ]
                     ; (S6) lush extension; not POSIX, not bash

default_value        = WORD | STRING | EXPANDABLE_STRING

brace_group_body     = "{" command_list (S1) "}"
                     ; (S6) body MUST be a brace group; subshell, if, etc. NOT accepted
```

Where:
- `NAME` is a TOK_WORD (or other word-like token -- see context below)
- `"function"` is TOK_FUNCTION
- `"("` / `")"` are TOK_LPAREN / TOK_RPAREN
- `"{"` / `"}"` are TOK_LBRACE / TOK_RBRACE
- `","` / `"="` are TOK_COMMA / TOK_ASSIGN

## AST nodes produced

| Node type | Produced by | Notes |
|---|---|---|
| NODE_FUNCTION | `parse_function_definition` (parser.c:3835) | `val.str` = function name; if parameters present, encoded as `NAME\|PARAMS{name1,name2=default,...}` (parser.c:3995-4030). Body is the first child. |
| (body children) | `parse_logical_expression` calls inside the body | Sibling-chain inside the NODE_FUNCTION |
| NODE_REDIR_* | `parse_trailing_redirections` (theoretically) | See **open question** below -- `parse_function_definition` does not appear to call this directly. |

## Context-sensitive behavior and lexer interactions

**Two-token lookahead for `name()` form.** `is_function_definition` (parser.c:3752-3769) is the gatekeeper -- `parse_simple_command` (parser.c:956) calls it. Pattern:
- Current token is "word-like" -- TOK_WORD, TOK_STRING, TOK_EXPANDABLE_STRING, TOK_NUMBER, or TOK_VARIABLE per `token_is_word_like()` (tokenizer.c:385-389).
- Peeked next token is TOK_LPAREN.

If both true, `parse_simple_command` hands off to `parse_function_definition`; otherwise the same tokens are parsed as a simple command.

**`function` keyword path.** When `parse_simple_command` sees TOK_FUNCTION (parser.c:939), it dispatches to `parse_function_definition` unconditionally. Inside:
- parser.c:3817-3821 -- consume TOK_FUNCTION, set `has_function_keyword = true`.
- parser.c:3826 -- next token must be word-like (the name).
- parser.c:3866-3868 -- if `has_function_keyword && current == TOK_LBRACE`, skip parens entirely. This is the `function name { body }` ksh/bash form.
- parser.c:3872-3877 -- otherwise, consume the mandatory `(`.

**Name validation.** `is_valid_posix_function_name` (parser.c:3780-3798) gates names when `is_posix_mode_enabled()` returns true (parser.c:3850):
- First char: `isalpha(c) || c == '_'`
- Remaining chars: `isalnum(c) || c == '_'`

When POSIX mode is **off**, no name validation runs (parser.c:3842-3847). Any word-like token is accepted as a name. This includes numbers (`0` as a function name) and other characters that survived word tokenization.

**Body must be a brace group.** parser.c:4032-4083 hard-requires `{` to open the body and `}` to close it. Subshells, `if` statements, and other compound commands as bodies are not accepted, even though POSIX permits this.

## Notable behaviors and surprises

1. **Permissive non-POSIX names.** Outside POSIX mode there is *no* name validation. `123() { echo hi; }` parses. So does any other word-like token. Whether the executor honors such names is a separate matter.
2. **Three parens-related forms.** `name() {...}`, `function name() {...}`, and `function name {...}` are all accepted. The keyword form makes parens optional only if `{` immediately follows.
3. **Parameter syntax is a lush extension.** `function add(a, b=0) { ... }` is neither POSIX nor bash. Parameters are flattened to a string suffix on the function-name field -- there is no structured parameter list on the AST node (parser.c:3995-4030).
4. **Parameter defaults are restricted.** Default values must be word, string, or expandable-string tokens (parser.c:3915-3926). Arithmetic, command substitution, and other complex expressions are rejected as defaults.
5. **No body validation at parse time.** Anything `parse_logical_expression` accepts inside `{ }` is accepted. Semantic errors (undefined names, syntax-correct-but-wrong constructs) surface at execution.
6. **Body is brace-group only.** `f() ( echo subshell )` and `f() if true; then ...; fi` are *not* accepted -- bash accepts both.

## Open questions

1. **Trailing redirections on function definitions.** Other compound commands route through `parse_trailing_redirections` (parser.c:1734). `parse_function_definition` does not appear to call it at line ~4087. Does `f() { ...; } > /dev/null` actually work, and where is that handled?
2. **Parameter encoding.** `NAME|PARAMS{...}` in `val.str` is brittle. Is there a runtime parser that decodes it back? Should NODE_FUNCTION carry a structured parameter list as a sibling of the body instead?
3. **Reserved-word names in non-POSIX mode.** If the tokenizer emits `if` as TOK_IF (a keyword), `is_function_definition` rejects it (TOK_IF is not word-like). But what about names that are word-like in some contexts and keyword-like in others? Does the keyword-disable flag interact?
