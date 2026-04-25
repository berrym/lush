# Section 3 — Brace Groups, Subshells, if/while/until

## EBNF productions

```
brace_group     = TOKEN_LBRACE separator* command* separator* TOKEN_RBRACE trailing_redir*

subshell        = TOKEN_LPAREN separator* command* separator* TOKEN_RPAREN trailing_redir*

if_statement    = TOKEN_IF logical_expr separator* TOKEN_THEN separator* 
                  if_body (elif_clause)* (else_clause)? 
                  separator* TOKEN_FI trailing_redir*

elif_clause     = TOKEN_ELIF logical_expr separator* TOKEN_THEN separator* if_body
else_clause     = TOKEN_ELSE separator* if_body

if_body         = command_list that halts when seeing TOKEN_ELSE, TOKEN_ELIF, or TOKEN_FI

while_statement = TOKEN_WHILE (simple_cmd | pipeline) separator* TOKEN_DO separator* 
                  command_body(TOK_DONE) separator* TOKEN_DONE trailing_redir*

until_statement = TOKEN_UNTIL (simple_cmd | pipeline) separator* TOKEN_DO separator* 
                  command_body(TOK_DONE) separator* TOKEN_DONE trailing_redir*

command_body(terminator) = logical_expr* that halts when seeing terminator token or EOF

trailing_redir  = (redir_in | redir_out | redir_append | heredoc | herestring | 
                   redir_err | redir_fd | ... )*

separator       = (TOKEN_SEMICOLON | TOKEN_NEWLINE | TOKEN_WHITESPACE)*
```

## AST nodes produced

| Node type | Produced by | Notes |
|-----------|-------------|-------|
| NODE_BRACE_GROUP | parse_brace_group (parser.c:1536) | Contains children as parsed logical expressions. Flat structure—multiple commands are siblings, not wrapped in NODE_COMMAND_LIST. |
| NODE_SUBSHELL | parse_subshell (parser.c:1622) | Contains children as parsed logical expressions. Flat structure—commands are siblings. |
| NODE_IF | parse_if_statement (parser.c:2237) | Flat, alternating structure: [condition, then_body, elif_condition, elif_body, ..., else_body]. elif chains are **flat** (all conditions and bodies are direct children of the single NODE_IF node), not recursive. |
| NODE_WHILE | parse_while_statement (parser.c:2388) | Children: [condition, body]. Condition parsed as simple_cmd or pipeline (not full logical_expr). Body is result of parse_command_body(TOK_DONE). |
| NODE_UNTIL | parse_until_statement (parser.c:2481) | Children: [condition, body]. Identical condition/body structure to WHILE. |
| NODE_COMMAND_LIST | parse_if_body (parser.c:599) | Used only for if/elif/else bodies. Wraps multiple commands. **Not** used by brace_group or subshell. |
| NODE_REDIR_* | parse_trailing_redirections (parser.c:1734) | Children of NODE_BRACE_GROUP, NODE_SUBSHELL, NODE_IF, NODE_WHILE, NODE_UNTIL when trailing redirections are present. |

## Context-sensitive behavior and lexer interactions

### Token reservation and keyword recognition

- **`then`, `else`, `elif`, `fi`, `do`, `done`**: Tokenized as reserved keywords **unconditionally** (see tokenizer.h:87–104). The tokenizer has no context-sensitive keyword disabling for these. They are always token types TOK_THEN, TOK_ELSE, etc.

- **`{` and `}`**: Tokenized as distinct token types TOK_LBRACE and TOK_RBRACE (tokenizer.c:1800, 1806). These are **single-character tokens** that do not require special whitespace handling—the tokenizer consumes them as individual tokens.

- **Keyword disabling for redirections**: The parser **disables keyword recognition around redirection targets** (parser.c:1831–1839). This is to allow filenames like `in`, `do`, `done`, `fi` to be treated as ordinary words in redirections. However, this affects only the redirection target itself, not the core reserved words in control structures.

### Whitespace requirements around `{` and `}`

- **`{` must be tokenizable**: The `{` token must appear as a separate token in the token stream. There is **no special requirement** that it be preceded or followed by whitespace—the tokenizer treats `{` as a complete token unit, one character wide.

- **Trailing `}`**: The closing `}` in a brace group must be a separate token (parser.c:1593–1598, expect_token_with_help). Like `{`, it has no whitespace prerequisites.

- **Practical note**: In shell syntax, `cmd}` would tokenize as a single WORD token containing both "cmd" and "}", so braces genuinely must be separated. But this is a tokenizer behavior, not an explicit parser whitespace rule.

### Separator handling

All control structures call `skip_separators()` before and after parsing bodies/conditions:
- After opening `{`, `(`, `if`/`while`/`until`, `then`, `else`, `do`: separators are skipped (parser.c:1569, 1655, 2265–2276, 2440–2441, 2528–2529, etc.)
- After closing `}`, `)`, `fi`, `done`: separators are skipped, and then trailing redirections are parsed (parser.c:1589, 1675, 2357–2358, 2452–2453, 2540–2541)
- **Separators include**: semicolons, newlines, and whitespace (skip_separators is context-agnostic)

### `elif` chaining

The `elif` chain is **flat**, not recursive. `parse_if_statement` builds a single NODE_IF with alternating children:
```
NODE_IF
├─ condition (from "if")
├─ then_body (NODE_COMMAND_LIST)
├─ condition (from first "elif")
├─ elif_body (NODE_COMMAND_LIST)
├─ condition (from second "elif")
├─ elif_body (NODE_COMMAND_LIST)
└─ else_body (NODE_COMMAND_LIST) [optional]
```

This is not three nested NODE_IF nodes; all conditions and bodies are direct children of a single root if node. The structure is appended to as each `elif` is encountered in the while loop (parser.c:2297–2339).

### `if` body termination

`parse_if_body()` (parser.c:599–639) creates a NODE_COMMAND_LIST and parses logical expressions until it sees TOKEN_ELSE, TOKEN_ELIF, TOKEN_FI, or TOKEN_EOF. It **does not consume** the terminator—the terminator is left in the token stream for the outer if_statement parser to handle.

`while`/`until` bodies, by contrast, use `parse_command_body(TOK_DONE)` (parser.c:553–588), which is a different loop that also stops before the terminator token. Both functions halt before, not after, consuming the terminator.

## Notable behaviors and surprises

### Condition parsing in `while` and `until`

**Lush restricts while/until conditions to simple_cmd or pipeline, not full logical_expr:**
```c
// parser.c:2411–2416
if (tokenizer_match(parser->tokenizer, TOK_LBRACKET)) {
    condition = parse_simple_command(parser);
} else {
    condition = parse_pipeline(parser);
}
```

This means `while a && b; do ...` would **not** parse as expected—it would parse `a` as the condition and treat `&& b; do` as part of the body. This differs from bash/zsh, which allow logical operators in conditions. (Likely intentional to avoid ambiguity with body parsing, since both use the same separator-delimited syntax.)

### Brace group and subshell command structure

Both NODE_BRACE_GROUP and NODE_SUBSHELL parse commands directly as siblings:
```c
// parser.c:1575–1586 (brace group)
while (!tokenizer_match(parser->tokenizer, TOK_RBRACE) ...) {
    node_t *command = parse_logical_expression(parser);
    add_child_node(group_node, command);  // Direct child, not in a list
    skip_separators(parser);
}
```

They **do not** wrap multiple commands in a NODE_COMMAND_LIST. Compare to `if` bodies, which are wrapped in NODE_COMMAND_LIST. This asymmetry means the AST shape differs:
- `{ a; b; }` → NODE_BRACE_GROUP with two logical_expr children
- `if true; then a; b; fi` → NODE_IF with then_body=NODE_COMMAND_LIST([a, b])

### Trailing redirections on compound structures

All compound structures (brace_group, subshell, if, while, until) support trailing redirections:
```
{ cmd; } > out.txt
(cmd) 2>&1
if ...; fi < input
while ...; done >&2
```

These are parsed by `parse_trailing_redirections()` (parser.c:1734–1751) and appended as child nodes of the compound structure. The redirection nodes are appended *after* the body/commands.

### Error behavior on missing delimiters

The parser is **not** permissive about missing delimiters:
- Missing `}` → error (parser.c:1593–1598)
- Missing `)` → error (parser.c:1679–1684)
- Missing `then` → error (parser.c:2268–2272)
- Missing `fi` → error (parser.c:2362–2366)
- Missing `do` → error (parser.c:2433–2437)
- Missing `done` → error (parser.c:2455–2459)

The error messages are descriptive and provide help context.

### Optional semicolons before terminators

The parser **optionally consumes** semicolons before `elif`, `else`, `fi` (parser.c:2288–2290, 2333–2335), but does **not** require them. The grammar allows:
```
if cond; then body fi
if cond; then body; elif ...
if cond; then body; else body fi
```

### Separator loop in `while` and `until` conditions

Unlike `if` (which parses the condition as a single logical_expr), `while` and `until` parse the condition as `simple_cmd | pipeline` and then expect `do`. There is **no logical operator** allowed in while/until conditions in this parser. This is a **lush-specific restriction** (see "Notable behaviors" above).

## Open questions

1. **Why is `if_body` a NODE_COMMAND_LIST while brace_group/subshell commands are naked siblings?** Is this inconsistency intentional for semantic clarity, or should all three use the same approach?

2. **Can conditions in `while`/`until` be extended to full logical_expr?** The current restriction to simple_cmd/pipeline avoids ambiguity, but is it a design choice or a limitation?

3. **How does the parser handle `{ }` with no space (e.g., `{cmd;}`)?** The tokenizer treats `{` as a single character, but does parse_brace_group require the opening `{` to be consumed as a separate token? (Answer: yes, it does, since tokenizer makes it a distinct token.)

4. **What is the maximum depth of nested elif clauses?** The flat AST structure suggests no hard limit, but are there recursion depth protections? (The parser does have stack-overflow protection via parser_enter_recursion/parser_exit_recursion, but elif is loop-based, not recursive.)

5. **Are there any edge cases with here-documents inside `if`/`while` bodies?** The parser parses commands as full logical expressions, which can include pipelines with redirections, but here-document handling may have subtle interactions.
