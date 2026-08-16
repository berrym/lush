# Section 5 -- Case Statements

## EBNF productions

```ebnf
case_statement   = "case" word "in" case_item* "esac" redir_list?

case_item        = pattern ( ";;" | ";&" | ";;&" | "esac" )
                 ;
                 
pattern          = pattern_part ( "|" pattern_part )*
                 ;

pattern_part     = ( WORD | VARIABLE | STRING | EXPANDABLE_STRING
                   | "*" | "?" | "[" | "]" | "=" )+
                 ;

word             = ( WORD | VARIABLE | STRING | EXPANDABLE_STRING
                   | ":" )+
                 ;

redir_list       = redir_op+
                 ;
```

**Notes on tokens:**
- `"case"`, `"in"`, `"esac"` are reserved words recognized contextually by the tokenizer (TOK_CASE, TOK_IN, TOK_ESAC).
- Patterns are NOT delimited by an optional leading `(` -- they begin immediately after `in` or `|` and terminate with `)`.
- The three case terminators `;;`, `;&`, and `;;&` are tokenized as TOK_SEMICOLON (when doubled), TOK_CASE_FALLTHROUGH, and TOK_CASE_CONTINUE respectively.
- An empty body (no commands between `)` and terminator) is syntactically valid and represents a case item with no children.

## AST nodes produced

| Node type | Produced by | Notes |
|-----------|-------------|-------|
| `NODE_CASE` | `case_statement` | Root node with `val.str` storing the test word; children are `NODE_CASE_ITEM` nodes in order. |
| `NODE_CASE_ITEM` | `case_item` | Represents one pattern and its body. `val.str` stores the patterns with a single-character terminator prefix: `'0'` for CASE_TERM_BREAK (`;;`), `'1'` for CASE_TERM_FALLTHROUGH (`;&`), `'2'` for CASE_TERM_CONTINUE (`;;&`). First child is the command sequence (or NULL if body is empty). |

## Context-sensitive behavior and lexer interactions

**Reserved word recognition:**
The parser expects TOK_CASE, TOK_IN, and TOK_ESAC as reserved words. These are recognized by the tokenizer's keyword mode when enabled. The parser calls `expect_token(parser, TOK_CASE)` at line 3424 to begin parsing.

**Pattern vs. word lexing:**
After `in` (or after `|` within patterns), the parser does not switch lexer modes. Patterns are built by concatenating consecutive tokens that match `token_is_word_like(type)` or specific operator types (TOK_MULTIPLY, TOK_QUESTION, TOK_GLOB, TOK_LBRACKET, TOK_RBRACKET, TOK_VARIABLE, TOK_ASSIGN). This allows glob syntax like `[abc]`, `*`, `?`, and variable references within patterns. The pattern collection loop (parser.c:3524-3562) terminates when it encounters `)` or `|`.

**Pattern delimiters:**
Patterns are **not** delimited by an optional `(` -- Lush does not accept `case word in (pattern) ...; esac`. Instead, the parser immediately starts collecting pattern tokens after `in` (or after `|` for subsequent patterns) and expects a closing `)` to mark the end of the pattern list.

**Terminator detection order:**
The tokenizer checks for `;;&` before `;&` before `;;` (parser.c:3647-3672). This is important because all three begin with semicolons. The order in the tokenizer (tokenizer.c:1256-1277) ensures that `;;& ` is recognized as a single token before testing for `;&`.

**Separator skipping strategy:**
After `)` closes the pattern list, the parser skips only newlines and whitespace (not semicolons) at line 3622-3625. During command parsing, it skips newlines, whitespace, and comments but **explicitly does not skip semicolons** because semicolons signal case terminators (line 3638-3644). This allows detection of the single-semicolon separator between commands (which is consumed at line 3670 and treated as command separation, not a case terminator).

## Notable behaviors and surprises

**Three terminator types with specific semantics:**
- `;;` (TOK_SEMICOLON followed by TOK_SEMICOLON) -- CASE_TERM_BREAK: stops execution, does not test subsequent patterns.
- `;&` (TOK_CASE_FALLTHROUGH) -- CASE_TERM_FALLTHROUGH: execute next case item's commands without testing its patterns (bash 4.0+ feature).
- `;;&` (TOK_CASE_CONTINUE) -- CASE_TERM_CONTINUE: test the next case item's patterns (bash 4.0+ feature).

All three are recognized as distinct tokens by the tokenizer and handled in sequence (parser.c:3648-3672).

**Empty case items are valid:**
A pattern immediately followed by a terminator with no intervening commands (e.g., `foo) ;;`) parses successfully. The `commands` pointer remains NULL (line 3695), and no child command node is added to the case item (line 3695-3697).

**Multi-pattern items:**
Multiple patterns separated by `|` are concatenated into a single string with `|` delimiters preserved in the AST (parser.c:3575-3596). The parser does not create separate NODE_CASE_ITEM nodes for each pattern; instead, a single case item contains `val.str = "pattern1|pattern2|pattern3"` (with terminator prefix).

**Terminator encoding in patterns:**
The terminator type is stored as a single-character prefix in the case item's `val.str` (parser.c:3699-3713):
```c
new_pattern[0] = '0' + (char)terminator;  // '0', '1', or '2'
strcpy(new_pattern + 1, case_item->val.str);
```
This means the actual patterns are at offset 1 in the string. Executors must extract the terminator before matching patterns.

**Trailing redirections:**
After `esac`, the parser optionally accepts redirections (e.g., `case x in a) echo a ;; esac > file`) via `parse_trailing_redirections` (parser.c:3735-3739). These are attached to the NODE_CASE node, not to individual case items.

**Test word concatenation:**
The word following `case` is collected by concatenating consecutive word-like tokens until `in` is encountered (parser.c:3437-3471). This allows test words like `case "$var" in`, `case $((x+1)) in`, or `case :$PATH: in` without requiring explicit quote handling in the parser.

**Single semicolon inside case body:**
Within a case body, a single `;` is treated as a command separator and does not trigger case item termination (parser.c:3669-3671). Only `;;`, `;&`, or `;;&` end the item.

**Default terminator is CASE_TERM_BREAK:**
If no explicit terminator is found before `esac`, the `terminator` variable defaults to CASE_TERM_BREAK (parser.c:3513). This can occur if a pattern is followed immediately by `esac` (though this is unusual).

## Open questions

- **Handling of bare `esac` without a preceding terminator:** If the parser encounters `esac` while in the command-parsing loop (line 3633-3635), it breaks with the default terminator (CASE_TERM_BREAK). Is this intentional, or should it require an explicit terminator on the final case item?

- **Order of terminator checks in the tokenizer:** The tokenizer checks `;;&` before `;&` at lines 1256-1272. This is correct, but what if the input contains `;; &` (with a space)? This would tokenize as `;;` followed by `&`, which is different from `;&`. Is this the intended behavior?

- **Behavior with nested structures:** The parser uses `parse_logical_expression` to parse commands within case bodies (line 3675). This allows arbitrary logical expressions and pipelines. Can case items contain control structures like `if` or `for`? Testing would clarify.

- **Variable expansion in patterns:** Patterns can contain variables (e.g., `case x in $y) ...`). Are these expanded at parse time (unlikely) or at runtime during pattern matching? The AST stores them as literal tokens, suggesting runtime expansion.

- **Glob vs. literal matching:** Patterns like `*`, `[abc]`, and `?` are accepted and stored literally in the AST. The executor is responsible for glob expansion during matching. Are these always treated as globs, or is quoting (e.g., `"*"`) respected?

---

**Grammar version:** 1.0  
**Parser location:** /Users/mberry/Lab/c/lush/src/parser.c:3423-3742  
**AST node types:** NODE_CASE, NODE_CASE_ITEM  
**Tokenizer features:** TOK_CASE, TOK_IN, TOK_ESAC, TOK_CASE_FALLTHROUGH, TOK_CASE_CONTINUE, TOK_SEMICOLON  
**References:**
- Bash 4.0+ case fall-through: `;&` and `;;&` terminators
- POSIX shell case: https://pubs.opengroup.org/onlinepubs/9699919799/utilities/V3_chap02.html#tag_18_06
