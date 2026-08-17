# Section 9 -- Tokenizer

The lush tokenizer is a hand-written lexer that performs context-sensitive tokenization. It differs from a pure lexical analyzer in that it maintains state for keyword recognition, arithmetic mode, and feature flags; performs lookahead to disambiguate constructs; and integrates closely with the parser via callback hooks for heredoc body collection.

## Token classes

The tokenizer defines ~80 token types in the `token_type_t` enum (tokenizer.h:19-111). They fall into these families:

**Basic literals** (tokenizer.h:20-27):
- `TOK_WORD`: Regular unquoted word (command, argument, filename)
- `TOK_STRING`: Single-quoted literal string (no expansions)
- `TOK_EXPANDABLE_STRING`: Double-quoted string (contains `$`, backticks, or escapes)
- `TOK_NUMBER`: Numeric literal
- `TOK_VARIABLE`: Variable reference (`$var`, `${var}`, `$(cmd)`, etc.)

**Operators and separators** (tokenizer.h:29-81):
- Control flow: `TOK_SEMICOLON` (`;`), `TOK_PIPE` (`|`), `TOK_AND` (`&`)
- Logical: `TOK_LOGICAL_AND` (`&&`), `TOK_LOGICAL_OR` (`||`)
- Redirections: `TOK_REDIRECT_IN` (`<`), `TOK_REDIRECT_OUT` (`>`), `TOK_APPEND` (`>>`), `TOK_HEREDOC` (`<<`), `TOK_HEREDOC_STRIP` (`<<-`), `TOK_HERESTRING` (`<<<`), `TOK_REDIRECT_ERR` (`N>`), `TOK_REDIRECT_IN_FD` (`N<`), `TOK_REDIRECT_BOTH` (`&>`), `TOK_APPEND_ERR` (`N>>`), `TOK_REDIRECT_FD` (`&N`, `&-`, `&$VAR`), `TOK_REDIRECT_FD_ALLOC` (`{var}>`)
- Arithmetic: `TOK_ASSIGN` (`=`), `TOK_NOT_EQUAL` (`!=`), `TOK_PLUS` (`+`), `TOK_MINUS` (`-`), `TOK_DIVIDE` (`/`), `TOK_MODULO` (`%`), `TOK_MULTIPLY` (`*`), `TOK_GLOB` (`*` as glob), `TOK_QUESTION` (`?`)
- Extended (bash/zsh): `TOK_PLUS_ASSIGN` (`+=`), `TOK_REGEX_MATCH` (`=~`)
- Process substitution: `TOK_PROC_SUB_IN` (`<(`), `TOK_PROC_SUB_OUT` (`>(`), `TOK_PIPE_STDERR` (`|&`), `TOK_APPEND_BOTH` (`&>>`)
- Case control: `TOK_CASE_FALLTHROUGH` (`;&`), `TOK_CASE_CONTINUE` (`;;$`)
- Clobber: `TOK_REDIRECT_CLOBBER` (`>|`)

**Command substitution** (tokenizer.h:57-59):
- `TOK_COMMAND_SUB`: `$(...)`
- `TOK_ARITH_EXP`: `$((...))` -- contains both syntax and arithmetic content
- `TOK_BACKQUOTE`: Backtick-quoted command `` `...` ``

**Delimiters** (tokenizer.h:61-71):
- Parens: `TOK_LPAREN` (`(`), `TOK_RPAREN` (`)`), `TOK_DOUBLE_LPAREN` (`((`), `TOK_DOUBLE_RPAREN` (`))`)
- Braces: `TOK_LBRACE` (`{`), `TOK_RBRACE` (`}`)
- Brackets: `TOK_LBRACKET` (`[`), `TOK_RBRACKET` (`]`), `TOK_DOUBLE_LBRACKET` (`[[`), `TOK_DOUBLE_RBRACKET` (`]]`)

**Keywords** (tokenizer.h:87-104):
`TOK_IF`, `TOK_THEN`, `TOK_ELSE`, `TOK_ELIF`, `TOK_FI`, `TOK_WHILE`, `TOK_DO`, `TOK_DONE`, `TOK_FOR`, `TOK_IN`, `TOK_CASE`, `TOK_ESAC`, `TOK_UNTIL`, `TOK_FUNCTION`, `TOK_SELECT`, `TOK_TIME`, `TOK_COPROC`

**Structural** (tokenizer.h:107-110):
- `TOK_NEWLINE`: Significant newline
- `TOK_WHITESPACE`: Spaces/tabs (usually skipped in token stream)
- `TOK_COMMENT`: `#` comment
- `TOK_ERROR`: Invalid token

## Reserved words

The tokenizer maintains a keyword lookup table at tokenizer.c:22-36. All reserved words are currently **unconditionally reserved** -- they are recognized as keywords whenever `enable_keywords` is true. The table is:

```c
if, then, else, elif, fi (if-then-else)
while, do, done (while loops)
for, in (for loops)
case, esac (case statements)
until (until loops)
function (function declarations)
select (select loops)
time (command timing)
coproc (coprocesses)
```

The parser controls keyword recognition via the `tokenizer_enable_keywords()` function (tokenizer.c:400-403). Keywords are only classified as such if `enable_keywords` is true; otherwise they are treated as `TOK_WORD`. The parser disables keyword recognition in contexts where a word can appear in place of a keyword (e.g., after `function` name, inside quoted strings).

Unlike bash, which has limited positional reservation (e.g., `do` only after `for`), **lush does not implement positional reservation**. Instead, it relies on `enable_keywords` state management by the parser.

## Lexer modes / context-sensitivity

The tokenizer does not have explicit lexer modes (like flex's `BEGIN` states). Instead, it is parameterized by:

1. **`enable_keywords` flag** (tokenizer.h:133): Boolean state on the tokenizer struct. When false, `classify_word()` (tokenizer.c:542-557) always returns `TOK_WORD` regardless of whether the word matches a keyword. The parser toggles this via `tokenizer_enable_keywords()`.

2. **`arith_cmd_depth` counter** (tokenizer.h:134): Tracks nesting of `(( ))` arithmetic commands. Incremented when `((` is seen (tokenizer.c:1561), decremented when `))` is seen (tokenizer.c:1641). The tokenizer uses this to disambiguate `))` as `TOK_DOUBLE_RPAREN` (arithmetic end) vs. two `TOK_RPAREN` (nested parentheses). Without this, `cat <(cat <(echo nested))` would fail because the inner `))` would be tokenized as the arithmetic-command terminator instead of two closing parens.

3. **Feature flags** (from `shell_mode_allows()`): Lush checks `FEATURE_*` flags at tokenization time (e.g., `FEATURE_EXTENDED_GLOB`, `FEATURE_PROCESS_SUBSTITUTION`, `FEATURE_INDEXED_ARRAYS`, `FEATURE_REGEX_MATCH`). These gates determine whether certain token types are recognized:
   - `FEATURE_ARITH_COMMAND`: Enables `(( ))` and arithmetic expansion
   - `FEATURE_PROCESS_SUBSTITUTION`: Enables `<(`, `>(`, `|&`, `&>>`
   - `FEATURE_EXTENDED_GLOB`: Enables `@(`, `?(`, `*(`, `+(`, `!(`, `(a|b)suffix` patterns, and glob qualifier suffixes like `*(.)` 
   - `FEATURE_INDEXED_ARRAYS`: Enables `arr+=(...)` and array subscript syntax
   - `FEATURE_BRACE_EXPANSION`: Enables `{a,b,c}` and `{1..10}` as words, not braces
   - `FEATURE_EXTENDED_TEST`: Enables `[[ ]]` construct
   - `FEATURE_REGEX_MATCH`: Enables `=~` operator

## Quote contexts

### Single quotes
Single-quoted strings are fully literal. No expansions, no escapes, no line continuations. The tokenizer scans from `'` to the closing `'`, copying content verbatim. Newlines within single quotes are preserved and increment the line counter (tokenizer.c:738-746). Unclosed single quotes return `TOK_ERROR`.

### Double quotes
Double-quoted strings allow expansions. Inside `"..."`, the tokenizer:
- Recognizes `$(...)` and backtick command substitutions and stores them verbatim (tokenizer.c:763-830, 831-871)
- Recognizes `$var`, `${var}` variable references (processed by the parser later, not the tokenizer)
- Processes backslash escapes: `\"`, `\$`, `` \` ``, `\\`, and `\<newline>` (line continuation, removed entirely, tokenizer.c:872-917)
- Preserves other backslash-char pairs literally (e.g., `\x` -> `\x`)
- Preserves literal newlines and advances the line counter
- Marks the result as `TOK_EXPANDABLE_STRING` if any expansions or special characters are found (tokenizer.c:754, 1039)

The tokenizer does **not** perform variable/command substitution itself -- it only marks the token as expandable and passes the raw text to the parser/expansion layer.

### ANSI-C quoting `$'...'`
When `$'` is encountered outside quotes or after a quote closure (tokenizer.c:1125-1155 for standalone, tokenizer.c:966-982 for adjacent), the tokenizer scans to the closing `'`, skipping backslash-escape sequences (`\x`, `\n`, etc.) without interpreting them. The result is marked as `TOK_STRING`. The actual ANSI-C interpretation (converting `\n` to newline, `\uXXXX` to Unicode) is deferred to the parser/expansion layer.

### Adjacent quote concatenation
When a close quote is immediately followed by another quote or `$'`, the tokenizer does **not** return; it continues parsing (tokenizer.c:957-1043). This allows constructs like `'hello'"world"` or `'a'$'b'` to be tokenized as a single token. This is handled via a `goto parse_next_segment` loop within the quote handling code.

### Quote nesting inside `$(...)`
When parsing a double-quoted string, if a `$(...)` or `` `...` `` is encountered, the tokenizer **scans it fully** (including any nested quotes) and copies the entire substitution verbatim into the result string (tokenizer.c:763-830, 831-871). The parser later re-tokenizes the content of `$(...)`; the tokenizer itself does not recurse.

## Expansion lex points

The tokenizer recognizes the **syntax** of expansions but does not evaluate them. Each expansion is returned as a single token with the raw text:

- **`$var`** / **`${var}`**: Recognized and returned as `TOK_VARIABLE` (tokenizer.c:1047-1218). Simple variables like `$x`, `$$`, `$?` are distinguished by length. Braced variables `${var}` include nested braces in the captured text. Inside double quotes or unquoted context, recognized. When adjacent to other word characters, `$var` ends the word and becomes a separate token.

- **`$(cmd)`**: Command substitution. The tokenizer finds the matching `)` (with nested paren counting) and returns `TOK_COMMAND_SUB` with the full text `$(...)` (tokenizer.c:1095-1123). Inside double quotes, the entire `$(...)` is copied into the result and marked for later expansion.

- **`$((expr))`**: Arithmetic expansion. Tokenized as `TOK_ARITH_EXP` (tokenizer.c:1062-1093). Inside double quotes, treated like `$(cmd)`.

- **`` `...` ``**: Backtick command substitution. Returns `TOK_BACKQUOTE` with the full text (tokenizer.c:1221-1248). Inside double quotes, copied verbatim.

- **`$'...'`**: ANSI-C quoting. Returns `TOK_STRING` (tokenizer.c:1125-1155).

- **`$"..."`**: Not currently implemented in the code. The tokenizer would treat `$"` as a variable `$"`, not a special quote.

- **`${...}` parameter expansion / substitution operators**: The `${...}` brace counting logic (tokenizer.c:1157-1183) captures the full syntax. Operators like `${var:-default}`, `${var%pattern}`, `${var^}` are not distinguished by the tokenizer -- they're all `TOK_VARIABLE`. The parser or expansion layer interprets the contents.

- **`$[...]` arithmetic** (ksh/zsh-ism): Not detected by the tokenizer. If present, `$[` would be tokenized as variable `$[` followed by the rest.

All of these are recognized **only at the top level of a word context** (before operators, whitespace, or delimiters). If a `$` is inside quotes, it's handled by the quote logic, not the operator logic.

## Lookahead

The tokenizer maintains a **two-token lookahead** system:
- `tokenizer->current`: The current token
- `tokenizer->lookahead`: The next token

This is initialized when the tokenizer is created (tokenizer.c:79-80) and maintained during `tokenizer_advance()` (tokenizer.c:135-149). When advancing, the lookahead becomes current, and a new token is read ahead.

**Why two-token lookahead?** The primary use is to disambiguate keyword contexts. When the parser needs to know if the next-next token is a keyword, it can call `tokenizer_peek()` and then conditionally call `tokenizer_enable_keywords()` before advancing. For example:

- After `function`, the parser disables keywords so the function name is not recognized as `TOK_IF`, `TOK_WHILE`, etc.
- In the lookahead, if the parser sees `do` and wants to treat it as a keyword, but `enable_keywords` is currently false, the parser calls `tokenizer_refresh_lookahead()` (tokenizer.c:415-434) to re-tokenize the lookahead token with the new `enable_keywords` setting.

**Re-tokenization of lookahead**: The `tokenizer_refresh_lookahead()` function (tokenizer.c:415-434) saves the position where the lookahead token starts, frees the lookahead, restores the tokenizer position, and calls `tokenize_next()` again. This is the only place where the same source text is tokenized twice, allowing dynamic reclassification of keywords.

The `tokenizer_refresh_from_position()` function (tokenizer.c:446-463) is used after heredoc body collection (parser.c:1922) to reset the tokenizer state and re-tokenize from the current position, since the parser has advanced `input` past the heredoc body.

## Heredoc handling at the lexer level

The tokenizer **does not read heredoc bodies**. When `TOK_HEREDOC`, `TOK_HEREDOC_STRIP`, or `TOK_HERESTRING` is recognized, the tokenizer only consumes the `<<` / `<<-` / `<<<` operator and returns a token.

The parser then calls `collect_heredoc_content()` (parser.c:2030-2120) to read the delimiter and body. This function:
1. Reads the delimiter word from the input following the `<<` / `<<-`
2. Scans forward through the remaining shell commands
3. Stops at a line that contains **only the delimiter** (with optional leading tabs if `<<-`)
4. Returns the body content

The tokenizer's role is limited: after the parser calls `tokenizer_refresh_from_position()`, the tokenizer resumes from the current input position, which is now past the heredoc body (parser.c:1922).

**Why this design?** Heredoc body collection requires parsing context: the parser must know whether the delimiter is quoted (in which case the body is literal) or unquoted (in which case variable expansion applies). The parser has this information; the tokenizer does not. So the parser owns heredoc body collection.

## Comments

Comments are recognized at the tokenizer level. When the tokenizer sees `#`, it scans to the end of line (or EOF) without checking quotes or context:

```c
if (c == '#') {
    size_t start = tokenizer->position;
    while (tokenizer->position < tokenizer->input_length &&
           tokenizer->input[tokenizer->position] != '\n') {
        tokenizer->position++;
        tokenizer->column++;
    }
    size_t length = tokenizer->position - start;
    return token_new(TOK_COMMENT, &tokenizer->input[start], length,
                     start_line, start_column, start_pos);
}
```

This is correct because:
- Shell metacharacters (quotes, operators) are all ASCII and recognized at the `#` point
- If `#` is inside quotes, the quote handler consumes the entire quote before the `#` is seen
- If `#` is inside a word, it was already part of the word token (since `#` is in the `is_word_char()` set, tokenizer.c:578)

**A `#` inside `$'...'` and `$"..."` is treated as literal.** The ANSI-C quote handler (tokenizer.c:1125-1155) does not special-case `#`.

## Notable behaviors and surprises

1. **Keyword reclassification**: Unlike traditional lexers, lush tokenizes `do` or `for` once, then can **re-tokenize the lookahead token** to reclassify it as a word if the parser context changes. This is necessary because bash treats `do` as a keyword only in specific positions. The `tokenizer_refresh_lookahead()` function enables this.

2. **Backslash-newline line continuation**: In unquoted context and double-quoted context (but not single-quoted), `\<newline>` is completely removed (not even a character left). In double quotes, the tokenizer discards both the backslash and newline (tokenizer.c:876-880). In unquoted words, the same applies (tokenizer.c:1016-1021). This differs from some shells that preserve the behavior but consume the line.

3. **UTF-8 word characters**: The tokenizer uses `is_word_codepoint()` (tokenizer.c:591-612) to allow non-ASCII Unicode characters in words. All non-ASCII codepoints (U+0080 and above) are treated as valid word characters. This enables filenames and variable names with accented characters, emoji, etc., without shell-escaping them. ASCII metacharacters (`|`, `$`, quotes, etc.) remain ASCII-only.

4. **Brace expansion lookahead**: When the tokenizer sees `{`, it scans ahead to find a matching `}` and checks for `,` or `..` (tokenizer.c:1704-1795). If these are found and the pattern is valid, the tokenizer treats the entire `{...}` as a word, not a brace delimiter. This allows `{a,b,c}` to be a single `TOK_WORD` and Cartesian products like `{1..2}{a..b}` to be captured.

5. **Extended glob patterns**: When `FEATURE_EXTENDED_GLOB` is enabled, patterns like `@(...)`, `?(...)`, `*(...)`, `+(...)`, `!(...)` are recognized and included in the word token (tokenizer.c:2106-2139). Also, `(a|b)suffix` is tokenized as a word if it matches the extended glob alternation pattern (tokenizer.c:1565-1624).

6. **Array literal syntax**: When `FEATURE_INDEXED_ARRAYS` is enabled and `var=(...)` is encountered, the entire `var=(...)` is captured as a single word token (tokenizer.c:2074-2105). This allows assignments like `arr=([0]=x [1]=y)` to be treated as a single lexeme.

7. **Glob qualifiers**: When `FEATURE_GLOB_QUALIFIERS` is enabled and a word contains `*`, `?`, or `[`, the tokenizer checks for a suffix like `(.)` for directories or `(/)` (tokenizer.c:2301-2327). If found, it's included in the word token.

8. **Arithmetic command depth tracking**: The `arith_cmd_depth` counter disambiguates `))`  inside nested `<(...)` from the end of `((...))`. Without it, `cat <(cat <(echo x))` would fail on the final `))` because it would be tokenized as arithmetic-end instead of two rparen. This is a **zsh-ism** not standard in bash.

9. **Adjacent quote concatenation**: Lush concatenates adjacent quoted strings into a single token at the lexer level (tokenizer.c:957-1043). So `'a''b'` becomes one `TOK_STRING` with content `ab`, not two separate tokens. This is standard POSIX behavior, but the lush tokenizer handles it early, in the quote handler.

10. **Process substitution feature flag**: `<(...)` and `>(...)` are only recognized if `FEATURE_PROCESS_SUBSTITUTION` is enabled (tokenizer.c:1335-1340, 1414-1419). Otherwise, `<(` is tokenized as `TOK_REDIRECT_IN` followed by `TOK_LPAREN`.

11. **Numbered file descriptor redirections**: The tokenizer handles patterns like `2>`, `3<`, `2>>`, `2>&1`, `2>&-`, `2>&$VAR` as single tokens (tokenizer.c:1841-1973). These are recognized **after** whitespace is skipped, so `2 >` is `TOK_NUMBER` then `TOK_REDIRECT_OUT`, while `2>` is `TOK_REDIRECT_ERR`.

12. **FD allocation syntax**: Bash 4.1+ and zsh support `{varname}> file` to allocate an fd and store its number in `varname`. Lush recognizes this pattern (tokenizer.c:1651-1700) and returns `TOK_REDIRECT_FD_ALLOC` for the entire `{name}>` or `{name}<` prefix.

## Open questions

1. **Why not handle heredoc bodies in the tokenizer?** The current design defers heredoc body collection to the parser, which has quote context information. Could the tokenizer be parameterized to handle this? The tradeoff: the tokenizer would need additional state (the delimiter, whether it's quoted). The parser approach is simpler but requires post-tokenization scanning.

2. **Why allow `#` in words?** The `is_word_char()` function includes `#` (tokenizer.c:578). This allows filenames like `file#1.txt` to be tokenized as a single word. But can a shell script actually create such filenames without escaping? This is a design choice for maximum flexibility.

3. **Glob qualifier lookahead**: The tokenizer scans ahead to find glob qualifiers (tokenizer.c:2301-2327), which is expensive. Could this be deferred to the parser? The tokenizer does it for convenience, but it duplicates work the glob expander will do.

4. **Feature flags at tokenize time**: Lush gates many token types on runtime feature flags (FEATURE_EXTENDED_GLOB, etc.). This is more flexible than bash (which has compile-time conditionals) but slower. Is the performance cost acceptable?

5. **Re-tokenization of lookahead**: Calling `tokenizer_refresh_lookahead()` re-tokenizes the lookahead token, which is O(lookahead-token-length). Could the tokenizer cache classifications to avoid repeated tokenization of the same text?

