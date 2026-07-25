/**
 * @file symtable.h
 * @brief POSIX-compliant symbol table with variable scoping
 *
 * Provides a unified variable scoping system handling global, local, loop,
 * subshell, and environment variables. Implements POSIX scoping rules for
 * proper variable shadowing and scope isolation.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#ifndef SYMTABLE_H
#define SYMTABLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

/// Forward declaration for libhashtable
typedef struct ht_strstr ht_strstr_t;

/// Forward declarations
typedef struct symtable_scope_enhanced symtable_scope_t;
typedef struct symvar symvar_t;

/// Variable types
typedef enum {
    SYMVAR_STRING,   ///< Regular string variable
    SYMVAR_INTEGER,  ///< Integer variable (for arithmetic)
    SYMVAR_ARRAY,    ///< Array variable (bash extension)
    SYMVAR_FUNCTION, ///< Function definition
    SYMVAR_NAMEREF   ///< Nameref variable (reference to another variable)
} symvar_type_t;

/* ============================================================================
 * ARRAY VALUE STORAGE (Extended Language Support)
 * ============================================================================
 */

/// Variable flags
///
/// Bit values are kept stable across removals so callers that store
/// the underlying mask see no shift if a value is later retired
/// again. Bits (1 << 2), (1 << 3), and (1 << 8) are intentionally
/// vacant -- the prior SYMVAR_LOCAL / SYMVAR_SPECIAL_VAR / SYMVAR_TRACE
/// values were dead (set but never read; locality is tracked by the
/// scope chain, special vars are dispatched by name, declare -t is a
/// no-op on variables in bash itself) and were removed rather than
/// kept as theatrical reservations. See the audit at 44e6970f.
typedef enum {
    SYMVAR_NONE = 0,
    SYMVAR_EXPORTED = (1 << 0),     ///< Variable is exported to environment
    SYMVAR_READONLY = (1 << 1),     ///< Variable is read-only
    SYMVAR_UNSET = (1 << 4),        ///< Variable is explicitly unset
    SYMVAR_NAMEREF_FLAG = (1 << 5), ///< Variable is a nameref (local -n)
    SYMVAR_LOWERCASE = (1 << 6),    ///< Convert value to lowercase (declare -l)
    SYMVAR_UPPERCASE = (1 << 7),    ///< Convert value to uppercase (declare -u)
    /**
     * Integer (declare -i): RHS of assignment is arith-evaluated
     */
    SYMVAR_INTEGER_ATTR = (1 << 9)
} symvar_flags_t;

/**
 * @brief Array value storage structure
 *
 * Supports both indexed arrays (Bash-style) and associative arrays.
 * Indexed arrays use sparse storage - only set indices consume memory.
 * Associative arrays use a hash table for key-value storage.
 */
typedef struct array_value {
    char **elements;     ///< Sparse array of element values (indexed)
    int64_t *indices;    ///< Parallel array of actual indices (for sparse).
                         ///< 64-bit so a subscript up to INT64_MAX is a native
                         ///< sparse key -- never truncated/aliased (issue #618)
    size_t count;        ///< Number of elements currently stored
    size_t capacity;     ///< Allocated capacity for elements/indices
    int64_t max_index;   ///< Highest index used (for ${#arr[@]}); 64-bit to
                         ///< match `indices` and the arithmetic engine
    bool is_associative; ///< True if associative array (declare -A)
    /**
     * Variable attributes carried by the array itself. Mirrors the
     * symvar_flags_t set used for scalars but lives on the array
     * record so element-level writes (arr[idx]=value) can consult
     * SYMVAR_READONLY without going through the scalar symbol table.
     * Set by declare -ar / -Ar and by readonly NAME on an existing
     * array; read by symtable_set_array_element.
     */
    symvar_flags_t flags;
    /**
     * Associative arrays use an insertion-ordered ht_strstr table, so the
     * map itself enumerates keys in first-set order (zsh-style, SEMANTICS.md
     * 4.2). A key keeps its original position when overwritten, and the
     * table owns the canonical key copies, so no parallel order list is
     * needed. (Issue #69.)
     */
    ht_strstr_t *assoc_map; ///< Insertion-ordered hash table for assoc arrays
} array_value_t;

/* ============================================================================
 * UNIFIED VALUE VIEW (SEMANTICS.md section 3 first-class values)
 * ============================================================================
 *
 * Single lookup primitive returning a kind-tagged view of whatever the
 * symtable holds for a name. Bridges the historical split between
 * symtable_get_var (returns owned char*) and symtable_get_array
 * (returns borrowed array_value_t*), so call sites that need to
 * branch on the kind of a value can do so through one query rather
 * than the two-step "try array, fall back to scalar" dance that
 * proliferates through the expansion engine.
 *
 * Ownership:
 *   - scalar_value is OWNED by the view (caller frees, or calls
 *     lush_value_view_clear); strdup'd from the symtable's storage
 *     so symtable mutations between lookup and use are safe.
 *   - array is BORROWED from the symtable's array side-table;
 *     valid until the next unset / mutation of that array.
 *
 * Call sites migrate at their own pace; symtable_get_var /
 * symtable_get_array continue to work and serve sites where the
 * unified view would not be a net simplification (single-kind
 * lookups, hot paths that pre-classified the kind elsewhere).
 */

typedef enum {
    LUSH_VALUE_NONE = 0, ///< No binding under this name
    LUSH_VALUE_SCALAR,   ///< Scalar string value
    LUSH_VALUE_LIST,     ///< Indexed array (list per SEMANTICS section 3.1)
    LUSH_VALUE_MAP       ///< Associative array (map per SEMANTICS section 3.1)
} lush_value_kind_t;

typedef struct lush_value_view {
    lush_value_kind_t kind;
    char *scalar_value;   ///< Owned strdup; non-NULL iff kind==SCALAR
    array_value_t *array; ///< Borrowed; non-NULL iff kind==LIST or MAP
} lush_value_view_t;

/**
 * @brief Return code: write blocked by readonly attribute
 *
 * symtable_set_var / symtable_assign_var / symtable_set_array_element
 * return this value (instead of -1) when the write target already
 * carries SYMVAR_READONLY anywhere in the scope chain. Callers that
 * want to surface a user-facing "readonly variable" diagnostic
 * distinguish this from a generic failure; callers that just need
 * success/failure can continue treating non-zero as failure.
 */
#define SYMTABLE_ERR_READONLY (-2)

/**
 * @brief Classification of an array-element/append write whose target is not
 * currently an array (the create branch). Produced by
 * symtable_scalar_promotion, consumed by the three promotion sites.
 */
typedef enum {
    SCALAR_PROMO_NONE, ///< Unbound name: create a fresh array (no kind change)
    SCALAR_PROMO_PRESERVE, ///< Existing scalar, relaxed mode: promote, keep the
                           ///< scalar as the base element (non-lossy)
    SCALAR_PROMO_REFUSE,   ///< Existing scalar, strict mode: refuse the re-kind
} scalar_promo_t;

/**
 * @brief Classify a scalar->array kind transition for @p name (issue #621).
 *
 * Call only when the name is not currently bound as an array (the create
 * branch). Returns SCALAR_PROMO_NONE for an unbound name (a fresh array is not
 * a kind change); for an existing scalar, SCALAR_PROMO_REFUSE under strict
 * value typing (lush mode) or SCALAR_PROMO_PRESERVE under a relaxed mode.
 */
scalar_promo_t symtable_scalar_promotion(const char *name);

/**
 * @brief Seed a freshly created empty indexed array with @p name's current
 * scalar value at the base index, so promotion is non-lossy (issue #621).
 *
 * Must be called while @p name is still bound as a scalar (before the store
 * that overwrites it). No-op unless @p array is a fresh, empty, indexed array
 * and @p name currently reads as a scalar.
 */
void symtable_seed_promoted_scalar(const char *name, array_value_t *array);

/**
 * @brief Apply SYMVAR_LOWERCASE / SYMVAR_UPPERCASE to a value
 *
 * If `flags` carries SYMVAR_LOWERCASE, returns a malloc'd lowercased
 * copy of `value`; if it carries SYMVAR_UPPERCASE, returns an
 * uppercased copy. Conversion goes through lle_utf8_tolower /
 * lle_utf8_toupper so non-ASCII codepoints fold according to the
 * project's Unicode case table.
 *
 * Returns NULL when no transformation applies (neither bit set, or
 * `value` is NULL / empty), letting callers cheaply skip the
 * allocation when the attribute is absent. Returns NULL on allocation
 * failure or invalid UTF-8 in `value` -- callers should fall back to
 * the untransformed value in those cases rather than failing the
 * surrounding write.
 *
 * @param value Source string (UTF-8). May be NULL.
 * @param flags Variable flags carrying the case attribute.
 * @return Heap-allocated transformed copy (caller frees), or NULL.
 */
char *symtable_apply_case_attr_alloc(const char *value, symvar_flags_t flags);

/// Scope types for different contexts
typedef enum {
    SCOPE_GLOBAL,      ///< Global shell scope
    SCOPE_FUNCTION,    ///< POSIX-form function scope (dynamic-scoped)
    SCOPE_LOOP,        ///< Loop iteration scope (for/while)
    SCOPE_SUBSHELL,    ///< Subshell scope
    SCOPE_CONDITIONAL, ///< Conditional execution scope (if/case)
    /// Typed-function (`fn`) frame: free names in the body resolve
    /// through the captured declaration-time scope, not the dynamic
    /// caller's scope chain.
    SCOPE_LEXICAL
} scope_type_t;

/**
 * @brief Variable entry structure
 *
 * Deserialized form of one scope-table entry. Storage is kind-tagged
 * via the type field:
 *
 *   - SYMVAR_STRING / SYMVAR_INTEGER / SYMVAR_NAMEREF / SYMVAR_FUNCTION:
 *       value holds the scalar string; array is NULL.
 *   - SYMVAR_ARRAY:
 *       value holds the array_value_t pointer encoded as a hex string
 *       (so the existing ht_strstr storage can carry it verbatim);
 *       array is the parsed pointer for fast direct access.
 *
 * Callers read array directly when type==SYMVAR_ARRAY rather than
 * re-parsing value, but the two fields are always consistent --
 * serialize_variable encodes both from a single source.
 */
struct symvar {
    char *name;           ///< Variable name
    char *value;          ///< Scalar string (or hex pointer for arrays)
    symvar_type_t type;   ///< Variable type (selects which storage is live)
    symvar_flags_t flags; ///< Variable flags
    size_t scope_level;   ///< Scope level where defined
    array_value_t *array; ///< List/Map storage; non-NULL iff type==ARRAY
    symvar_t *next;       ///< Next variable in hash chain
};

/// Enhanced symbol table scope structure using libhashtable
struct symtable_scope_enhanced {
    scope_type_t scope_type; ///< Type of scope
    size_t level;            ///< Scope nesting level
    ht_strstr_t *vars_ht;    ///< libhashtable ht_strstr_t for variables
    symtable_scope_t
        *parent; ///< Parent scope (for variable lookup walk); for SCOPE_LEXICAL
                 ///< this is the captured declaration-site parent, not the
                 ///< dynamic caller.
    /**
     * SCOPE_LEXICAL only: the scope that was current at push time
     * (i.e. the dynamic caller). The scope stack is LIFO regardless
     * of scoping discipline, so pop must return here, not to `parent`.
     * NULL for non-lexical frames.
     */
    symtable_scope_t *dynamic_caller;
    char *scope_name; ///< Name of scope (for debugging)
};

/// Symbol table manager (forward declaration for implementation)
typedef struct symtable_manager symtable_manager_t;

/// Legacy compatibility structures (for string management system)
typedef enum {
    SYM_STR,  ///< String symbol
    SYM_FUNC, ///< Function symbol
} symbol_type_t;

typedef struct symtable_entry {
    char *name;                  ///< Symbol name
    symbol_type_t val_type;      ///< Type of stored value
    char *val;                   ///< String value (for SYM_STR)
    unsigned int flags;          ///< Legacy flag bits (FLAG_*)
    struct symtable_entry *next; ///< Next entry in the chain
    struct node *func_body;      ///< Function body AST (for SYM_FUNC)
} symtable_entry_t;

typedef struct {
    size_t level;           ///< Scope nesting level
    symtable_entry_t *head, ///< First entry in the chain
        *tail;              ///< Last entry in the chain
} symtable_t;

#define MAX_SYMTAB 256
typedef struct {
    size_t symtable_count;                 ///< Number of active symbol tables
    symtable_t *symtable_list[MAX_SYMTAB]; ///< Stack of symbol tables
    symtable_t *global_symtable,           ///< Global (outermost) symbol table
        *local_symtable;                   ///< Current (innermost) symbol table
} symtable_stack_t;

/* ============================================================================
 * CORE MODERN API
 * ============================================================================
 */

/// Manager Lifecycle

/**
 * @brief Create a new symbol table manager
 *
 * @return New manager instance or NULL on failure
 */
symtable_manager_t *symtable_manager_new(void);

/**
 * @brief Free a symbol table manager and all scopes
 *
 * @param manager Manager to free
 */
void symtable_manager_free(symtable_manager_t *manager);

/**
 * @brief Enable or disable debug output
 *
 * @param manager Manager instance
 * @param debug True to enable debug output
 */
void symtable_manager_set_debug(symtable_manager_t *manager, bool debug);

/// Scope Management

/**
 * @brief Push a new scope onto the scope stack
 *
 * @param manager Manager instance
 * @param type Type of scope to push
 * @param name Name for the scope (for debugging)
 * @return 0 on success, -1 on error
 */
int symtable_push_scope(symtable_manager_t *manager, scope_type_t type,
                        const char *name);

/**
 * @brief Capture the current scope as an opaque parent for a future
 *        SCOPE_LEXICAL push.
 *
 * Used at typed-function (`fn`) declaration time to record the lexical
 * environment the function should resolve free names against. The
 * returned pointer is borrowed and remains valid until the captured
 * scope is popped by the same manager; the caller must not free it.
 * In the common case of top-level `fn` declarations, the captured
 * scope is the global scope, which lives for the manager's entire
 * lifetime.
 *
 * @param manager Manager instance
 * @return Opaque pointer to the current scope, or NULL on error
 */
void *symtable_capture_scope_for_lexical(symtable_manager_t *manager);

/**
 * @brief Push a SCOPE_LEXICAL frame whose parent is the supplied
 *        captured scope rather than the dynamic caller.
 *
 * The new frame inherits all lookup semantics of a normal scope
 * frame -- variable resolution walks the parent chain -- but the
 * parent is the capture site, not the live `current_scope` at push
 * time. The result is lexical (closure) scoping: free names in the
 * frame's body resolve through the declaration environment of the
 * enclosing function, not through the dynamic call chain. Used to
 * give a typed-function body its closure environment at call time.
 *
 * On pop, the frame restores the previous `current_scope` (the dynamic
 * caller) -- the captured-parent linkage exists only for the duration
 * of the lexical frame.
 *
 * @param manager Manager instance
 * @param name Scope name for diagnostics (typically the fn name)
 * @param captured_parent Opaque pointer returned by
 *                        symtable_capture_scope_for_lexical
 * @return 0 on success, -1 on error
 */
int symtable_push_lexical_scope(symtable_manager_t *manager, const char *name,
                                void *captured_parent);

/**
 * @brief Pop the current scope from the stack
 *
 * @param manager Manager instance
 * @return 0 on success, -1 on error
 */
int symtable_pop_scope(symtable_manager_t *manager);

/**
 * @brief Get current scope nesting level
 *
 * @param manager Manager instance
 * @return Current scope level (0 = global)
 */
size_t symtable_current_level(symtable_manager_t *manager);

/**
 * @brief Get name of current scope
 *
 * @param manager Manager instance
 * @return Scope name or NULL
 */
const char *symtable_current_scope_name(symtable_manager_t *manager);

/**
 * @brief Check if currently executing within a function scope
 *
 * Searches the scope stack for any SCOPE_FUNCTION scope.
 * Used by return builtin to validate context.
 *
 * @param manager Manager instance
 * @return true if in a function scope, false otherwise
 */
bool symtable_in_function_scope(symtable_manager_t *manager);

/**
 * @brief Get the type of the current (innermost) scope
 *
 * Returns SCOPE_GLOBAL when no scope is pushed, otherwise the
 * scope_type of the innermost pushed scope (SCOPE_FUNCTION /
 * SCOPE_LOOP / SCOPE_SUBSHELL / SCOPE_CONDITIONAL). Used by the
 * debugger to label and gate its local-variable view.
 *
 * @param manager Manager instance
 * @return Current scope type (SCOPE_GLOBAL if manager is NULL or no
 *         scope is active).
 */
scope_type_t symtable_current_scope_type(symtable_manager_t *manager);

/// Variable Operations

/**
 * @brief Set a variable with flags
 *
 * @param manager Manager instance
 * @param name Variable name
 * @param value Variable value
 * @param flags Variable flags (exported, readonly, etc.)
 * @return 0 on success, -1 on error
 */
int symtable_set_var(symtable_manager_t *manager, const char *name,
                     const char *value, symvar_flags_t flags);

/**
 * @brief Set a local variable in current scope
 *
 * @param manager Manager instance
 * @param name Variable name
 * @param value Variable value
 * @return 0 on success, -1 on error
 */
int symtable_set_local_var(symtable_manager_t *manager, const char *name,
                           const char *value);

/**
 * @brief Set a global variable
 *
 * @param manager Manager instance
 * @param name Variable name
 * @param value Variable value
 * @return 0 on success, -1 on error
 */
int symtable_set_global_var(symtable_manager_t *manager, const char *name,
                            const char *value);

/**
 * @brief Set/unset a positional parameter in its home scope
 *
 * The positional parameters ($1..$N, $#) live in the nearest enclosing
 * function frame, or the global scope if not in a function. Loop and
 * conditional frames are transparent to them, so `set --` must write to
 * this home scope rather than the innermost current scope -- otherwise a
 * `set --` inside a loop body would land in the loop frame and be lost when
 * it pops (issue #562, and its loop-scope regression). Subshells fork, so
 * their positionals are isolated by the process boundary.
 *
 * @param manager Symbol table manager
 * @param name    Parameter name ("1".."N" or "#")
 * @param value   Value (set only)
 * @return 0 on success, non-zero on failure
 */
int symtable_set_positional_var(symtable_manager_t *manager, const char *name,
                                const char *value);
int symtable_unset_positional_var(symtable_manager_t *manager,
                                  const char *name);

/**
 * @brief Assign a value using POSIX scope-chain semantics
 *
 * The correct write path for unprefixed `name=value` assignments. Walks
 * the scope chain from current scope upward; if the variable already
 * exists in any reachable scope, updates that scope (preserving its
 * locality and existing flags). If not found anywhere, creates the
 * variable in the global scope (POSIX default for unprefixed
 * assignments).
 *
 * Use this instead of symtable_set_global_var when implementing the
 * runtime semantics of `name=value`, `name+=value`, `for var in ...`,
 * and similar implicit-assignment forms. symtable_set_global_var
 * unconditionally creates or updates a global, ignoring any local of
 * the same name — which leaves the local unchanged and produces silent
 * semantic shifts (issue #47).
 *
 * @param manager Manager instance
 * @param name Variable name
 * @param value Variable value
 * @return 0 on success, -1 on error
 */
int symtable_assign_var(symtable_manager_t *manager, const char *name,
                        const char *value);

/**
 * @brief Get a variable value with scope lookup
 *
 * @param manager Manager instance
 * @param name Variable name
 * @return Variable value or NULL if not found
 */
char *symtable_get_var(symtable_manager_t *manager, const char *name);

/**
 * @brief Get a scalar variable's value from the CURRENT scope only
 *
 * Unlike symtable_get_var, this does NOT walk to parent scopes: it returns a
 * value only when @p name is bound as a scalar in the current scope. Used
 * when declaring a new local must not inherit a same-named outer value.
 *
 * @param manager Symbol table manager
 * @param name    Variable name
 * @return Owned copy of the value, or NULL if unbound in the current scope
 *         (or bound as an array / marked unset). Caller frees.
 */
char *symtable_get_var_current_scope(symtable_manager_t *manager,
                                     const char *name);

/**
 * @brief Unset a variable
 *
 * @param manager Manager instance
 * @param name Variable name
 * @return 0 on success, -1 on error
 */
int symtable_unset_var(symtable_manager_t *manager, const char *name);

/* ============================================================================
 * NAMEREF SUPPORT (Function Enhancements)
 * ============================================================================
 */

/**
 * @brief Create a nameref variable
 *
 * Creates a variable that references another variable by name.
 * When the nameref is accessed, the referenced variable is used.
 *
 * @param manager Manager instance
 * @param name Nameref variable name
 * @param target Name of the variable to reference
 * @param flags Additional flags (SYMVAR_NAMEREF_FLAG, SYMVAR_EXPORTED, etc.)
 * @return 0 on success, -1 on error
 */
int symtable_set_nameref(symtable_manager_t *manager, const char *name,
                         const char *target, symvar_flags_t flags);

/**
 * @brief Resolve a nameref to its target variable name
 *
 * Follows the nameref chain to find the ultimate target variable.
 * Detects circular references and returns NULL in that case.
 *
 * @param manager Manager instance
 * @param name Variable name (may be a nameref)
 * @param max_depth Maximum chain depth to follow (prevents infinite loops)
 * @return A newly-allocated copy of the resolved variable name that the caller
 *         must free, or NULL on a circular reference or allocation failure. The
 *         return is always owned (even when it equals @p name).
 */
const char *symtable_resolve_nameref(symtable_manager_t *manager,
                                     const char *name, int max_depth);

/**
 * @brief Check if a variable is a nameref
 *
 * @param manager Manager instance
 * @param name Variable name
 * @return True if variable is a nameref
 */
bool symtable_is_nameref(symtable_manager_t *manager, const char *name);

/**
 * @brief Get variable flags
 *
 * @param manager Manager instance
 * @param name Variable name
 * @return Variable flags or SYMVAR_NONE if not found
 */
symvar_flags_t symtable_get_flags(symtable_manager_t *manager,
                                  const char *name);

/**
 * @brief Set variable flags
 *
 * @param manager Manager instance
 * @param name Variable name
 * @param flags Flags to set
 * @return 0 on success, -1 on error
 */
int symtable_set_flags(symtable_manager_t *manager, const char *name,
                       symvar_flags_t flags);

/**
 * @brief Check if a variable exists
 *
 * @param manager Manager instance
 * @param name Variable name
 * @return True if variable exists
 */
bool symtable_var_exists(symtable_manager_t *manager, const char *name);

/// Export/Environment Operations

/**
 * @brief Mark a variable as exported
 *
 * @param manager Manager instance
 * @param name Variable name
 * @return 0 on success, -1 on error
 */
int symtable_export_var(symtable_manager_t *manager, const char *name);

/**
 * @brief Get environment array for exec
 *
 * @param manager Manager instance
 * @return NULL-terminated array of "name=value" strings (caller must free)
 */
char **symtable_get_environ(symtable_manager_t *manager);

/**
 * @brief Free an environment array
 *
 * @param environ Array to free
 */
void symtable_free_environ(char **environ);

/// Debugging

/**
 * @brief Dump variables in a specific scope
 *
 * @param manager Manager instance
 * @param scope Scope type to dump
 */
void symtable_dump_scope(symtable_manager_t *manager, scope_type_t scope);

/**
 * @brief Dump all scopes for debugging
 *
 * @param manager Manager instance
 */
void symtable_dump_all_scopes(symtable_manager_t *manager);

/* ============================================================================
 * CONVENIENCE API (High-level functions for common operations)
 * ============================================================================
 */

/**
 * @brief Get the global symbol table manager
 *
 * @return Global manager instance
 */
symtable_manager_t *symtable_get_global_manager(void);

/// Basic Variable Operations (Global Scope)

/**
 * @brief Get a global variable value
 *
 * @param name Variable name
 * @return Variable value or NULL
 */
char *symtable_get_global(const char *name);

/**
 * @brief Get a global variable with default
 *
 * @param name Variable name
 * @param default_value Value to return if not found
 * @return Variable value or default_value
 */
char *symtable_get_global_default(const char *name, const char *default_value);

/**
 * @brief Set a global variable
 *
 * @param name Variable name
 * @param value Variable value
 * @return 0 on success, -1 on error
 */
int symtable_set_global(const char *name, const char *value);

/**
 * @brief Check if a global variable exists
 *
 * @param name Variable name
 * @return True if exists
 */
bool symtable_exists_global(const char *name);

/**
 * @brief Unset a global variable
 *
 * @param name Variable name
 * @return 0 on success, -1 on error
 */
int symtable_unset_global(const char *name);

/// Integer Variable Operations

/**
 * @brief Get a global variable as integer
 *
 * @param name Variable name
 * @param default_value Default if not found or not numeric
 * @return Integer value
 */
int symtable_get_global_int(const char *name, int default_value);

/**
 * @brief Set a global variable to integer value
 *
 * @param name Variable name
 * @param value Integer value
 * @return 0 on success, -1 on error
 */
int symtable_set_global_int(const char *name, int value);

/// Boolean Variable Operations

/**
 * @brief Get a global variable as boolean
 *
 * @param name Variable name
 * @param default_value Default if not found
 * @return Boolean value
 */
bool symtable_get_global_bool(const char *name, bool default_value);

/**
 * @brief Set a global variable to boolean value
 *
 * @param name Variable name
 * @param value Boolean value
 * @return 0 on success, -1 on error
 */
int symtable_set_global_bool(const char *name, bool value);

/// Export Operations

/**
 * @brief Export a global variable to environment
 *
 * @param name Variable name
 * @return 0 on success, -1 on error
 */
int symtable_export_global(const char *name);

/**
 * @brief Remove export flag from a variable
 *
 * @param name Variable name
 * @return 0 on success, -1 on error
 */
int symtable_unexport_global(const char *name);

/// Special Variable Operations

/**
 * @brief Set a special system variable
 *
 * @param name Variable name
 * @param value Variable value
 * @return 0 on success, -1 on error
 */
int symtable_set_special_global(const char *name, const char *value);

/**
 * @brief Get a special system variable
 *
 * @param name Variable name
 * @return Variable value or NULL
 */
char *symtable_get_special_global(const char *name);

/**
 * @brief Set a read-only global variable
 *
 * @param name Variable name
 * @param value Variable value
 * @return 0 on success, -1 on error
 */
int symtable_set_readonly_global(const char *name, const char *value);

/// Debugging

/** @brief Dump global scope variables */
void symtable_debug_dump_global_scope(void);

/** @brief Dump all scopes */
void symtable_debug_dump_all_scopes(void);

/**
 * @brief Enumerate global variables with callback (debug - raw values)
 *
 * Note: This function returns raw encoded values with internal metadata.
 * For clean values, use symtable_enumerate_global_vars() instead.
 *
 * @param callback Function called for each variable
 * @param userdata User data passed to callback
 */
void symtable_debug_enumerate_global_vars(void (*callback)(const char *key,
                                                           const char *value,
                                                           void *userdata),
                                          void *userdata);

/**
 * @brief Enumerate global variables with callback (clean values)
 *
 * Enumerates all global shell variables and calls the callback for each one.
 * Unlike symtable_debug_enumerate_global_vars, this function returns the
 * actual variable values without internal metadata encoding.
 *
 * @param callback Function called for each variable (key, value, userdata)
 * @param userdata User data passed to callback
 */
void symtable_enumerate_global_vars(void (*callback)(const char *key,
                                                     const char *value,
                                                     void *userdata),
                                    void *userdata);

/**
 * @brief Enumerate variables defined directly in the current scope
 *
 * Iterates entries in @p manager->current_scope->vars_ht only -- it does
 * NOT walk the scope chain, so this returns just the variables declared
 * in the innermost active scope (e.g. a function's locals). Array
 * variables live in separate global storage and are reached through
 * symtable_enumerate_arrays(); only scalar / integer / nameref entries
 * appear here. Used by the debugger's local-variable inspection.
 *
 * @param manager Manager instance
 * @param callback Called for each variable. @p type carries the symvar
 *                 type so the consumer can label Scalar / Nameref / etc.
 * @param userdata Opaque pointer passed through to the callback
 */
void symtable_enumerate_current_scope_vars(symtable_manager_t *manager,
                                           void (*callback)(const char *name,
                                                            const char *value,
                                                            symvar_type_t type,
                                                            void *userdata),
                                           void *userdata);

/**
 * @brief Count global variables
 *
 * @return Number of global variables
 */
size_t symtable_count_global_vars(void);

/// Environment Array

/**
 * @brief Get environment as array for exec
 *
 * @return NULL-terminated array (caller must free)
 */
char **symtable_get_environment_array(void);

/**
 * @brief Free an environment array
 *
 * @param env Array to free
 */
void symtable_free_environment_array(char **env);

/* ============================================================================
 * CONVENIENCE MACROS
 * ============================================================================
 */

#define symtable_set(mgr, name, value)                                         \
    symtable_set_var(mgr, name, value, SYMVAR_NONE)
#define symtable_get(mgr, name) symtable_get_var(mgr, name)
#define symtable_export(mgr, name) symtable_export_var(mgr, name)

/// Backward compatibility macros
#define get_global_var(name) symtable_get_global(name)
#define set_global_var(name, value) symtable_set_global(name, value)
#define get_global_var_default(name, def) symtable_get_global_default(name, def)
#define export_global_var(name) symtable_export_global(name)

/// Advanced operations (direct modern API access)
#define symtable_manager() symtable_get_global_manager()
#define symtable_push_function_scope(name)                                     \
    symtable_push_scope(symtable_manager(), SCOPE_FUNCTION, name)
#define symtable_push_loop_scope(name)                                         \
    symtable_push_scope(symtable_manager(), SCOPE_LOOP, name)
#define symtable_push_subshell_scope(name)                                     \
    symtable_push_scope(symtable_manager(), SCOPE_SUBSHELL, name)
#define symtable_pop_current_scope() symtable_pop_scope(symtable_manager())

/* ============================================================================
 * SYSTEM INTERFACE (Essential functions for shell operation)
 * ============================================================================
 */

/** @brief Initialize the global symbol table */
void init_symtable(void);

/** @brief Free the global symbol table */
void free_global_symtable(void);

/**
 * @brief Set the exit status special variable ($?)
 *
 * @param status Exit status value
 */
void set_exit_status(int status);

/**
 * @brief Get environment array for child processes
 *
 * @return NULL-terminated array (caller must free)
 */
char **get_environ_array(void);

/**
 * @brief Free an environment array
 *
 * @param env Array to free
 */
void free_environ_array(char **env);

/* ============================================================================
 * LEGACY COMPATIBILITY (For string management and other subsystems)
 * ============================================================================
 */

/// Legacy flag definitions
#define FLAG_EXPORT (1 << 0)
#define FLAG_READONLY (1 << 1)
#define FLAG_CMD_EXPORT (1 << 2)
#define FLAG_LOCAL (1 << 3)
#define FLAG_ALLCAPS (1 << 4)
#define FLAG_ALLSMALL (1 << 5)
#define FLAG_FUNCTRACE (1 << 6)
#define FLAG_INTVAL (1 << 7)
#define FLAG_SPECIAL_VAR (1 << 8)
#define FLAG_TEMP_VAR (1 << 9)

/// Legacy functions (for string management system)

/**
 * @brief Allocate a new legacy symbol table at the given scope level
 *
 * @param level Scope nesting level for the new table
 * @return New symbol table on success, NULL on failure
 */
symtable_t *new_symtable(size_t level);

/**
 * @brief Push a fresh symbol table onto the legacy scope stack
 *
 * @return New top-of-stack symbol table on success, NULL on failure
 */
symtable_t *symtable_stack_push(void);

/**
 * @brief Pop the top symbol table off the legacy scope stack
 *
 * @return Popped symbol table on success, NULL on failure
 */
symtable_t *symtable_stack_pop(void);

/**
 * @brief Remove an entry from a legacy symbol table
 *
 * @param symtable Symbol table holding the entry
 * @param entry Entry to remove
 * @return 0 on success, -1 on error
 */
int remove_from_symtable(symtable_t *symtable, symtable_entry_t *entry);

/**
 * @brief Create and insert a new entry into the current legacy symbol table
 *
 * @param name Symbol name (copied/owned per legacy semantics)
 * @return New symbol entry on success, NULL on failure
 */
symtable_entry_t *add_to_symtable(char *name);

/**
 * @brief Look up a symbol by name in a specific legacy symbol table
 *
 * @param symtable Symbol table to search
 * @param name Symbol name to find
 * @return Matching entry on success, NULL if not found
 */
symtable_entry_t *lookup_symbol(symtable_t *symtable, const char *name);

/**
 * @brief Look up a symbol by name across the legacy scope stack
 *
 * @param name Symbol name to find
 * @return Matching entry on success, NULL if not found
 */
symtable_entry_t *get_symtable_entry(const char *name);

/**
 * @brief Get the innermost (current) legacy symbol table
 *
 * @return Current local symbol table on success, NULL if uninitialized
 */
symtable_t *get_local_symtable(void);

/**
 * @brief Get the outermost (global) legacy symbol table
 *
 * @return Global symbol table on success, NULL if uninitialized
 */
symtable_t *get_global_symtable(void);

/**
 * @brief Get the legacy symbol table stack
 *
 * @return Pointer to the symbol table stack on success, NULL if uninitialized
 */
symtable_stack_t *get_symtable_stack(void);

/**
 * @brief Free a legacy symbol table and all its entries
 *
 * @param symtable Symbol table to free
 */
void free_symtable(symtable_t *symtable);

/**
 * @brief Set the string value of a legacy symbol table entry
 *
 * @param entry Entry to update
 * @param val New value (copied/owned per legacy semantics)
 */
void symtable_entry_setval(symtable_entry_t *entry, char *val);

/* ============================================================================
 * ENHANCED SYMBOL TABLE (libhashtable implementation)
 * ============================================================================
 */

/// Feature detection

/**
 * @brief Report whether the libhashtable-backed symbol table is available
 *
 * @return true if the enhanced (libhashtable) backend can be used
 */
bool symtable_libht_available(void);

/**
 * @brief Get a description of the active symbol table implementation
 *
 * @return Static string describing the selected backend
 */
const char *symtable_implementation_info(void);

/// Enhanced API (feature flag controlled)

/** @brief Initialize the libhashtable-backed symbol table */
void init_symtable_libht(void);

/** @brief Tear down the libhashtable-backed symbol table */
void free_symtable_libht(void);

/**
 * @brief Get the underlying libhashtable manager handle
 *
 * @return Opaque pointer to the libhashtable manager, or NULL if uninitialized
 */
void *get_libht_manager(void);

/// Enhanced variable operations

/**
 * @brief Set a variable using the enhanced (libhashtable) backend
 *
 * @param name Variable name
 * @param value Variable value
 * @param flags Variable flags
 * @return 0 on success, -1 on error
 */
int symtable_set_var_enhanced(const char *name, const char *value,
                              symvar_flags_t flags);

/**
 * @brief Get a variable value using the enhanced (libhashtable) backend
 *
 * @param name Variable name
 * @return Variable value or NULL if not found
 */
char *symtable_get_var_enhanced(const char *name);

/// Enhanced scope operations

/**
 * @brief Push a scope using the enhanced (libhashtable) backend
 *
 * @param type Scope type to push
 * @param name Scope name (for debugging)
 * @return 0 on success, -1 on error
 */
int symtable_push_scope_enhanced(scope_type_t type, const char *name);

/**
 * @brief Pop the current scope using the enhanced (libhashtable) backend
 *
 * @return 0 on success, -1 on error
 */
int symtable_pop_scope_enhanced(void);

/// Performance and testing

/**
 * @brief Run a benchmark comparison between symbol table backends
 *
 * @param iterations Number of iterations to execute per backend
 */
void symtable_benchmark_comparison(int iterations);

/**
 * @brief Run the libhashtable symbol table self-test
 *
 * @return 0 on success, non-zero on failure
 */
int symtable_libht_test(void);

/* ============================================================================
 * OPTIMIZED SYMBOL TABLE (libhashtable v2 implementation)
 * ============================================================================
 */

/// Feature detection

/**
 * @brief Report whether the optimized (libhashtable v2) backend is available
 *
 * @return true if the optimized backend can be used
 */
bool symtable_opt_available(void);

/**
 * @brief Get a description of the optimized symbol table implementation
 *
 * @return Static string describing the optimized backend
 */
const char *symtable_opt_implementation_info(void);

/// Optimized API (feature flag controlled)

/** @brief Initialize the optimized (libhashtable v2) symbol table */
void init_symtable_opt(void);

/** @brief Tear down the optimized (libhashtable v2) symbol table */
void free_symtable_opt(void);

/**
 * @brief Get the underlying optimized manager handle
 *
 * @return Opaque pointer to the optimized manager, or NULL if uninitialized
 */
void *get_opt_manager(void);

/// Optimized variable operations

/**
 * @brief Set a variable using the optimized (libhashtable v2) backend
 *
 * @param name Variable name
 * @param value Variable value
 * @param flags Variable flags
 * @return 0 on success, -1 on error
 */
int symtable_set_var_opt_api(const char *name, const char *value,
                             symvar_flags_t flags);

/**
 * @brief Get a variable value using the optimized (libhashtable v2) backend
 *
 * @param name Variable name
 * @return Variable value or NULL if not found
 */
char *symtable_get_var_opt_api(const char *name);

/// Optimized scope operations

/**
 * @brief Push a scope using the optimized (libhashtable v2) backend
 *
 * @param type Scope type to push
 * @param name Scope name (for debugging)
 * @return 0 on success, -1 on error
 */
int symtable_push_scope_opt_api(scope_type_t type, const char *name);

/**
 * @brief Pop the current scope using the optimized (libhashtable v2) backend
 *
 * @return 0 on success, -1 on error
 */
int symtable_pop_scope_opt_api(void);

/// Performance and testing

/**
 * @brief Run a benchmark comparison for the optimized backend
 *
 * @param iterations Number of iterations to execute
 */
void symtable_benchmark_opt_comparison(int iterations);

/**
 * @brief Run the optimized symbol table self-test
 *
 * @return 0 on success, non-zero on failure
 */
int symtable_opt_test(void);

/* ============================================================================
 * SPECIAL VARIABLE API
 * ============================================================================
 */

/**
 * @brief Set the current line number for $LINENO
 *
 * @param lineno Current line number
 */
void symtable_set_lineno(int lineno);

/**
 * @brief Get the current line number
 *
 * @return Current line number
 */
int symtable_get_lineno(void);

/**
 * @brief Reset SECONDS counter (makes $SECONDS return 0)
 */
void symtable_reset_seconds(void);

/**
 * @brief Seed the RANDOM generator
 *
 * @param seed New seed value (0 uses time-based seed)
 */
void symtable_seed_random(unsigned int seed);

/* ============================================================================
 * ARRAY VARIABLE API (Extended Language Support)
 * ============================================================================
 */

/**
 * @brief Create a new array value
 *
 * @param is_associative True for associative array (declare -A)
 * @return New array value or NULL on failure
 */
array_value_t *symtable_array_create(bool is_associative);

/**
 * @brief Free an array value and all its elements
 *
 * @param array Array to free
 */
void symtable_array_free(array_value_t *array);

/**
 * @brief Set an element in an indexed array
 *
 * @param array Target array
 * @param index Element index (0-based internally)
 * @param value Element value (will be copied)
 * @return 0 on success, -1 on error
 */
int symtable_array_set_index(array_value_t *array, int64_t index,
                             const char *value);

/**
 * @brief Get an element from an indexed array
 *
 * @param array Source array
 * @param index Element index (0-based internally)
 * @return Element value or NULL if not set
 */
const char *symtable_array_get_index(array_value_t *array, int64_t index);

/**
 * @brief Set an element in an associative array
 *
 * @param array Target array (must be associative)
 * @param key Element key
 * @param value Element value (will be copied)
 * @return 0 on success, -1 on error
 */
int symtable_array_set_assoc(array_value_t *array, const char *key,
                             const char *value);

/**
 * @brief Get an element from an associative array
 *
 * @param array Source array (must be associative)
 * @param key Element key
 * @return Element value or NULL if not set
 */
const char *symtable_array_get_assoc(array_value_t *array, const char *key);

/**
 * @brief Append a value to an indexed array
 *
 * @param array Target array
 * @param value Value to append
 * @return New element index, or -1 on error
 */
int64_t symtable_array_append(array_value_t *array, const char *value);

/**
 * @brief Get the number of elements in an array
 *
 * @param array Source array
 * @return Number of elements
 */
size_t symtable_array_length(array_value_t *array);

/**
 * @brief Unset an element in an indexed array
 *
 * @param array Target array
 * @param index Element index to unset
 * @return 0 on success, -1 on error
 */
int symtable_array_unset_index(array_value_t *array, int64_t index);

/**
 * @brief Unset an element in an associative array
 *
 * @param array Target array (must be associative)
 * @param key Element key to unset
 * @return 0 on success, -1 on error
 */
int symtable_array_unset_assoc(array_value_t *array, const char *key);

/**
 * @brief Get all keys/indices from an array
 *
 * For indexed arrays, returns string representations of indices.
 * For associative arrays, returns the keys.
 *
 * @param array Source array
 * @param count Output: number of keys returned
 * @return Array of key strings (caller must free array and strings)
 */
char **symtable_array_get_keys(array_value_t *array, size_t *count);

/**
 * @brief Get all values from an array
 *
 * @param array Source array
 * @param count Output: number of values returned
 * @return Array of value strings (caller must free array and strings)
 */
char **symtable_array_get_values(array_value_t *array, size_t *count);

/**
 * @brief Expand array to string for ${arr[*]} or ${arr[@]}
 *
 * @param array Source array
 * @param sep Separator for ${arr[*]}, NULL for ${arr[@]} (space-separated)
 * @return Expanded string (caller must free)
 */
char *symtable_array_expand(array_value_t *array, const char *sep);

/// Array Variable Management

/**
 * @brief Set a variable as an array in the CURRENT scope.
 *
 * Low-level current-scope writer (the array analog of symtable_set_var). Use
 * for explicit-scope writes (`local -a` / `declare -a` without -g, typed-
 * function parameter binding) and internal specials (PIPESTATUS). For an
 * unprefixed user assignment use symtable_assign_array so the write resolves
 * to the owning/global scope.
 *
 * @param name Variable name
 * @param array Array value (ownership transferred)
 * @return 0 on success, -1 on error
 */
int symtable_set_array(const char *name, array_value_t *array);

/**
 * @brief Force-write an array binding into the GLOBAL scope.
 *
 * Array analog of symtable_set_global_var: used by the `declare -g` array path
 * and by symtable_assign_array's miss-fallback.
 *
 * @param name Variable name
 * @param array Array value (ownership transferred)
 * @return 0 on success, -1 on error
 */
int symtable_set_array_global(const char *name, array_value_t *array);

/**
 * @brief Resolve the scope for a bare (unprefixed) array assignment (#614).
 *
 * Array analog of symtable_assign_var: walk the scope chain, update the owning
 * scope in place if the name is already bound (nearest wins), otherwise create
 * in the global scope. Enforces readonly across the chain via array->flags.
 *
 * @param name Variable name
 * @param array Array value (ownership transferred)
 * @return 0 on success, -1 on error, SYMTABLE_ERR_READONLY if a bound array in
 *         the chain is readonly
 */
int symtable_assign_array(const char *name, array_value_t *array);

/**
 * @brief Get an array variable
 *
 * @param name Variable name
 * @return Array value or NULL if not an array or not found
 */
array_value_t *symtable_get_array(const char *name);

/**
 * @brief Read the attribute flags carried by an array
 *
 * Returns SYMVAR_NONE if the name is not bound to an array (or
 * the array has no attributes set). Used by enforcement paths
 * such as symtable_set_array_element (whole-array readonly).
 *
 * @param name Variable name
 * @return Flags currently set on the array record
 */
symvar_flags_t symtable_array_get_flags(const char *name);

/**
 * @brief Add attribute flags to an array
 *
 * OR-merges @p add into the array's existing flags. No-op when the
 * name is not bound to an array; returns -1 in that case so callers
 * distinguish "applied" from "no array to apply to".
 *
 * @param name Variable name
 * @param add  Flags to add (bitwise OR into existing)
 * @return 0 on success, -1 if no array under @p name
 */
int symtable_array_add_flags(const char *name, symvar_flags_t add);

/**
 * @brief Unified value lookup (SEMANTICS section 3 first-class values)
 *
 * Populates @c out with whatever the symtable holds for @c name --
 * scalar, list, or map -- with one call. Implementation walks the
 * array side-table first (lists/maps), then the scalar scope chain
 * (symtable_get_var) for a fallback to a scalar binding.
 *
 * On a hit, returns true and out->kind is one of LUSH_VALUE_SCALAR /
 * LUSH_VALUE_LIST / LUSH_VALUE_MAP; out->scalar_value (owned) or
 * out->array (borrowed) is populated accordingly. The caller MUST
 * call lush_value_view_clear(&out) when done, even on a hit -- it is
 * the only safe way to release a scalar strdup.
 *
 * On a miss, returns false; out->kind is LUSH_VALUE_NONE and the
 * other fields are zeroed. lush_value_view_clear is a no-op on a
 * cleared view, so the same call site can clear unconditionally.
 *
 * @param name Variable name (must not be NULL)
 * @param out  Output view (must not be NULL; zeroed on entry)
 * @return true if @c name is bound, false otherwise.
 */
bool symtable_lookup(const char *name, lush_value_view_t *out);

/**
 * @brief Free any owned storage in a value view
 *
 * Frees out->scalar_value if non-NULL and zeroes the struct. The
 * array field is borrowed from the symtable and is not freed. Safe
 * to call multiple times or on a never-populated view.
 *
 * @param view View to clear (NULL-safe; out fields zeroed on return)
 */
void lush_value_view_clear(lush_value_view_t *view);

/**
 * @brief Check if a variable is an array
 *
 * @param name Variable name
 * @return True if variable is an array
 */
bool symtable_is_array(const char *name);

/**
 * @brief Set an array element using shell syntax
 *
 * Handles both arr[index]=value and arr[key]=value (associative)
 *
 * @param name Variable name
 * @param subscript Index or key string
 * @param value Element value
 * @return 0 on success, -1 on error
 */
int symtable_set_array_element(const char *name, const char *subscript,
                               const char *value);

/**
 * @brief Get an array element using shell syntax
 *
 * @param name Variable name
 * @param subscript Index or key string
 * @return Element value or NULL
 */
char *symtable_get_array_element(const char *name, const char *subscript);

/**
 * @brief Enumerate all arrays with callback
 *
 * @param callback Function called for each array (name, array, userdata)
 * @param userdata User data passed to callback
 */
void symtable_enumerate_arrays(void (*callback)(const char *name,
                                                array_value_t *array,
                                                void *userdata),
                               void *userdata);

#endif /// SYMTABLE_H
