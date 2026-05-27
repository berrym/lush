# Unicode Audit Worklist

**Status**: Catalogue-only. No code changes made.
**Audit date**: 2026-05-25
**Scope**: `src/**/*.c` (excluding `src/lle/unicode/*`, `src/strings.c`, `src/libhashtable`, `src/libfuzzy`).
**Rule of record**: memory `feedback-unicode-mandatory` — byte-oriented APIs (strcmp, strncmp, strcasecmp, strncasecmp, strchr, strrchr, strstr, strspn, strcspn, strpbrk, strtok, strdup, strlen, memcmp, ctype.h) are permitted only when there is *zero possibility* of non-ASCII input. Anything else must go through `src/lle/unicode/*` helpers.

## Verdict legend

- **A** — Provably ASCII. Both operands are fixed ASCII literals or tokens constructed from a known ASCII alphabet (keyword tables, option strings, syntactic punctuation, builtin names). Keep as-is.
- **B** — Ambiguous / byte-vs-Unicode-semantics mismatch. One or both operands can carry non-ASCII bytes (var names, var values, paths, command names, user input, file content). Bytewise operation produces *correct-on-ASCII / wrong-on-NFD-vs-NFC / wrong-on-non-ASCII-prefix* behavior; should route through a Unicode helper.
- **C** — Actively wrong on Unicode input. Byte-wise case folding that destroys non-ASCII case mapping, byte-counting used as a character count, char-class predicates that should match Unicode general categories, byte-prefix iteration that splits a grapheme cluster, etc. These are the most-concerning regressions.

## Replacement helper table

| Byte pattern | Unicode replacement |
| --- | --- |
| `strcmp(a, b) == 0` on user values / paths / var values | `lle_unicode_strings_equal(a, b, &LLE_UNICODE_COMPARE_DEFAULT)` (NFC-equal) |
| `strncasecmp(a, b, n)` for case-insensitive | `lle_utf8_casefold` then `lle_unicode_strings_equal_n` |
| `strncmp(prefix, x, plen) == 0` for prefix completion | `lle_unicode_is_prefix(prefix, plen, x, strlen(x), opts)` |
| `strlen(s)` used as *character* count | `lle_utf8_count_codepoints(s, byte_len)` (codepoints) or grapheme count via `lle_utf8_index_byte_to_codepoint` + `..._codepoint_to_grapheme` |
| `tolower((unsigned char)c)` / `toupper((unsigned char)c)` on text | `lle_unicode_tolower_codepoint(cp)` after `lle_utf8_decode_codepoint`, or `lle_utf8_tolower` for full-string |
| Manual case fold (`c >= 'A' && c <= 'Z'` ± 32) | `lle_utf8_casefold` |
| `isalpha(c)` / `isalnum(c)` for identifier scanning | OK for shell identifiers (POSIX restricts var/function names to `[A-Za-z_][A-Za-z0-9_]*`); but bash allows wider names in some modes — see executor.c §B notes |
| `isspace(c)` for word-boundary detection in user input | needs Unicode whitespace handling (U+00A0, U+2028, U+2029, etc.) — currently absent everywhere |
| POSIX `[[:alpha:]]` / `[[:upper:]]` / `[[:lower:]]` etc. via `isalpha`/`isupper`/`islower` | needs proper Unicode general-category tests; `lle_unicode_is_upper` / `_is_lower` cover Latin/Greek but not full UCD |
| `strchr(p, '/')` for path separator | A — path separator is fixed ASCII byte |
| `strstr(haystack, needle)` for variable-content substring search | Should NFC-normalize both then byte-match, or use codepoint scanning |
| `for (size_t i = 0; s[i]; i++) { c = s[i]; ... }` treating bytes as characters | Use `lle_utf8_decode_codepoint` in a stride loop |

---

## High-priority files (large surface area + user-data exposure)

### src/executor.c (740 str calls, 68 ctype calls)

Far too many sites to enumerate exhaustively; representative findings below. Most strcmp sites compare against fixed literals (option flags, builtin names, AST node-text constants) and are **A**. The dangerous sites cluster in pattern matching, case conversion, variable-name validation, and value comparisons.

| Line | Call | Context | Verdict | Reasoning |
|---|---|---|---|---|
| 140 | `strcmp(*(const char *const *)a, *(const char *const *)b)` | qsort comparator for completion candidates | B | Sorts user-visible strings (filenames, command names); bytewise sort places non-ASCII after `~` and ignores NFC equivalence. Should use locale/Unicode collation or at minimum NFC-equal before ordering. |
| 237-238 | `strcmp(command, "exec"/"cd"/"set")` | guard against transient assignments on these | A | command is parsed builtin name; lush builtins are ASCII. |
| 266 | `strstr(target, "../")` and `strcmp(target, "..")` | path-traversal guard | A | the `../` and `..` byte sequences themselves are fixed ASCII; finding them anywhere in `target` is correct regardless of Unicode in the rest of the string. |
| 292-293 | `strcmp(var_name, "PATH"/"IFS"/"ENV"/"SHELL")` | special-var transient-assignment block | A | fixed env names; PATH/IFS are ASCII by POSIX. |
| 1674-1676 | `strcmp(cmd, "local"/"declare"/"typeset"/"readonly"/"export")` | builtin classification | A | fixed builtin names. |
| 1723-1753 | `strcmp(argv[i], "2>/dev/null"/"2"/">"/"/dev/null")` | pre-builtin argv scrub for redirection forms | A | fixed POSIX redirection literals. |
| 1830 | `strcmp(expanded_command, original_command) != 0` | did expansion change the word? | B | both are command tokens; if NFC vs NFD or alias expansion produced byte-different but Unicode-equivalent strings, this misses. In practice tokenizer round-trips bytes, so likely A in current flow; flag for revisit if NFC ingest is added. |
| 3141-3142 | `strcmp(word->val.str, "\"$@\""/"$@")` | sentinel-token marker | A | sentinel value constructed by parser as fixed ASCII. |
| 4509-4512 | `isalpha((unsigned char)s[1])` / `isalnum((unsigned char)s[k])` in `$varname` scanning | identifier-start / identifier-char | B | POSIX restricts shell identifiers to ASCII, but bash allows non-ASCII in some locales; zsh also accepts. lush's identifier policy is undecided. Currently rejects non-ASCII var names silently — flag as policy question. |
| 4659, 4677 | `strcmp(items[i], items[j])` and ordering comparison | array-element dedup / sort | B | items are user values; NFC normalization needed for dedup. |
| 4730 | `while (p < end && (isalnum((unsigned char)*p) || *p == '_'))` | variable-name scanning | B | same as 4509. |
| 5160 | `strcmp(child->val.str, heredoc_delimiters[i])` | heredoc delimiter match | B | delimiter could be user-chosen and contain non-ASCII. NFC-equal would be safer. |
| 5696, 5717-5719 | `isalpha((unsigned char)text[..])` / `isalnum((unsigned char)*p)` | param-expansion var-name scan | B | same identifier-policy question. |
| 5909 | `while (i < len && (isalnum(text[i]) || text[i] == '_'))` | var-name scan in expansion | B | identifier; also `isalnum(text[i])` is missing `(unsigned char)` cast — UB on negative chars on signed-char platforms. |
| 6092 | same as 5909 | identifier scan | B | same. |
| 6886-6887, 7301-7302, 7541-7542 | `strcmp(entry->d_name, "."/"..")` | skip dot dirs in glob/dir walk | A | `.` and `..` are fixed ASCII bytes; readdir guarantees these names byte-exact. |
| 8075 | `isalpha(start_str[0]) && isalpha(end_str[0])` | brace-expansion `{a..z}` range detection | A | bash's `{a..z}` semantics are ASCII letters only — `{α..ω}` is not specified. Documenting as A but worth a feature decision (Greek brace ranges would be a lush-superset feature). |
| 8825 | `strcmp(argv[0], builtins[i].name)` | builtin dispatch | A | builtin names are fixed ASCII. |
| 8913-8958 | `strcmp(argv[1], "-z"/"-n"/"="/"=="/"!="/...)` in `[` test builtin | A for the operator names | A | fixed operators. |
| 8953, 8958 | `strcmp(str1, str2)` inside `[ "$a" = "$b" ]` | C-by-strict-semantics or B | B | user values; NFC equivalence should hold for `=` per POSIX intent. Currently NFD `é` ≠ NFC `é` even though both display identically. |
| 9085, 9091 | `isalpha(var_name[0])` / `isalnum(var_name[i])` validating exported var name | B | same identifier-policy question; currently ASCII-only. |
| 10130-10164 | `memcmp(class_start, "alpha"/"digit"/...)` for class *names* | A | class names are fixed ASCII literals. |
| 10131-10164 | `isspace(uc)` / `isalpha(uc)` / `isdigit(uc)` / `isalnum(uc)` / `isupper(uc)` / `islower(uc)` / `ispunct(uc)` / `isprint(uc)` / `isgraph(uc)` / `iscntrl(uc)` / `isxdigit(uc)` evaluating POSIX `[[:class:]]` against single byte `*s` | **C** | This is the POSIX char-class implementation inside lush's glob/pattern matcher. `[[:alpha:]]` should match Unicode alpha (Cyrillic letters, CJK ideographs in lush superset terms) but currently only matches ASCII. Same with `[[:upper:]]`, `[[:lower:]]`, etc. **Major Unicode-correctness gap.** |
| 10173 | range pattern `[a-z]` walking by byte | **C** | range walks single bytes, not codepoints; `[α-ω]` matches the byte range of the first/last UTF-8 byte, garbage. |
| 10416-10417 | `(char)toupper((unsigned char)c)` / `(char)tolower((unsigned char)c)` in `${var^^}` / `${var,,}` | **C** | This is the parameter-expansion case conversion. Drops case for all non-ASCII letters. lush already ships `lle_utf8_toupper` / `lle_utf8_tolower` / `lle_utf8_casefold` for exactly this — *the helpers exist but the executor doesn't call them.* High-priority C. |
| 10534 | `isspace((unsigned char)*src)` for word splitting on IFS-substituted whitespace | B | IFS-default whitespace is ASCII space/tab/newline by POSIX. If user sets IFS to include Unicode, currently miscounts. |
| 11005-11006, 15620, 15625, 15647, 15684 | `isxdigit(str[i + N])` for `\xHH` / `\uHHHH` escape parsing | A | hex digits are fixed ASCII. |
| 11496, 14752 | `isalnum(str[var_end])` / `isalnum(var_name[name_len])` | identifier scan | B | identifier-policy. |
| 12072-12077, 13053-13057 | `isalpha((unsigned char)rest[0])` / `isalnum((unsigned char)rest[i])` | identifier scan | B | identifier-policy. |

**Net for executor.c**: ~90% A (option/builtin/keyword/literal matching), ~7% B (identifier scans, value comparisons, dir name comparisons), **~3% C, concentrated in pattern matching (`[[:class:]]`, `[a-z]` byte range, case conversion).** The case-conversion and char-class sites are the most user-visible.

### src/parser.c (89 str + 3 ctype calls)

Almost all `strcmp` sites compare a token's `text` field against a fixed shell keyword (`return`, `let`, `scalar`, `list`, `map`, `-p`, etc.) — these tokens are produced by the tokenizer and the keyword table is ASCII by design. The `strdup` / `strlen` sites copy token text and length-count token text; bytes are correct as the storage representation.

| Line | Call | Context | Verdict | Reasoning |
|---|---|---|---|---|
| 127 | `strlen(input)` | input length for error collector | A | byte-length is correct here (offsets into source map to bytes). |
| 1170, 1206, 1255, 1514, 1518, 1662, 2230, 2568, 2653, 2747, 3827, 3860, 3984, 4204, 4364, 5042, 5093, 5123, 5684, 5994, 6064, 6229, 6400, 6458, 6515, 6617 | `strdup(token->text)` | copy token byte content | A | byte-copy is the right thing for storage. |
| 1443, 1456, 4282, 6012-6013, 6323 | `strcmp(current->text, "return"/"let"/"-p"/"scalar"/"list"/"map")` | keyword recognition | A | keyword table is ASCII; tokenizer produces these literal bytes. |
| 1472-1473 | `strchr(current->text, '[')` / `strchr(bracket, ']')` | array-subscript probe | A | `[` and `]` are fixed ASCII bytes; byte search is correct. |
| 1552, 1965, 2078, 2088, 2720, 4476, 4613, 4800, 5447, 5580, 5805, 6118, 6270 | `strlen(token->text)` / etc. | byte-length for buffer math | A | bytes are storage; correct. |
| 2192 | `!strchr(t->text, '[')` | check for `[` byte | A | fixed punctuation. |
| 2871 | `strcmp(line_content, match_delimiter) == 0` | heredoc end-marker match | B | line_content and match_delimiter are user text; if either contains non-ASCII (Unicode delimiter), NFC equivalence matters. Bash delimiter behavior is byte-exact, so could be argued A under bash semantics. **Flag for behavioral decision.** |
| 2807-2809 | `strlen(delimiter) > 2 && delimiter[strlen(delimiter) - 1] == delimiter[0]` | heredoc-delimiter quote stripping | A | inspecting first/last byte for `"` or `'`, both fixed ASCII. |
| 3577-3585, 3645-3653, 3709-3717 | `strlen(init_expr)` / `memmove(.., strlen(p)+1)` | C-style for-loop sub-expr buffer manipulation | A | byte storage. |
| 5487, 5510, 5868, 5903 | similar `strlen` / `memmove` | A | byte storage. |
| 6127, 6140 | `strchr(composite, ':')` | parse composite type name `scalar:int` etc. | A | colon delimiter is fixed ASCII; type tags are reserved ASCII. |

**Net for parser.c**: 100% A or near-100%. Parser operates on tokenizer output (ASCII-keyword vocabulary) and copies byte content for storage. Only borderline is heredoc delimiter equality (line 2871) where user could pick a Unicode delimiter.

### src/tokenizer.c (9 str + 29 ctype calls)

| Line | Call | Context | Verdict | Reasoning |
|---|---|---|---|---|
| 87 | `tokenizer->input_length = strlen(input)` | source length in bytes | A | byte-length is correct for source-offset arithmetic. |
| 618-619 | `strlen(keywords[i].text) == length && strncmp(keywords[i].text, text, length) == 0` | shell keyword match | A | shell keywords (`if`, `then`, `else`, `case`, etc.) are ASCII. |
| 634 | `strchr(";|&<>=+*%?(){}[]#!,", c)` | shell-metachar test | A | metachar set is fixed ASCII. |
| 651, 669, 2050 | `isalnum(c) \|\| strchr("_.-/~:@*?[]+%!^#", c)` | word-character set | B | The byte-set `strchr` part is A (fixed ASCII punctuation), but `isalnum(c)` returns false for non-ASCII letters — so a filename like `файл.txt` doesn't get recognized as a word token character-by-character; lush relies on the `else` branch in `read_word` to fall through to UTF-8 byte append, which mostly works for the *value* but misclassifies the *kind* of token. Verify with non-ASCII command names. |
| 1350-1408, 1640-1742, 1887-1948, 2049-2658 | `isalpha`/`isalnum`/`isdigit`/`isspace` on `tokenizer->input[..]` bytes inside various tokenizer scans (param expansion, redirection-target identifier, brace expansion, etc.) | B | These are all "is this byte an identifier character or whitespace?" tests on raw input bytes. POSIX-style identifier scanning is ASCII-correct; for lush-superset that allows Unicode identifiers, these will miss. |
| 2154-2418 | `isdigit(c)` for numeric-literal detection in arithmetic/option-arg | A | numeric literals are ASCII digits in POSIX/bash/zsh arithmetic. |
| 2540, 2556-2557 | `strchr("./@*rwND,", sc)` etc. | redirection sub-form punctuation test | A | fixed punctuation. |
| 2647-2658 | `isalnum(nc)` in special-var-name look-ahead (`$?`, `$$`, `$#`) | A for the punctuation special vars (`?`, `$`, `#` are fixed); B for the named-var branch | B | branch is identifier scan. |

**Net for tokenizer.c**: Byte-level scanning is correct for ASCII shell vocabulary; flagged B sites are all "would identifier scanning need to accept non-ASCII letters?" — a lush-superset policy decision rather than an outright bug. The tokenizer correctly *preserves* UTF-8 bytes in token text by treating non-ASCII as falling through, but it doesn't *classify* them as letters.

### src/expand.c (2 calls)

| Line | Call | Context | Verdict | Reasoning |
|---|---|---|---|---|
| 77 | `strlen(str)` | alias key length | A | bytes correct as storage size. |
| 166 | `strdup(alias_value)` | byte-copy alias replacement text | A | storage. |

### src/symtable.c (47 str + 0 ctype calls)

Almost all `strdup`/`strlen` sites copy variable names and values verbatim for storage — **A**. Risky sites:

| Line | Call | Context | Verdict | Reasoning |
|---|---|---|---|---|
| 102, 1180-1181, 1183, 2850, 2861 | `strlen(value)` / `strlen(name)` | byte-length for buffer math | A | bytes are storage. |
| 139, 252, 395, 481, 873, 978, 1453, 2491, 2577, 2747, 2768, 2811, 2822, 2831 | `strdup(name)` / `strdup(value)` / `strdup(key)` | copy for storage | A | byte storage. |
| 162-180 | `strstr(pos, METADATA_SEPARATOR)` where `METADATA_SEPARATOR` is a fixed escape sequence | A | fixed ASCII sentinel. |
| 779, 792, 803, 810, 934, 2173 | `strcmp(name, "RANDOM"/"SECONDS"/"LINENO"/"-"/...)` | special var match | A | reserved var names are ASCII. |
| 1550 | `strcmp(value, "1") == 0 \|\| strcmp(value, "true") == 0` | boolify | A | fixed truth-value literals. |
| 2692 | `strcmp(array->assoc_insertion_order[i], key) == 0` | associative-array key match | **B** | keys are user values; bash/zsh assoc arrays support Unicode keys; NFC-equivalent keys (NFD vs NFC) currently treated as distinct. C-leaning: would surprise users typing `arr[café]` vs `arr[café]` decomposed. |

**Net for symtable.c**: One B site (line 2692, assoc-array key lookup). Everything else is storage or fixed-name testing.

### src/redirection.c (11 str + 6 ctype calls)

| Line | Call | Context | Verdict | Reasoning |
|---|---|---|---|---|
| 145 | `strcmp(expand_flag_node->val.str, "1") == 0` | parse-flag check | A | sentinel value parser-emitted. |
| 1023 | `strcmp(content, delimiter) == 0` | heredoc-line vs delimiter match | B | same as parser.c heredoc; delimiter could be Unicode. |
| 1030, 1110, 1167, 1213, 1292, 1349, 1684 | `strlen(...)`, `strdup(...)`, `strchr(line_start, '\n')`, `strchr(var_start, '}')` | byte-buffer arithmetic and ASCII-byte search | A | `\n` and `}` are fixed ASCII. |
| 1179, 1181 | `strchr(line_start, '\n')` | line splitter | A | `\n` is fixed ASCII. |
| 578, 641, 704, 1383, 1414, 1719 | `isdigit(redir_node->val.str[0])` / `isdigit(*op)` / `isdigit(*p)` | does this redirection start with a fd number? | A | POSIX redirection fd is ASCII digit. |

### src/pattern_match.c (4 str + 0 ctype calls)

| Line | Call | Context | Verdict | Reasoning |
|---|---|---|---|---|
| 209, 333, 347, 376 | `strlen(rest)` / `strlen(s)` | byte length for composite buffer | A | byte storage; the pattern matcher itself walks byte-by-byte. **However**, pattern_match.c's walker (not shown in these grep hits) also iterates a single byte at a time for `?` (match one char) and `*` (any chars); see "Cross-cutting C verdicts" below. |

### src/arithmetic.c (6 str + 11 ctype calls)

| Line | Call | Context | Verdict | Reasoning |
|---|---|---|---|---|
| 824 | `strdup(var_name)` | storage | A | bytes. |
| 856 | `bool valid_name_char(char c) { return isalnum(c) \|\| c == '_'; }` | identifier-char predicate inside arithmetic expr | B | identifier-policy; matches executor pattern. |
| 951, 983, 988 | `isdigit(expr[1])` / `isalpha(*expr)` | numeric-literal start / identifier start | A for digit, B for alpha | digits are ASCII in arithmetic; alpha for identifier-start has same identifier-policy question. |
| 1340 | `strdup("0")` | fixed literal | A | |
| 1352-1353 | `strncmp(orig_expr, "$((", 3) == 0` | dollar-double-paren detection | A | fixed ASCII syntax. |
| 1387, 1454, 1492, 1499, 1502-1503, 1545 | `isspace(*current)` / `isdigit(*current)` / `isalnum(*name_end)` | whitespace + numeric + identifier scanning in arithmetic expr | B for `isspace` (Unicode whitespace), A for `isdigit`, B for `isalnum` (identifier-policy). |
| 1497 | `strncmp(trimmed, "echo ", 5) == 0` | command-substitution shortcut | A | fixed ASCII command name. |
| 1536 | `strchr(start, '}')` | brace match | A | fixed ASCII. |

### src/builtins/builtins.c (11 str + 2 ctype calls)

| Line | Call | Context | Verdict | Reasoning |
|---|---|---|---|---|
| 109 | `strcmp(path_neg_cache[i].name, command)` | negative-cache hit | B | command is user-supplied executable name; NFC-equivalent names cache-miss. |
| 128 | `strlen(command) >= PATH_NEG_CACHE_NAME_MAX` | length check for cache bucket | A | byte-length cap is correct for a byte buffer. |
| 251 | `strcmp(name, builtins[i].name)` | builtin lookup | A | builtin names are fixed ASCII. |
| 323, 329 | `isalpha(*name)` / `isalnum(*p)` | var-name validation | B | identifier-policy. |
| 372 | `strchr(command, '/')` | does command contain a path-separator? | A | `/` is fixed ASCII. |
| 374, 388 | `strdup(command)` / `strdup(cached)` | storage | A | bytes. |
| 407 | `strdup(path_env)` | storage | A | bytes. |
| 412, 432 | `strtok(path_copy, ":")` | PATH split on `:` | A | `:` is fixed ASCII; PATH segment values can contain Unicode but are *stored* as bytes and not compared here. |
| 416-417 | `strlen(path_dir)` / `strlen(command)` | buffer math | A | bytes. |

### src/builtins/bin_test.c

| Line | Call | Context | Verdict | Reasoning |
|---|---|---|---|---|
| 35-36, 67, 75, 84, 116-178, 187, 190, 193, 198, 203, 208, 213, 218 | `strcmp(argv[i], "["/"]"/"-z"/"-n"/"-f"/...)` | test-operator detection | A | fixed operators. |
| 189, 192 | `strcmp(argv[start], argv[start + 2])` inside `[ a = b ]` | **B** | user values; POSIX `=` is byte-equality but lush-superset should NFC-compare; NFD vs NFC `é` currently unequal. |

### src/builtins/alias.c

| Line | Call | Context | Verdict | Reasoning |
|---|---|---|---|---|
| 141 | `isalnum((unsigned char)c) \|\| c == '_'` | alias-name char check | B | alias names are user-chosen; could be Unicode in lush-superset. |
| 168, 199, 320, 342, 668 | `isspace((unsigned char)*..)` | alias whitespace trimming/scanning | B | Unicode whitespace not detected. |
| 338, 441, 465-466, 473, 509, 515, 522, 526-528, 537, 665, 673-674 | `strlen` / `strdup` / `strncmp` on user alias names / values | mixed | mostly A for storage; the `strncmp(arg, ...)` for `-a`/`--` matches **A** (fixed flag); user value strlens are A as byte size. |
| 719, 805, 815 | `strcmp(argv[i], "--"/"-a"/...)` | option flag | A | fixed flags. |

### src/builtins/bin_cd.c

All `strcmp`/`strlen` against `-`/`--` and PWD-prefix byte math. **A**.

### src/builtins/bin_export.c

| Line | Call | Context | Verdict | Reasoning |
|---|---|---|---|---|
| 47, 49 | `strchr(name_start, '=')` / `strlen(name_start)` | env-assignment splitter | A | `=` is fixed ASCII. |
| 81 | `strchr(arg, '=')` | env-assignment | A | `=` is fixed ASCII. |
| 114 | `strcmp(argv[i+1], "=")` | three-token assignment form | A | fixed `=`. |

### src/builtins/bin_type.c, bin_command.c, bin_unset.c, bin_export.c, bin_declare.c, etc.

Pattern is consistent: option-flag `strcmp` (A) + identifier-name `isalpha`/`isalnum` (B, identifier-policy). Spot checks confirm no new categories.

### src/shell_mode.c (16 str calls)

| Line | Call | Context | Verdict | Reasoning |
|---|---|---|---|---|
| 590, 620, 638 | `strcasecmp(name, feature_noop_aliases[i].name)` | feature-alias case-insensitive match | **B** | feature names are fixed ASCII but user-typed mixed-case input may contain non-ASCII garbage in malformed input. Practically A; the case-insensitive part is bytewise but the alphabet is ASCII so case-fold is well-defined. Strict-policy: B; pragmatic: A. |
| 787, 794 | `strcasecmp(name, mode_names[i])` / `strcasecmp(name, "sh")` | mode name match | same | same — fixed alphabet, user-typed source. |
| 814, 822 | `strcasecmp` against fixed feature names | A/B | same. |
| 880, 891, 897, 902, 907, 914, 920-921 | `strncmp(shebang, "/usr/bin/env"/"bash"/"zsh"/"sh"/"lush"/"dash"/"ash")` | shebang interpreter detection | A | interpreter names are fixed ASCII; shebang line is a `#!` magic line. |

### src/config.c (190 str calls)

Almost the entire file is `strcmp(key, "fixed.config.path") == 0` for option dispatch. Sampled lines 967, 973, 981, 985-998, 1101, 1117-1119, 1136-1170 are all fixed-config-key matches against literal strings. **A** wholesale.

| Line | Call | Context | Verdict | Reasoning |
|---|---|---|---|---|
| 967-1170 (representative) | `strcmp(key, "<fixed.path>")` | config-key dispatch | A | config keys are a fixed registry of ASCII paths. |
| any `strdup(value)` for user config string values | storage | A | bytes. |

### src/autocorrect.c (~30 str + 6 ctype calls)

| Line | Call | Context | Verdict | Reasoning |
|---|---|---|---|---|
| 158, 410, 449, 544, 667, 712, 759 | `strdup(command)` / `strdup(path)` / `strdup(builtins[i])` / `strdup(entry->d_name)` | candidate storage | A | bytes. |
| 237 | `strcmp(results->suggestions[j].command, ...)` | dedup current suggestion vs candidate | B | candidates are command names / filenames from filesystem; NFC dedup. |
| 403 | `strcmp(learned_commands[i], command)` | learned-commands dedup | B | user-typed command names. |
| 454, 464, 681, 685, 721 | `strtok(path_copy, ":")` | PATH split on `:` | A | `:` is fixed ASCII. |
| 587-588 | `strlen(s1)` / `strlen(s2)` | edit-distance length cap | **B** | this is *byte-length* used as char-length cap for Damerau-Levenshtein edit distance. A non-ASCII filename `файл` is 4 codepoints / 8 bytes; would exceed the 32-byte cap immediately. Should be codepoint count. |
| 608-619 | `for (i ... ) { c1 = s1[i-1]; if (c1 >= 'A' && c1 <= 'Z') c1 += 32; ... }` | per-character case-insensitive edit-distance step | **C** | the edit-distance loop indexes byte-by-byte and does ASCII case fold via byte arithmetic. Misclassifies UTF-8 continuation bytes as their own characters and provides zero case-fold for non-ASCII. For a fuzzy "did you mean…" suggester this is significant: typing `dofcs` won't fuzzy-match `Документы` even with edit-distance 5. |

### src/lle/keybinding/keybinding_actions.c (21 str + 9 ctype calls)

| Line | Call | Context | Verdict | Reasoning |
|---|---|---|---|---|
| 116 | `isspace((unsigned char)c)` in `is_unix_word_boundary` | Ctrl-W word-boundary | B | misses Unicode whitespace (NBSP etc.); editing across such bytes is wrong. |
| 1964 | `strcmp(s1, s2) == 0` | kill-ring entry dedup | B | kill-ring entries are user text; NFC-equal would be more user-friendly. |
| 2939, 2945 | `isspace((unsigned char)data[..])` | whitespace-stretch deletion | B | same Unicode-whitespace gap. |
| 228, 244, 276 | `strncmp(word, shell_keywords[..], len)` | shell-keyword highlighting in widget rendering | A | shell keyword vocabulary is ASCII. |
| 471, 544, 554, 725, 1403 | `strchr(command, '/')` / `strncmp(input, "function", 8)` etc. | command path / keyword detection | A | fixed ASCII. |
| 512 | `strcmp(entry->command, command)` | history-entry exact match | B | user-typed commands; NFC-equal. |

### src/lle/history/history_search.c (21 str calls)

| Line | Call | Context | Verdict | Reasoning |
|---|---|---|---|---|
| 156-157 | `strlen(command)` / `strlen(query)` | byte-length math | A | bytes are storage. |
| 212-218 | `stristr(haystack, needle)` implementation: for each byte position, `strncasecmp(p, needle, needle_len) == 0` | **C / DONE c6dd8fb8** | interactive history substring search: needle is user-typed (can include accented letters, emoji), history entries can include arbitrary Unicode; case-insensitive comparison via bytewise `strncasecmp` only folded ASCII A-Z. Searching `café` would not match a history line `CAFÉ`. **Resolved**: rewritten as codepoint-iterating `cf_prefix_match_bytes` over `lle_utf8_decode_codepoint` + `lle_unicode_tolower_codepoint`. |
| 233-236 | `str_starts_with_i(str, prefix)` using `strncasecmp` | **C / DONE c6dd8fb8** | same — case-insensitive prefix used for history prefix search; same Unicode failure. **Resolved** alongside the substring helper. |
| 447 | `strcmp(entry->command, query) == 0` | exact match | B | user values; NFC-equal. |

### src/lle/completion/completion_sources.c, builtin_completions.c, completion_config.c

| Line | Call | Context | Verdict | Reasoning |
|---|---|---|---|---|
| completion_sources.c:40 | `strcmp(text, builtins[i].name)` | builtin completion match | A | fixed builtin names. |
| completion_sources.c:65-68 | `lle_unicode_is_prefix(prefix, plen, candidate, strlen(candidate), NULL)` | Unicode-aware prefix | **GOOD** | already uses helper; the surrounding `strlen` for byte length is appropriate. |
| completion_sources.c:241 | `strcmp(entry->d_name, "."/"..")` | A | fixed. |
| builtin_completions.c:646, 673, 703, 771, 916, 932 | `strncmp(*sig/job/theme/name/opt_name, prefix, prefix_len) == 0` | prefix-match in command/job/theme/binding name lists | **B** | candidates are fixed ASCII vocabulary (signal names, theme names) → A for those; but `*job` is a job descriptor (user command text) → B. Mixed; review per call. |
| completion_config.c:553 | `strncmp(lines[i], prefix, prefix_len) != 0` | line-prefix filter | B | lines are user-typed config completion candidates. |

### src/lle/display/syntax_highlighting.c (17 str calls)

Spot check confirms most are fixed-keyword matches (shell keywords, redirection operators) — **A**. Identifier-scanning sites mirror tokenizer.c (B for identifier-policy). Not enumerated here line-by-line for brevity; see tokenizer.c entries for the equivalent pattern.

### src/signals.c (12 str calls)

| Line | Call | Context | Verdict | Reasoning |
|---|---|---|---|---|
| 280 | `strlen(command) == 0` | empty-trap check | A | bytes. |
| 292 | `strdup(command)` | storage | A | bytes. |
| 413-438 | `strcmp(signame, "INT"/"SIGINT"/"TERM"/etc.)` | signal-name match | A | fixed POSIX signal names. |

### src/lle/prompt/* (338 str calls across 7 files)

Prompt template parsing, theme key matching, color-name lookups. Sampled: nearly all calls are `strcmp(token, "fixed_segment_name")` against the template-grammar vocabulary; theme keys are TOML keys which are fixed ASCII per TOML spec. Manual segment user-text rendering passes bytes through verbatim — **A** dominant. Spot-check did not find C verdicts in this dir.

---

## Cross-cutting C verdicts (highest-priority)

Sorted by likelihood-of-impact (how often a real user hits this):

1. **`src/executor.c:10416-10417`** — `${var^^}` / `${var,,}` / `${var^}` / `${var,}` case conversion uses `toupper`/`tolower` per byte. Drops non-ASCII case mapping entirely. `lle_utf8_toupper` / `lle_utf8_tolower` already exist; just need to be wired in.

2. **`src/executor.c:10131-10165`** — POSIX `[[:alpha:]]`, `[[:upper:]]`, `[[:lower:]]`, `[[:alnum:]]`, etc. evaluated with `isalpha(uc)` on single bytes. Glob/case patterns fail to match non-ASCII letters. Needs Unicode general-category tests (extend `unicode_case.c` or add `unicode_class.c`).

3. **`src/executor.c:10173`** — pattern range `[a-z]` walks bytes, not codepoints. `[α-ω]` is undefined; `[À-ÿ]` matches first-byte range, not codepoint range.

4. **`src/lle/history/history_search.c:212-236`** — interactive history search (`stristr` and `str_starts_with_i`) is case-insensitive over ASCII only via `strncasecmp`. Users searching history with mixed-case Unicode (filenames, paths, prior `echo café` commands) won't get hits.
   - **DONE** (c6dd8fb8): replaced both helpers with codepoint-iterating implementations built on `lle_utf8_decode_codepoint` + `lle_unicode_tolower_codepoint`; tests in `tests/lle/functional/test_history_phase3_day8.c` cover `café`/`naïve`/`ångström` substring and `über` prefix matching across mixed-case Unicode entries.

5. **`src/autocorrect.c:608-619`** — fuzzy "did you mean…" edit-distance iterates bytes with ASCII-only case fold via `c += 32`. Non-ASCII command names won't fuzzy-match. Combined with **autocorrect.c:587-588** byte-length cap of 32, non-ASCII names of moderate visual length get rejected before edit-distance even runs.
   - **DONE** (5c73422b + 3fb5fbfc): libfuzzy `decode_to_codepoints` now case-folds via `lle_unicode_tolower_codepoint` (Latin-1, Latin Extended-A/B, IPA, Greek, Cyrillic, Cyrillic Supplement). `fast_edit_distance` rewritten as a delegation to `fuzzy_damerau_levenshtein_distance` with a 32-*codepoint* cap via `fuzzy_string_length` (no more byte cap), preserving the `max_distance` early-termination. Five Unicode cases added to `tests/unit/test_autocorrect.c` covering Latin-1, Latin Extended-A, Greek, and mixed-script distance bounds.

6. **`src/pattern_match.c:match()`** — the `[[ str == pat ]]` extended-test matcher (separate from executor.c's case-pattern matcher) walks bytes for every operator. Concrete sub-verdicts: (a) `?` matches one byte rather than one codepoint (`c?é` fails to match `café`); (b) `[...]` bracket class calls `match_char_class` which uses byte ranges + single-byte `s++` -- same shape as the executor.c verdict #3 fix; (c) literal `*p != *s` compares first byte of multi-byte codepoints. The `*` branch iterates `s++` byte-by-byte; functionally tolerable (eventually lands on a codepoint boundary) but redundant work on each mid-codepoint position. **Audit-enumeration gap:** the file-level entry at line 140 forward-referenced these but the cross-cutting list did not include them; the work is real, the discovery is mine.
   - **DONE** (a0e525eb): added `decode_one` helper and rewrote `match_char_class` with an `s_consumed` out-param + codepoint comparison; `match()` `?` / `[` / literal branches now decode and advance by codepoint, so `c?é` matches `café`, `[α-ω]` ranges work as Greek codepoint ranges, and multi-byte literal pattern chars compare against full input codepoints. Coverage in `tests/unit/test_pattern_match.c`: question_matches_unicode_codepoint, star_matches_with_unicode_tail, bracket_range_unicode_lowercase, bracket_literal_unicode_chars, bracket_mixed_ascii_and_unicode, bracket_negation_unicode.

## High-priority B verdicts (NFC equivalence)

All four resolved in 2026-05-27 via the `lle_unicode_strings_equal`
primitive (existing) plus a new `lle_unicode_normalize_nfc_alloc`
heap wrapper (3e7c3acd) for the assoc-key path that needed
canonical-form storage rather than just canonical-form comparison.

- **`src/builtins/bin_test.c:189,192`** — `[ a = b ]` / `[[ a == b ]]` compare user values bytewise. NFC `é` vs NFD `é` currently unequal. Pure POSIX defends this; lush-superset shouldn't.
  - **DONE** (83cc8e04): both branches use `lle_unicode_strings_equal(s1, s2, NULL)`. The executor.c:8953,8958 duplicate (a MAYBE_UNUSED dead-code copy of execute_test_builtin) was deleted in the same commit. Tests in `tests/unit/test_executor.c`: `rt_test_equality_under_nfc_equivalence`, `rt_test_inequality_under_nfc_when_actually_different`, `rt_test_ascii_paths_unchanged`.
- **`src/symtable.c:2692`** — associative-array key lookup is bytewise. `arr[café]=1; echo "${arr[café]}"` with NFD-typed key creates two distinct entries displayed identically.
  - **DONE** (423f4f36): introduced `assoc_key_nfc` static helper over the new `lle_unicode_normalize_nfc_alloc` primitive (3e7c3acd); `symtable_array_set_assoc`, `symtable_array_get_assoc`, and `symtable_array_unset_assoc` all NFC-normalise the key at the boundary so the hashtable stores canonical bytes and NFC/NFD pairs collapse. Tests: `rt_assoc_key_nfc_nfd_collapse`, `rt_assoc_key_distinct_when_actually_different`, `rt_assoc_key_ascii_paths_unchanged`.
- **`src/executor.c:5160`** and **`src/redirection.c:1023`**, **`src/parser.c:2871`** — heredoc end-delimiter match is bytewise across user-chosen delimiter; rare in practice but a latent NFC bug.
  - **DONE** (2818f1ae): all three layers (parse-time scan, runtime body-read, argv-time delimiter check) use `lle_unicode_strings_equal`. Tests: `rt_heredoc_delimiter_nfc_vs_nfd`, `rt_heredoc_delimiter_nfd_vs_nfc`, `rt_heredoc_delimiter_unequal_when_actually_different`.
- **`src/autocorrect.c:237, 403`** — autocorrect dedup of suggestion candidates is bytewise across filesystem-sourced names.
  - **DONE** (178ff0d9): both dedup loops (`autocorrect_aggregate_suggestions` and `autocorrect_learn_command`) use `lle_unicode_strings_equal`. Tests in `tests/unit/test_autocorrect.c`: `learn_command_nfc_nfd_dedup`, `learn_command_distinct_under_case`.

## Identifier-policy B cluster (cross-cutting)

Many sites use `isalpha`/`isalnum` to test "is this a valid var/function/alias name character?":

- src/executor.c: 4509, 4512, 4625, 4730, 5696, 5717-5719, 5909, 6092, 9085, 9091, 11496, 12072-12077, 13053-13057, 14752
- src/parser.c: 4881, 4887
- src/tokenizer.c: 1350, 1362, 1392, 1408, 1948, 1952, 2049, 2324, 2333, 2364, 2647, 2658
- src/arithmetic.c: 856, 983, 1545
- src/builtins/builtins.c: 323, 329
- src/builtins/alias.c: 141

These are **not bugs** under POSIX or bash strict-mode (identifier names are `[A-Za-z_][A-Za-z0-9_]*`). They become bugs if lush-superset decides to accept Unicode identifiers. Track as one policy question, not as 25 separate fixes.

## Unicode-whitespace B cluster (cross-cutting)

`isspace((unsigned char)c)` for word-boundary detection misses Unicode whitespace (U+00A0 NO-BREAK SPACE, U+2028 LINE SEPARATOR, U+2029 PARAGRAPH SEPARATOR, U+3000 IDEOGRAPHIC SPACE, etc.):

- src/executor.c: 4484, 4488, 10534
- src/builtins/alias.c: 168, 199, 320, 342, 668
- src/lle/keybinding/keybinding_actions.c: 116, 2939, 2945
- src/arithmetic.c: 1387, 1492, 1499

If a user pastes text from a webpage containing NBSP, Ctrl-W / IFS word-splitting won't split at it. Track as one policy question.

---

## Per-directory rollup

| Directory | str calls (approx) | ctype calls (approx) | A% | B% | C% | Notes |
|---|---|---|---|---|---|---|
| src/ (top-level *.c) | 1100 | 90 | 85 | 12 | 3 | C concentrated in executor.c pattern/case code |
| src/builtins/ | 442 | 32 | 92 | 7 | 1 | autocorrect.c is the C-class outlier |
| src/builtins/display/ | (included in builtins) | | mostly A | | | dispatch on fixed `display lle X` subcommand names |
| src/display/ | 111 | 11 | ~95 | ~5 | 0 | screen buffer / display controller; no Unicode-incorrect calls found in sampling |
| src/debug/ | 106 | 3 | ~95 | ~5 | 0 | mostly fixed debug-cmd names |
| src/lle/prompt/ | 338 | 1 | ~98 | ~2 | 0 | template-vocabulary matching |
| src/lle/completion/ | 129 | 5 | ~85 | ~15 | 0 | already partially routes through `lle_unicode_is_prefix`; remainder is candidate fixed-vocabulary |
| src/lle/history/ | 41 | 3 | ~80 | ~10 | ~10 | C-class: history_search.c case-insensitive search |
| src/lle/keybinding/ | 43 | 9 | ~85 | ~15 | 0 | identifier + whitespace B-cluster |
| src/lle/multiline/ | 7 | 12 | ~90 | ~10 | 0 | parser-like usage |
| src/lle/adaptive/ | 44 | 0 | ~95 | ~5 | 0 | terminal-cap names are fixed |
| src/lle/terminal/ | 15 | 0 | 100 | 0 | 0 | terminfo/termcap names are fixed ASCII |
| src/lle/core/ | 16 | 0 | 100 | 0 | 0 | error-domain / metric names are fixed |
| src/lle/event/ | 6 | 0 | 100 | 0 | 0 | event-type names are fixed |
| src/lle/input/ | 2 | 0 | 100 | 0 | 0 | escape-sequence parsing on bytes is correct |
| src/lle/widget/ | 1 | 0 | 100 | 0 | 0 | |
| src/lle/display/ | 18 | 14 | ~90 | ~10 | 0 | mirrors tokenizer identifier B-cluster |
| src/lle/buffer/ | 0 | 0 | n/a | n/a | n/a | all calls go through Unicode helpers already — exemplary |

**Total catalogued**: ≈2500 byte-oriented call sites across ≈140 files.

**Distribution estimate**: A ≈ 88%, B ≈ 11%, C ≈ 1%.

Absolute C counts: ~30-40 individual call sites, but they cluster into ~6 *features* (pattern char-class, pattern byte range, `${var^^}`/`${var,,}`, autocorrect edit distance, history case-insensitive search, keybinding-actions kill-ring dedup). Fixing the feature, not the call site, is the engineering unit.

## Suggested fix ordering

1. **`${var^^}` / `${var,,}` / `${var^}` / `${var,}` family** — wire `executor.c:10416-10417` to `lle_utf8_toupper` / `lle_utf8_tolower`. Helpers already exist; this is a 30-line patch.

2. **Pattern char-class `[[:alpha:]]` family** — `executor.c:10131-10165`. Needs new helpers (`lle_unicode_is_alpha`, `_is_digit`, `_is_upper`, `_is_lower`, `_is_punct`, `_is_space`, etc.) in `unicode_case.c` or a new `unicode_class.c`. Then the dispatch becomes a single call per branch.

3. **Pattern byte range `[a-z]`** — `executor.c:10173`. Walk by codepoint via `lle_utf8_decode_codepoint`.

4. **Interactive history search** — `history_search.c:212-236`. Replace `stristr` and `str_starts_with_i` with codepoint-iterating equivalents that use `lle_utf8_casefold` on both inputs.

5. **Autocorrect edit distance** — `autocorrect.c:586-680`. Replace byte iteration with codepoint iteration; replace `c += 32` case fold with `lle_unicode_tolower_codepoint`. Replace 32-byte cap with 32-codepoint cap.

6. **NFC-equivalence of `[ a = b ]`, assoc-array keys, heredoc delimiters, autocorrect dedup, kill-ring dedup, exact-history-match** — switch from `strcmp(==0)` to `lle_unicode_strings_equal`. These are individually small; could be batched per-subsystem.

7. **Identifier-policy decision** (one design call, then 25-30 call-site updates) — does lush accept non-ASCII identifiers? Currently silently rejects them. If yes, replace `isalpha`/`isalnum` identifier-scans with `lle_unicode_is_alpha` / `_is_alnum`.

8. **Unicode-whitespace decision** — does word splitting / Ctrl-W / `read` strip Unicode whitespace? Currently no. If yes, batch fix the `isspace((unsigned char)..)` sites.

## Completeness note

This worklist enumerates representative call sites for the highest-traffic files (executor.c, parser.c, tokenizer.c, symtable.c, redirection.c, pattern_match.c, expand.c, autocorrect.c, arithmetic.c, config.c, shell_mode.c, signals.c, the major builtins, completion sources, history search, keybinding actions, syntax highlighting) and rolls up the rest by directory. The **C verdicts** are believed complete — the audit walked every grep hit for case-conversion / case-folding / char-class / range patterns. The **B verdicts** are representative; the identifier-policy cluster and Unicode-whitespace cluster are likely to surface a few more sites in less-trafficked builtins on a second pass (e.g., `bin_read.c`'s `-d` delimiter handling, `bin_printf.c`'s `%c` format handling). The **A verdicts** are not exhaustively enumerated for the smaller files because the pattern is so uniform (option-flag literal dispatch).

A second pass — line-by-line through bin_*.c, src/debug/*.c, src/display/*.c, src/lle/prompt/*.c — would catch the remaining ~100-200 individual B-class sites for the identifier-policy + Unicode-whitespace clusters, but would not change the C-verdict count or the fix-ordering priorities above.
