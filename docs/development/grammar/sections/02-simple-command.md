# Section 2 -- Simple Commands

## Overview

`parse_simple_command()` (parser.c:867-1526) is the largest parser function, handling the most complex grammar construct in lush: transforming words into typed AST nodes, recognizing variable assignments, detecting command-position reserved words, and dispatching to compound-command parsers. This 668-line function is the gateway through which nearly all input flows.

The function's structure reflects shell semantics: **context matters**. The same token sequence can be a command name, an assignment target, or an argument depending on position and surrounding tokens.

## EBNF Productions

```ebnf
simple_command = compound_command
               | ( [ assignment { assignment } ] command_name { argument } { redirection } )
               | assignment { assignment }

assignment = NAME "=" [ value ]                     (* scalar assignment *)
           | NAME "+=" [ "(" { value } ")" ]        (* array append (if FEATURE_INDEXED_ARRAYS) *)
           | NAME "=" "(" { value } ")"             (* array literal assignment *)
           | NAME "[" subscript "]" ("=" | "+=") value

command_name = WORD | KEYWORD

argument = WORD | STRING | EXPANDABLE_STRING | VARIABLE
         | ARITH_EXP | COMMAND_SUB | BACKQUOTE | KEYWORD
         | array_arg
         | process_substitution
         | string_concat                            (* adjacent tokens without whitespace *)

array_arg = NAME ("=" | "+=") "(" { value } ")"   (* as argument to builtin *)

redirection = redir_token target
            | fd_number redir_token target
            | "{" NAME "}" redir_token target       (* fd allocation *)

compound_command = "{" (S 3 brace_group) "}"
                 | "(" (S 3 subshell) ")"
                 | "((" (S 1 arith_command) "))"
                 | "[[" (S 2 extended_test) "]]"
                 | "()" "{" (S 7 anon_function) "}"
                 | "if" (S 4 if_statement) "fi"
                 | "while" (S 4 while_statement) "done"
                 | "until" (S 4 until_statement) "done"
                 | "for" (S 4 for_statement) "done"
                 | "case" (S 4 case_statement) "esac"
                 | "function" (S 8 function_definition) [ "{" ... "}" ]
                 | "time" [ "-p" ] (S 5 pipeline)
                 | "coproc" [ NAME ] (S 5 coproc)
                 | "select" (S 5 select_statement) "done"
                 | NAME "(" ")"                      (* bash-style function def *)
```

## AST Nodes Produced

| Node type | Produced by | Notes |
|-----------|------------|-------|
| `NODE_COMMAND` | regular command (word + args) or scalar assignment | val.str holds command name or "name=value" string |
| `NODE_VAR` | argument expansion, subscript in array access | Plain text reference, may contain $, ${}, etc. |
| `NODE_STRING_LITERAL` | single-quoted or TOK_STRING token | No expansion, quotes preserved in value |
| `NODE_STRING_EXPANDABLE` | double-quoted string or concatenated tokens | Variable expansion required at execution |
| `NODE_ARITH_EXP` | $((expr)) token | Arithmetic expression, pass to evaluator |
| `NODE_COMMAND_SUB` | $(cmd) or backtick token | Command substitution, capture output |
| `NODE_ARRAY_ASSIGN` | arr[n]=value or arr=(a b c) | var name in val.str, children: subscript, value |
| `NODE_ARRAY_APPEND` | arr+=(a b c) | Append elements to array (FEATURE_INDEXED_ARRAYS) |
| `NODE_REDIR_*` | redirection tokens (>, <, >>, etc.) | Redirect I/O, children carry fd and target |
| `NODE_PROC_SUB_IN`, `NODE_PROC_SUB_OUT` | <(cmd), >(cmd) | Process substitution (FEATURE_PROCESS_SUBSTITUTION) |
| `NODE_BRACE_GROUP` | { ... } | Executed in current shell, no new process |
| `NODE_SUBSHELL` | ( ... ) | Executed in subshell, new process |
| `NODE_ARITH_CMD` | (( ... )) | Arithmetic command (FEATURE_ARITH_COMMAND) |
| `NODE_EXTENDED_TEST` | [[ ... ]] | Extended test command (FEATURE_EXTENDED_TEST) |
| `NODE_ANON_FUNCTION` | () { ... } | Anonymous function (FEATURE_ANONYMOUS_FUNCTIONS) |
| `NODE_IF`, `NODE_WHILE`, etc. | if/while/for/case/function/select/time/coproc | Dispatched to specific compound handlers |

## Context-Sensitive Behavior and Lexer Interactions

### 1. Word vs. Assignment Discrimination

**Line 960-1206: Assignment Detection**

The parser looks ahead after a `WORD` token to detect assignments:
- If next token is `TOK_ASSIGN` (`=`) or `TOK_PLUS_ASSIGN` (`+=`), treat the word as a variable name.
- If next is `TOK_LPAREN`, check what follows:
  - If `(` leads to array literal `( elements )`, parse as `NODE_ARRAY_ASSIGN` or `NODE_ARRAY_APPEND`.
  - If `(` is followed by `)`, and then `{`, this is an anonymous function definition (lines 891-920).
  - If `(` appears in the word text itself (e.g., `arr[0]`), parse as array element assignment (lines 963-1063).
- Otherwise, treat as command name.

**Key distinction (lines 1137-1142):** If a word is followed by `=`, it's a new assignment, not a value for the current assignment. The parser stops concatenating when it peeks and sees `=` on the next word.

### 2. Variable Assignment Forms

**Scalar Assignment (lines 1066-1205):**
```
NAME=value
NAME+=value (append in arithmetic context, or for arrays)
NAME=          (empty value, creates empty string or variable)
```

Both `=` and `+=` are recognized. The value can be:
- Empty (line 1193)
- A single token (WORD, VARIABLE, ARITH_EXP, COMMAND_SUB, BACKQUOTE)
- Multiple adjacent tokens concatenated (lines 1128-1179)

Quotes are preserved for single-quoted strings (lines 1165-1169) to prevent expansion; double-quoted strings are concatenated without quotes (line 1172).

**Array Element Assignment (lines 963-1063, requires FEATURE_INDEXED_ARRAYS):**
```
arr[subscript]=value
arr[subscript]+=value
```

Tokenizer produces `arr[subscript]` as a single WORD token with brackets embedded. Parser extracts:
- Variable name (text before `[`)
- Subscript expression (text between `[` and `]`)
- Assignment operator (`=` or `+=`)
- Value (next token or concatenated sequence)

Produces `NODE_ARRAY_ASSIGN` with var name in val.str, children: subscript node, value node.

**Array Literal Assignment (lines 1080-1098, requires FEATURE_INDEXED_ARRAYS):**
```
arr=(elem1 elem2 elem3)
arr+=(elem1 elem2 elem3)
```

When `=` or `+=` is followed by `(`, call `parse_array_literal()` to parse the parenthesized list. Creates `NODE_ARRAY_ASSIGN` or `NODE_ARRAY_APPEND` with array literal as child.

### 3. Command-Position Recognition

**Lines 873-951: Compound Command Dispatch**

Detection is **token-driven**: the parser checks the **current** token type before consuming any word. Dispatch happens in priority order:

1. **`{`** (TOK_LBRACE, line 874) -> `parse_brace_group()`
2. **`((`** (TOK_DOUBLE_LPAREN, line 879, requires FEATURE_ARITH_COMMAND) -> `parse_arithmetic_command()`
3. **`[[`** (TOK_DOUBLE_LBRACKET, line 885, requires FEATURE_EXTENDED_TEST) -> `parse_extended_test()`
4. **`(`** (TOK_LPAREN, line 891):
   - Lookahead check: if next is `)` and then `{`, and FEATURE_ANONYMOUS_FUNCTIONS enabled -> `parse_anonymous_function()`
   - Otherwise -> `parse_subshell()`
5. **Keywords** (lines 923-951):
   - `if` -> `parse_if_statement()`
   - `while` -> `parse_while_statement()`
   - `until` -> `parse_until_statement()`
   - `for` -> `parse_for_statement()`
   - `case` -> `parse_case_statement()`
   - `function` -> `parse_function_definition()`
   - `select` (requires FEATURE_SELECT_LOOP) -> `parse_select_statement()`
   - `time` (requires FEATURE_TIME_KEYWORD) -> `parse_time_command()`
   - `coproc` (requires FEATURE_COPROC) -> `parse_coproc()`
   - Other keywords (THEN, ELSE, FI, DONE, ESAC, etc.) -> return NULL (let parent construct detect)

6. **Function definition (bash-style, line 955):** If word is followed by `()`, call `parse_function_definition()` via lookahead check `is_function_definition()`.

### 4. Keyword Recognition and Disabling

Keywords are recognized by checking `token_is_keyword(current->type)` at line 923. The tokenizer has context-sensitive keyword recognition (enabled by default), which means shell keywords like `if`, `while`, `for` are classified as keyword tokens (TOK_IF, TOK_WHILE, etc.) only in command position.

**Why this matters:** If `if` appears as an argument or in a quoted context, it remains a WORD token, not TOK_IF.

### 5. Expansion on Words and String Concatenation (lines 1393-1523)

**Adjacent token concatenation:** The parser collects sequences of adjacent tokens (no whitespace gap) and concatenates them into a single argument:

```c
size_t last_end_pos = arg_token->position + strlen(arg_token->text);
// ... loop continues while next_token->position == last_end_pos
```

This allows syntax like:
```bash
echo hello${VAR}world    # Three tokens adjacent: hello (WORD) + ${VAR} (VARIABLE) + world (WORD)
x=prefix_$(cmd)_suffix   # Concatenated assignment value
```

**Node type determination (lines 1463-1513):**
- Single token: create node based on token type (NODE_STRING_LITERAL for TOK_STRING, NODE_STRING_EXPANDABLE for TOK_EXPANDABLE_STRING, etc.)
- Multiple adjacent tokens: always create `NODE_STRING_EXPANDABLE` (line 1504) since concatenation may involve variables

**Collected token types (lines 1394-1405, 1418-1430):**
- TOK_STRING (single-quoted)
- TOK_EXPANDABLE_STRING (double-quoted)
- TOK_ARITH_EXP ($((expr)))
- TOK_COMMAND_SUB, TOK_BACKQUOTE ($(cmd) or `cmd`)
- TOK_WORD (unquoted word)
- TOK_VARIABLE ($var, ${var})
- TOK_RBRACKET, TOK_ASSIGN, TOK_GLOB, TOK_QUESTION, TOK_NOT_EQUAL (all collected as words)
- TOK_KEYWORD (keywords can appear as arguments, e.g., `echo if`)

### 6. Array Literal Arguments in Builtin Commands (lines 1312-1392)

Special handling for arguments like `declare -A map=([key]=value)`. When parsing arguments:
- If we see `word=(...` pattern and FEATURE_INDEXED_ARRAYS is enabled (lines 1313-1321)
- Check if `=` is immediately adjacent to the word (no whitespace)
- Check if `(` immediately follows the `=`
- If all conditions met, parse the `(...)` as an array literal via `parse_array_literal()`
- Build a stringified representation like `name=([key]=value [key2]=value2)` as NODE_STRING_LITERAL
- Pass to builtin for parsing (the builtin interprets the [key]=value syntax)

## Dispatch Table -- When Does This Function Hand Off?

**Full dispatch matrix from lines 873-951:**

| Trigger Token | Condition | Function Called | Node Type | Notes |
|--------------|-----------|-----------------|-----------|-------|
| TOK_LBRACE | always | `parse_brace_group()` | NODE_BRACE_GROUP | { ... } |
| TOK_DOUBLE_LPAREN | FEATURE_ARITH_COMMAND | `parse_arithmetic_command()` | NODE_ARITH_CMD | (( ... )) |
| TOK_DOUBLE_LBRACKET | FEATURE_EXTENDED_TEST | `parse_extended_test()` | NODE_EXTENDED_TEST | [[ ... ]] |
| TOK_LPAREN | followed by `) {` and FEATURE_ANONYMOUS_FUNCTIONS | `parse_anonymous_function()` | NODE_ANON_FUNCTION | () { ... } |
| TOK_LPAREN | otherwise | `parse_subshell()` | NODE_SUBSHELL | ( ... ) |
| TOK_IF | always (keyword) | `parse_if_statement()` | NODE_IF | if ... then ... fi |
| TOK_WHILE | always (keyword) | `parse_while_statement()` | NODE_WHILE | while ... do ... done |
| TOK_UNTIL | always (keyword) | `parse_until_statement()` | NODE_UNTIL | until ... do ... done |
| TOK_FOR | always (keyword) | `parse_for_statement()` | NODE_FOR or NODE_FOR_ARITH | for ... in ... done or for (( ... )) |
| TOK_CASE | always (keyword) | `parse_case_statement()` | NODE_CASE | case ... esac |
| TOK_FUNCTION | always (keyword) | `parse_function_definition()` | NODE_FUNCTION | function name { ... } |
| TOK_SELECT | always (keyword), FEATURE_SELECT_LOOP | `parse_select_statement()` | NODE_SELECT | select ... in ... done |
| TOK_TIME | always (keyword), FEATURE_TIME_KEYWORD | `parse_time_command()` | NODE_TIME | time ... |
| TOK_COPROC | always (keyword), FEATURE_COPROC | `parse_coproc()` | NODE_COPROC | coproc name cmd |
| WORD + lookahead `()` | is_function_definition() = true | `parse_function_definition()` | NODE_FUNCTION | name() { ... } or name() compound |
| Other keywords (THEN, ELSE, ELIF, FI, DONE, ESAC, RBRACE, IN, DO, THEN, ELSE) | N/A | return NULL | N/A | Let parent construct detect and handle |

**Lookahead logic for `()` disambiguation (lines 891-920):**
```
if (current == TOK_LPAREN) {
  if (peek == TOK_RPAREN) {
    advance(); advance();  // Consume ( )
    if (current == TOK_LBRACE) {
      // () { ... } -> anonymous function
      restore_position();
      return parse_anonymous_function();
    }
    restore_position();
  }
  // Regular ( ... ) subshell
  return parse_subshell();
}
```

Position restoration (lines 897-914) uses tokenizer state save/restore to avoid consuming tokens if the lookahead didn't confirm the pattern.

## Trailing Redirections (lines 1262-1298)

After parsing the command name and arguments, the loop (lines 1263-1523) continues parsing arguments **or** redirections until a command separator is found:

**Loop termination conditions (lines 1263-1269):**
```c
while (!tokenizer_match(parser->tokenizer, TOK_EOF) &&
       !tokenizer_match(parser->tokenizer, TOK_SEMICOLON) &&
       !tokenizer_match(parser->tokenizer, TOK_NEWLINE) &&
       !tokenizer_match(parser->tokenizer, TOK_PIPE) &&
       !tokenizer_match(parser->tokenizer, TOK_AND) &&
       !tokenizer_match(parser->tokenizer, TOK_LOGICAL_AND) &&
       !tokenizer_match(parser->tokenizer, TOK_LOGICAL_OR))
```

**Redirection tokens recognized (lines 1277-1289):**
- TOK_REDIRECT_OUT (`>`)
- TOK_REDIRECT_IN (`<`)
- TOK_APPEND (`>>`)
- TOK_HEREDOC (`<<`)
- TOK_HEREDOC_STRIP (`<<-`)
- TOK_HERESTRING (`<<<`)
- TOK_REDIRECT_ERR (`2>`, `N>` for any fd)
- TOK_REDIRECT_IN_FD (`3<`, `N<` for any fd)
- TOK_REDIRECT_BOTH (`&>`)
- TOK_REDIRECT_FD (`>&1`, `>&2`, etc.)
- TOK_REDIRECT_FD_ALLOC (`{var}>`, fd allocation)
- TOK_REDIRECT_CLOBBER (`>|`)
- TOK_APPEND_ERR (`2>>`, `N>>`)
- TOK_APPEND_BOTH (`&>>`)

When any redirection token is encountered, `parse_redirection()` is called (line 1291) to consume the redirection and its target, producing a NODE_REDIR_* child on the command node.

**Interleaving of args and redirections:** Redirections can appear anywhere in the argument list:
```bash
cmd arg1 > file arg2     # valid: arg1, then redirect, then arg2
echo > out one two three # valid: redirect to 'out', then args 'one two three'
```

Redirections and arguments are collected as children of NODE_COMMAND in the order encountered.

## Notable Behaviors and Surprises

### 1. Permissive Assignment Prefix Collection

**Lines 1066-1205:** The parser collects **all trailing concatenatable tokens** as the assignment value, including:
- Keywords (`NAME=if`)
- Operators (`NAME=+`)
- Bracket tokens (`NAME=]`)
- Glob patterns (`NAME=*`)

This is **more permissive than bash/zsh** in that it will happily accept:
```bash
x=if y=then z=[  # Valid: three assignments, no error
```

Bash would reject `y=then` with "command not found: then" at runtime, but the parser accepts it syntactically.

### 2. Array Index Subscript Parsing is Textual

**Lines 963-1063:** The subscript between `[` and `]` is extracted as **plain text**, not parsed:
```bash
arr[$(cmd)]     # Subscript text is "$(cmd)", passed literally to executor
arr[1+2]        # Subscript text is "1+2", no arithmetic evaluation here
```

The executor/evaluator will expand and evaluate these at runtime.

### 3. Single-Quote Preservation in Assignment Values

**Lines 1165-1169:** When concatenating assignment values, single-quoted strings are **preserved with quotes**:
```
a='x' b=y  ->  a='x'b=y (quotes kept to prevent expansion)
a="$x" b=y ->  a=$xb=y   (double quotes removed, variable kept)
```

This is the parser's attempt to prevent shell expansion of the literal text `'x'` when reassembled.

### 4. Command Name Cannot Be Empty

**Lines 1243-1248:** After rejecting ERROR tokens and checking for word-like tokens, if nothing matches, an error is thrown:
```c
if (!token_is_word_like(current->type) && current->type != TOK_LBRACKET) {
    parser_error_add(..., "expected command name");
    return NULL;
}
```

Note: `TOK_LBRACKET` is allowed as a command name (for the `[` builtin test command).

### 5. Arrays Enabled Only When Feature-Gated

**Lines 966, 1082, 1314:** All array-related parsing is gated behind `shell_mode_allows(FEATURE_INDEXED_ARRAYS)`:
```c
if (shell_mode_allows(FEATURE_INDEXED_ARRAYS)) {
    // Parse arr[idx]=val and arr=(...)
}
```

If arrays are disabled, `arr[idx]=value` is treated as an assignment to a variable named literally `arr[idx]` (no bracket parsing).

### 6. Process Substitution as Arguments (lines 1301-1310)

Process substitution tokens `<(cmd)` and `>(cmd)` are recognized mid-argument-list and dispatched to `parse_process_substitution()`, producing NODE_PROC_SUB_IN or NODE_PROC_SUB_OUT children.

### 7. Zsh-Specific: Anonymous Functions

**Lines 891-920:** The `() { ... }` syntax is **zsh-specific** (FEATURE_ANONYMOUS_FUNCTIONS). The function is parsed with lookahead:
1. Current token is `(`
2. Next is `)`
3. Advance past both
4. Check if current (third token) is `{`
5. If yes, parse as anonymous function; otherwise restore and parse as subshell

This is a **two-token lookahead** (uncommon in the rest of the parser, which typically uses one-token lookahead).

### 8. Error Token Handling (lines 1210-1241)

If a TOK_ERROR token is encountered (unclosed quote, unclosed substitution), the parser generates a **specific error message** based on the error token's text:
- `$((` -> "unterminated arithmetic expansion"
- `$(` (without `((`) -> "unterminated command substitution"
- `` ` `` -> "unterminated backtick substitution"
- Unmatched quote -> "unterminated quoted string"

### 9. Function Definition Detection via `is_function_definition()`

**Lines 955-956:** Before treating a word as a command name, check if it's followed by `()`:
```c
if (token_is_word_like(current->type) && is_function_definition(parser)) {
    return parse_function_definition(parser);
}
```

This dispatch **must come before assignment detection**, otherwise `name() { }` would be misparsed as a command named `name` with no arguments (since `(` is not `=`).

### 10. Keywords Can Be Arguments

**Line 1400:** Keywords are accepted as argument tokens when collected:
```c
token_is_keyword(arg_token->type)
```

This allows:
```bash
echo if while for case
```

Keywords only trigger compound-command parsing when they appear in **command position** (checked at lines 923-951 before attempting regular command parsing).

### 11. Heredoc and Here-String Handling

**Lines 1279-1281:** TOK_HEREDOC, TOK_HEREDOC_STRIP, and TOK_HERESTRING are redirection tokens. The actual content (heredoc document or here-string value) is handled by `parse_redirection()`, which calls `collect_heredoc_content()` if needed.

### 12. Regex Match and Assignment Operators in Arguments

**Lines 1403, 1427:** TOK_NOT_EQUAL (`!=`) and TOK_ASSIGN (`=`) tokens can appear as argument elements (when adjacent to other tokens). This allows patterns like:
```bash
cmd a!=b x=y  # Concatenated into arguments: "a!=b" and "x=y"
```

## Open Questions

1. **Why allow keywords as arguments?** Is this for compatibility with shell scripts that use `echo if`? Or a lexer design issue where keywords aren't context-restricted?

2. **Comma operator missing:** The grammar allows `,(comma)` in arguments? Not evident from redirection/argument token checks. Is comma handled elsewhere (e.g., in arithmetic expressions), or is it rejected?

3. **Brace expansion on arguments:** The function doesn't do brace expansion (`{a,b}`). Is brace expansion handled in the executor, or is it a separate pass?

4. **Assignment value termination:** Why does the parser stop concatenating on `=`? Is this to allow `a=x b=y` to be two assignments? This breaks `a=$(cmd) b=c` if `$(cmd)` expands to a word containing `=`.

5. **Array subscript evaluation timing:** Subscripts like `arr[$(cmd)]` are stored as text. When are they evaluated? In the executor? Or at assignment time?

6. **Feature gate semantics:** If FEATURE_INDEXED_ARRAYS is disabled, does `arr[0]=x` parse as a variable named `arr[0]` literally? This seems odd for traditional shells that don't support arrays.

7. **Why two-token lookahead for `()`?** The anonymous function check restores tokenizer state. This is expensive. Could the tokenizer support three-token lookahead instead?

8. **Trailing redirections and pipes:** The loop (lines 1263-1523) stops at TOK_PIPE, so `cmd | file` parses `cmd` then hands off to the pipeline parser. But what about `cmd > file | cmd2`? The pipeline parser must handle redirections too?
