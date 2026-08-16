# Section 8 -- Redirections and Heredocs

## EBNF productions

```ebnf
redirection_operator = 
    | '<'                       (* standard input *)
    | '>'                       (* standard output *)
    | '>>'                      (* append output *)
    | '<<<'                     (* herestring *)
    | '<<' | '<<-'              (* heredoc, optionally strip tabs *)
    | '&>'                      (* redirect both stdout+stderr to file *)
    | '&>>'                     (* append both stdout+stderr to file *)
    | '|&'                      (* pipe both stdout+stderr *)
    | '>|'                      (* clobber -- unconditional overwrite *)
    | FD_NUMBER '<'             (* redirect from fd to stdin *)
    | FD_NUMBER '>'             (* redirect from stdout to fd *)
    | FD_NUMBER '>>'            (* append stdout to fd *)
    | FD_NUMBER '<&' ( FD_NUMBER | '-' | VARIABLE )  (* dup input fd *)
    | FD_NUMBER '>&' ( FD_NUMBER | '-' | VARIABLE )  (* dup output fd *)
    | FD_NUMBER '>&-'           (* close output fd *)
    | FD_NUMBER '<&-'           (* close input fd *)
    | '>&' ( FD_NUMBER | '-' | VARIABLE )  (* redirect stdout to fd (implicit 1>) *)
    | '<&' ( FD_NUMBER | '-' | VARIABLE )  (* redirect stdin from fd (implicit 0<) *)
    | '{' IDENTIFIER '}' '<'        (* bash 4.1+ fd allocation for input *)
    | '{' IDENTIFIER '}' '>'        (* bash 4.1+ fd allocation for output *)
    | '{' IDENTIFIER '}' '>>'       (* bash 4.1+ fd allocation for append *)
    | '{' IDENTIFIER '}' '>&' VARIANT  (* bash 4.1+ with fd variants *)

redirection = 
    redirection_operator ( WORD | HEREDOC_DELIMITER )

trailing_redirections = 
    redirection*

command_list_with_redirections =
    ( simple_command | compound_command ) trailing_redirections*

compound_command =
    | '(' command_list ')'                  (* subshell; supports trailing_redirections *)
    | '{' command_list '}'                  (* brace group; supports trailing_redirections *)
    | 'if' ... 'fi'                         (* if statement; supports trailing_redirections *)
    | 'while' ... 'done'                    (* while loop; supports trailing_redirections *)
    | 'until' ... 'done'                    (* until loop; supports trailing_redirections *)
    | 'for' ... 'done'                      (* for loop; supports trailing_redirections *)
    | 'for' '((' ... '))' 'do' ... 'done'  (* arithmetic for; supports trailing_redirections *)
    | 'case' ... 'esac'                     (* case statement; supports trailing_redirections *)
    | 'select' ... 'done'                   (* select loop; supports trailing_redirections *)
```

## Redirection operator table

| Operator | Token | Node type | FD default | Target form | Notes |
|----------|-------|-----------|------------|-------------|-------|
| `<` | TOK_REDIRECT_IN | NODE_REDIR_IN | 0 (stdin) | filename or process sub | Standard input redirection |
| `>` | TOK_REDIRECT_OUT | NODE_REDIR_OUT | 1 (stdout) | filename or process sub | Truncate; fails if file exists (unless `>` set) |
| `>>` | TOK_APPEND | NODE_REDIR_APPEND | 1 (stdout) | filename | Append to file |
| `<<<` | TOK_HERESTRING | NODE_REDIR_HERESTRING | 0 (stdin) | word/expansion | Here-string: single line via stdin |
| `<<` | TOK_HEREDOC | NODE_REDIR_HEREDOC | 0 (stdin) | delimiter on next line | Heredoc body: multi-line, expansion enabled unless delimiter quoted |
| `<<-` | TOK_HEREDOC_STRIP | NODE_REDIR_HEREDOC_STRIP | 0 (stdin) | delimiter on next line | Heredoc body: strip leading tabs only, expansion enabled unless delimiter quoted |
| `&>` | TOK_REDIRECT_BOTH | NODE_REDIR_BOTH | 1+2 (stdout+stderr) | filename | Redirect both stdout and stderr |
| `&>>` | TOK_APPEND_BOTH | NODE_REDIR_BOTH_APPEND | 1+2 (stdout+stderr) | filename | Append both stdout and stderr |
| `\|&` | TOK_PIPE_STDERR | (piping context) | n/a | (right side of pipe) | Pipe both stdout and stderr; not part of redirection but related operator |
| `>&#124;` | TOK_REDIRECT_CLOBBER | NODE_REDIR_CLOBBER | 1 (stdout) | filename | Clobber: unconditionally overwrite, ignoring noclobber setting |
| `N>` | TOK_REDIRECT_ERR | NODE_REDIR_ERR | N | filename | Redirect fd N to file (truncate) |
| `N>>` | TOK_APPEND_ERR | NODE_REDIR_ERR_APPEND | N | filename | Redirect fd N to file (append) |
| `N<` | TOK_REDIRECT_IN_FD | NODE_REDIR_IN_FD | N | filename | Redirect file to fd N |
| `N>&M` | TOK_REDIRECT_FD | NODE_REDIR_FD | embedded | embedded (M, -, or $VAR) | Duplicate output fd: fd N to fd M |
| `N>&-` | TOK_REDIRECT_FD | NODE_REDIR_FD | embedded | embedded (-) | Close output fd N |
| `N>&$VAR` | TOK_REDIRECT_FD | NODE_REDIR_FD | embedded | embedded ($VAR or ${VAR}) | Redirect to fd from variable |
| `N<&M` | TOK_REDIRECT_FD | NODE_REDIR_FD | embedded | embedded (M, -, or $VAR) | Duplicate input fd: fd N from fd M |
| `N<&-` | TOK_REDIRECT_FD | NODE_REDIR_FD | embedded | embedded (-) | Close input fd N |
| `N<&$VAR` | TOK_REDIRECT_FD | NODE_REDIR_FD | embedded | embedded ($VAR or ${VAR}) | Redirect from fd from variable |
| `>&M` | TOK_REDIRECT_FD | NODE_REDIR_FD | embedded | embedded (M, -, or $VAR) | Implicit `1>&M` -- redirect stdout to fd M |
| `>&-` | TOK_REDIRECT_FD | NODE_REDIR_FD | embedded | embedded (-) | Implicit `1>&-` -- close stdout |
| `>&$VAR` | TOK_REDIRECT_FD | NODE_REDIR_FD | embedded | embedded ($VAR or ${VAR}) | Implicit `1>&$VAR` |
| `<&M` | TOK_REDIRECT_FD | NODE_REDIR_FD | embedded | embedded (M, -, or $VAR) | Implicit `0<&M` -- redirect stdin from fd M |
| `<&-` | TOK_REDIRECT_FD | NODE_REDIR_FD | embedded | embedded (-) | Implicit `0<&-` -- close stdin |
| `<&$VAR` | TOK_REDIRECT_FD | NODE_REDIR_FD | embedded | embedded ($VAR or ${VAR}) | Implicit `0<&$VAR` |
| `{var}>` | TOK_REDIRECT_FD_ALLOC | NODE_REDIR_FD_ALLOC | dynamic | filename (or embedded if >&- or >&N) | Bash 4.1+/Zsh: dynamically allocate fd, store in variable |
| `{var}>>` | TOK_REDIRECT_FD_ALLOC | NODE_REDIR_FD_ALLOC | dynamic | filename (or embedded if >&- or >&N) | Bash 4.1+/Zsh: allocate and append |
| `{var}<` | TOK_REDIRECT_FD_ALLOC | NODE_REDIR_FD_ALLOC | dynamic | filename | Bash 4.1+/Zsh: dynamically allocate fd for input |
| `{var}>&-` | TOK_REDIRECT_FD_ALLOC | NODE_REDIR_FD_ALLOC | dynamic | embedded (-) | Bash 4.1+/Zsh: close fd stored in variable |
| `{var}>&N` | TOK_REDIRECT_FD_ALLOC | NODE_REDIR_FD_ALLOC | dynamic | embedded (N or $VAR) | Bash 4.1+/Zsh: duplicate to variable's fd |

## AST nodes produced

| Node type | Produced by | Description | Children |
|-----------|------------|-------------|----------|
| NODE_REDIR_IN | `<` | Standard input redirection | target (filename/process sub) |
| NODE_REDIR_OUT | `>` | Standard output redirection | target (filename/process sub) |
| NODE_REDIR_APPEND | `>>` | Append stdout | target (filename) |
| NODE_REDIR_ERR | `N>` | Redirect fd N to file | target (filename) |
| NODE_REDIR_ERR_APPEND | `N>>` | Append fd N to file | target (filename) |
| NODE_REDIR_IN_FD | `N<` | Redirect file to fd N | target (filename) |
| NODE_REDIR_HEREDOC | `<<` | Here-document (expansion enabled unless delimiter quoted) | content, expand_flag |
| NODE_REDIR_HEREDOC_STRIP | `<<-` | Here-document, strip leading tabs | content, expand_flag |
| NODE_REDIR_HERESTRING | `<<<` | Here-string | target (word/expansion) |
| NODE_REDIR_BOTH | `&>` | Redirect both stdout and stderr | target (filename) |
| NODE_REDIR_BOTH_APPEND | `&>>` | Append both stdout and stderr | target (filename) |
| NODE_REDIR_FD | `N>&M`, `N<&M`, `N>&-`, `>&M`, etc. | FD duplication/closing; target encoded in token | (no separate target child) |
| NODE_REDIR_FD_ALLOC | `{var}>`, `{var}<`, `{var}>>`, `{var}>&-`, `{var}>&N` | Bash 4.1+/Zsh fd allocation; target encoded or separate | target (filename or fd), if not embedded in token |
| NODE_REDIR_CLOBBER | `>&#124;` | Unconditional overwrite | target (filename) |

## Heredoc collection -- the context-sensitive bit

### Overview
Heredoc body collection is triggered immediately in `parse_redirection()` (parser.c:1900-1955). The entire mechanism is context-sensitive: it reads directly from the raw input buffer, bypassing the normal token stream, to preserve whitespace and detect line-based delimiters accurately.

### Step-by-step execution

**1. Delimiter extraction (parser.c:1904)**
- The delimiter token (next token after `<<` or `<<-`) is extracted as-is: `target_token->text`
- If the delimiter is quoted (`TOK_STRING` or `TOK_EXPANDABLE_STRING`), an expansion flag is set to `false`; otherwise `true`
- The token text may include quotes (e.g., `"EOF"`, `'EOF'`) -- these are preserved for later unquoting

**2. Expansion flag determination (parser.c:1909-1914)**
- If `target_token->type == TOK_STRING` or `TOK_EXPANDABLE_STRING`: disable expansion (any quoted form)
- Otherwise: enable expansion (unquoted delimiter allows variable expansion in body)
- **Note:** POSIX rule -- any quoting of the delimiter disables expansion of the body

**3. Tokenizer advance (parser.c:1918)**
- The tokenizer is advanced past the delimiter token to position just after it on the same line
- This positions the stream for `collect_heredoc_content()` to find the heredoc operator and begin collecting

**4. Body collection (parser.c:1922-1923, invoked function at 2030-2227)**

#### Finding the heredoc start in raw input (2030-2131)
The function `collect_heredoc_content()` scans the raw tokenizer input buffer backward from the current tokenizer position to locate the `<<` or `<<-` operator:

- Loop through `tokenizer->input` searching for `<<` (2062)
- When `<<` is found, check for optional `-` flag (2067-2069)
- Skip optional whitespace (spaces and tabs) after the `-` (2073-2076)
- Check if the next text matches the unquoted delimiter:
  - If the input has a quoted delimiter (e.g., `<<'EOF'`), extract the unquoted content and compare (2084-2106)
  - Otherwise, match unquoted delimiter directly (2108-2116)
- When match is found, position is at the end of the delimiter in input; advance to end of that line (2119-2127)
- Move to the next line: `content_start` is now at the first character of the first heredoc body line (2126)

#### Collecting lines until delimiter found (2133-2203)
Loop through input starting at `content_start`:

- Extract each line (from line start to `\n`) (2145-2149)
- If `<<-` variant: strip leading **tabs only** from the line (2163-2166)
  - **Important:** Only tabs are stripped, not spaces; this is per POSIX `<<-` semantics
  - Stripping is done character-by-character: `while (*line_content == '\t')`
- Compare the (possibly stripped) line to the unquoted delimiter (2170)
- If exact match: stop collection (2171-2173)
- Otherwise: append line + newline to content buffer (2176-2197)
- Move to next line: `line_start = line_end + 1` (2202)

#### Final state update (2205-2219)
- Update tokenizer position to after the delimiter line: `tokenizer->position = line_start`
- Recalculate line and column tracking for all consumed input (2209-2216)
- Call `tokenizer_refresh_from_position()` to re-tokenize from the new position (2219)
- Free temporary unquoted delimiter if allocated (2222-2223)
- Return the collected content string (2226)

### Delimiter matching rules

1. **Quoted vs. unquoted:** The delimiter is matched against the **unquoted** form (internal representation in memory)
2. **Input detection:** The input may contain the delimiter in various forms:
   - Unquoted: `<<EOF` -> search for literal `EOF`
   - Single-quoted: `<<'EOF'` -> search for literal `EOF` (quotes are parsing-level, not in body)
   - Double-quoted: `<<"EOF"` -> search for literal `EOF` (quotes are parsing-level, not in body)
   - Escaped: `<<\EOF` -> search for literal `EOF` (escape is parsing-level)
3. **Line-based:** Delimiter must match **exactly** (after stripping if `<<-`) and must be **alone on a line** (followed only by the newline)
4. **Whitespace handling:** 
   - **Before delimiter:** optional spaces and tabs are skipped after `<<` or `<<-` (2073-2076)
   - **Leading tabs in body:** only tabs are stripped by `<<-`, not spaces (2164)
   - **Trailing whitespace on lines:** preserved in the body

### Quoted delimiter forms recognized

| Form | Expansion? | Unquoting | Example |
|------|-----------|-----------|---------|
| `<<EOF` | yes | none | `cat <<EOF` -> body expands |
| `<<'EOF'` | no | strip quotes | `cat <<'EOF'` -> body literal |
| `<<"EOF"` | no | strip quotes | `cat <<"EOF"` -> body literal |
| `<<\EOF` | no | strip backslash | `cat <<\EOF` -> body literal |
| `<<E"O"F` | no (any quoting disables) | mixed unquoting | `cat <<E"O"F` -> body literal |

### Error cases

1. **EOF before delimiter:** The loop (2143-2203) will scan to end of input without finding a match. In this case, all remaining input is collected as the body, and the function returns normally (no explicit error). **No error is reported in the current code.**
2. **Empty delimiter:** Allowed; matches an empty line
3. **Delimiter with internal quotes:** Handled at the parsing level; once tokenized, quotes are processed and the unquoted form is used for matching

### Multiple heredocs on one line

**Not supported directly by collect_heredoc_content().** Each call to `parse_redirection()` (which invokes `collect_heredoc_content()`) processes exactly one heredoc. If multiple `<<` appear on one line, each will trigger its own collection call in sequence:

```
cat <<A <<B
line1
A
line2
B
```

- `<<A` is parsed, `collect_heredoc_content()` finds the `A` delimiter and collects `line1`
- `<<B` is then parsed, finds the `B` delimiter and collects `line2`
- Both bodies are collected sequentially

## Context-sensitive behavior and lexer interactions

### Redirection operator tokenization

- **FD prefix as single token:** When a number appears at the start of a word, the tokenizer checks the next character(s). If it is `<`, `>`, `>>`, `<&`, or `>&`, the entire fd+operator is returned as a single token (e.g., `2>` as TOK_REDIRECT_ERR). **The fd and operator are NOT separate tokens** (tokenizer.c:1850-1973).

- **Variable-expansion in FD targets:** Patterns like `N>&$VAR` and `N>&${VAR}` are tokenized as single tokens (TOK_REDIRECT_FD), with the variable syntax embedded. The parser and executor later expand the variable to determine the target fd.

### {varname}> fd allocation syntax

- The tokenizer recognizes `{identifier}` followed by `>`, `<`, `>>`, `>&`, etc. as a single token (TOK_REDIRECT_FD_ALLOC) (tokenizer.c:1650-1700).
- Pattern: `{[a-zA-Z_][a-zA-Z0-9_]*}[<>|&-]+` is recognized as a single token.
- Variants include `{var}>&-`, `{var}>&N`, `{var}>&${VAR}`, etc.
- In `parse_redirection()`: if the token ends with `>&-` or `>&N`, no separate target is needed; otherwise, a filename follows (parser.c:1849-1862).

### Heredoc body outside normal token stream

- `collect_heredoc_content()` reads directly from `tokenizer->input` (the raw input buffer), **not from the token stream**.
- This allows it to preserve all whitespace, newlines, and other characters exactly as written.
- After collection, the tokenizer position is updated to resume after the heredoc body (parser.c:2206), and the lookahead token is refreshed (parser.c:2219).
- This is why heredocs are called "context-sensitive": the lexer's normal token-by-token stream is bypassed.

### Keyword recognition disabled for redirection targets

- When parsing a redirection target (after `>`, `<`, etc.), keyword recognition is temporarily disabled (parser.c:1831-1832).
- This allows filenames like `in`, `do`, `done`, `while` to be treated as regular words, not keywords.
- Keyword recognition is re-enabled after parsing the target (parser.c:1839).

### Compound commands and trailing redirections

The parser recognizes that compound commands (brace groups, subshells, control structures) can have redirections attached after their closing delimiter:

```
{ echo hello; } > output.txt
(echo hello) >> output.txt
if true; then echo yes; fi 2> errors.txt
while true; do sleep 1; done < input.txt
```

The function `parse_trailing_redirections()` (parser.c:1734-1751) loops and parses any redirection operators that follow a compound command, attaching each redirection node as a child of the compound node.

**Compound commands supporting trailing redirections** (parser.c calls to `parse_trailing_redirections`):
- Brace groups: `{ ... }` (line 1604)
- Subshells: `( ... )` (line 1690)
- If statements: `if ... fi` (line 2372)
- While loops: `while ... done` (line 2465)
- Until loops: `until ... done` (line 2553)
- For loops: `for ... done` (line 3099)
- Arithmetic for loops: `for (( ... )) do ... done` (line 2863)
- Case statements: `case ... esac` (line 3736)
- Select loops: `select ... done` (line 3244)

## Notable behaviors and surprises

### 1. `<<-` only strips tabs, not all whitespace
The code explicitly checks `*line_content == '\t'` (parser.c:2164), not `isspace()`. This means leading spaces are NOT stripped by `<<-`; only tabs are. This is per POSIX 2008 specification.

### 2. Expansion flag stored in AST
For heredocs, the `expand_variables` flag is stored as a second child node (NODE_VAR with value "1" or "0") (parser.c:1945-1953). This defers the actual expansion to the execution phase, allowing the interpreter to handle it contextually.

### 3. FD embedding in tokens
Patterns like `N>&M`, `N<&-`, and `N>&$VAR` are fully embedded in the redirection token text. The `parse_redirection()` function returns the node without a separate target child for `NODE_REDIR_FD` (parser.c:1842-1845). This is different from filename redirections, which have a separate target child.

### 4. {varname}> detection happens at tokenizer level
The tokenizer (not the parser) detects `{identifier}>` syntax and produces a single TOK_REDIRECT_FD_ALLOC token. The parser then decides whether to expect a separate target file.

### 5. `>|` clobber operator
The `>|` operator (tokenizer.c:1430) unconditionally overwrites, even if the `noclobber` option is set in the shell. It's tokenized as TOK_REDIRECT_CLOBBER and produces NODE_REDIR_CLOBBER.

### 6. `&>` and `&>>` redirect both streams
These operators redirect **both** stdout and stderr to the same file, equivalent to `1> file 2>&1` and `1>> file 2>&1` respectively. They are tokenized as TOK_REDIRECT_BOTH and TOK_APPEND_BOTH.

### 7. Process substitution as redirection target
The parser recognizes `<(cmd)` and `>(cmd)` as valid redirection targets (parser.c:1864-1876). These are parsed as process substitution nodes and attached as children of the redirection node.

### 8. Variable reference in fd duplication: `N>&${varname}`
When duplication targets a variable (e.g., `2>&${FD}`), the tokenizer includes the full `${VAR}` syntax in the token text. Variable expansion happens at execution time.

### 9. Token concatenation for redirection targets
For non-heredoc, non-fd redirections, the parser concatenates consecutive word-like tokens without whitespace (parser.c:1956-2001). This allows targets like `/tmp/file_$VAR` to be treated as a single filename.

### 10. Immediate vs. deferred heredoc collection
Heredoc bodies are collected **immediately** during parsing, not deferred to a later phase. This means the parser must consume the heredoc body right away and store it in the AST. This is different from some other shell implementations that defer body collection.

## Open questions

1. **Heredoc body EOF handling:** If a heredoc delimiter is never found (EOF reached first), does the current code report an error or silently use all remaining input? (Appears to silently collect to EOF, with no explicit error.)

2. **Heredoc after pipeline continuation:** If a heredoc appears in a command before a `|` or `&&`, does `collect_heredoc_content()` correctly handle reading lines that come after the pipeline operator on the same physical line? (Not clearly tested in the code.)

3. **Interaction with `set -e` (errexit):** When a heredoc delimiter is not found, should the shell exit immediately if `set -e` is active? (Not explicitly implemented.)

4. **Variable expansion in `<<<` herestring:** The code supports `<<<` (herestring), which takes a single word/expansion. Does it support expansions like `<<<$(cat file)`, and if so, are they expanded at parse time or execution time? (Presumably execution time, but not documented.)

5. **Redirection precedence with operators:** When multiple redirections are chained (e.g., `cmd < input > output 2>&1`), does the parser enforce any specific precedence, or are they applied left-to-right in execution? (Likely left-to-right, but not explicitly documented.)

6. **{varname}>&M forms with variable targets:** For `{var}>&${FD}` where FD is itself a variable, is `${FD}` expanded to get the target fd number before allocation, or is the variable binding deferred? (Likely deferred to execution.)
