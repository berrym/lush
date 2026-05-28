/**
 * @file node.h
 * @brief Abstract Syntax Tree (AST) node definitions
 *
 * Defines the node types and structures used to represent parsed shell
 * commands as an abstract syntax tree. Includes command nodes, redirections,
 * pipelines, control structures, and more.
 *
 * @author Michael Berry <trismegustis@gmail.com>
 * @copyright Copyright (C) 2021-2026 Michael Berry
 */

#ifndef NODE_H
#define NODE_H

#include "shell_error.h" /// For source_location_t

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

typedef enum {
    NODE_COMMAND,           ///< Simple command (argv-style)
    NODE_ASSIGN,            ///< cmd_prefix assignment: val.str = "var=value" /
                            ///< "var+=value"
    NODE_VAR,               ///< Variable reference
    NODE_STRING_LITERAL,    ///< Single-quoted string - no expansion
    NODE_STRING_EXPANDABLE, ///< Double-quoted string - variable expansion
    NODE_ARITH_EXP,         ///< Arithmetic expansion $((expr))
    NODE_COMMAND_SUB,       ///< Command substitution $(cmd)
    NODE_PIPE,              ///< Pipe operator '|'
    NODE_REDIR_IN,          ///< '<'
    NODE_REDIR_OUT,         ///< '>'
    NODE_REDIR_APPEND,      ///< '>>'
    NODE_REDIR_ERR,         ///< 'N>' (any fd output, e.g., 2>, 3>)
    NODE_REDIR_IN_FD,       ///< 'N<' (any fd input, e.g., 3<, 4<)
    NODE_REDIR_ERR_APPEND,  ///< 'N>>' (any fd append, e.g., 2>>, 3>>)
    NODE_REDIR_HEREDOC,     ///< '<<'
    NODE_REDIR_HEREDOC_STRIP, ///< '<<-'
    NODE_REDIR_HERESTRING,    ///< '<<<'
    NODE_REDIR_BOTH,          ///< '&>'
    NODE_REDIR_BOTH_APPEND,   ///< '&>>' - append both stdout and stderr
    NODE_REDIR_FD,            ///< '&1', '&2', etc.
    NODE_REDIR_FD_ALLOC,      ///< '{varname}>' - fd allocation (bash 4.1+/zsh)
    NODE_REDIR_CLOBBER,       ///< '>|'
    /// List types for semantic clarity
    NODE_COMMAND_LIST, ///< Sequence of commands separated by semicolons
    NODE_PIPELINE,     ///< Sequence of commands connected by pipes
    /// Control structures
    NODE_IF,          ///< if statement
    NODE_FOR,         ///< for loop (POSIX: for var in words)
    NODE_FOR_ARITH,   ///< C-style for loop: for ((init; test; update))
    NODE_WHILE,       ///< while loop
    NODE_UNTIL,       ///< until loop
    NODE_REPEAT,      ///< zsh repeat N loop: repeat N do/done OR repeat N {...}
    NODE_CASE,        ///< case statement
    NODE_FUNCTION,    ///< function definition
    NODE_BRACE_GROUP, ///< brace group { commands; }
    NODE_SUBSHELL,    ///< subshell ( commands )
    /// Logical operators
    NODE_LOGICAL_AND, ///< && operator
    NODE_LOGICAL_OR,  ///< || operator
    /// Job control
    NODE_BACKGROUND, ///< & operator (background execution)

    /// Extended language features (Phase 1: Arrays and Arithmetic)
    NODE_ARITH_CMD,     ///< (( expr )) - arithmetic command evaluation
    NODE_ARRAY_LITERAL, ///< (a b c) - array literal
    NODE_ARRAY_ACCESS,  ///< ${arr[index]} - array element access
    NODE_ARRAY_ASSIGN,  ///< arr[n]=value or arr=(...) - array assignment
    NODE_ARRAY_APPEND,  ///< arr+=(a b c) - append elements to array

    /// Extended language features (Phase 2: Extended Tests)
    NODE_EXTENDED_TEST, ///< [[ expr ]] - extended test command

    /// Extended language features (Phase 3: Process Substitution)
    NODE_PROC_SUB_IN,  ///< <(cmd) - process substitution input
    NODE_PROC_SUB_OUT, ///< >(cmd) - process substitution output
    NODE_COPROC,       ///< coproc name cmd - coprocess

    /// Extended language features (Phase 5: Control Flow)
    NODE_CASE_ITEM, ///< Case item with terminator type
    NODE_SELECT,    ///< select var in list; do body; done
    NODE_TIME,      ///< time [-p] pipeline
    NODE_NEGATE,    ///< ! pipeline - negate exit status

    /// Extended language features (Phase 7: Zsh-Specific)
    NODE_ANON_FUNCTION, ///< () { body } - anonymous function (immediately
                        ///< executed)

    /**
     * @brief Typed-function form AST nodes.
     *
     * NODE_FN_DECL is a typed-function declaration. Its val.str packs
     * the signature as
     *   "name\x1F<return_kind>\x1F<p1_name>:<p1_kind>\x1F<p2_name>:<p2_kind>..."
     * with return_kind = "" for void; first_child is the body (a brace
     * group or compound command).
     *
     * NODE_FN_CALL is a typed-function call expression used as the
     * right-hand side of a NODE_LET_FN. val.str is the callee name;
     * children are the argument expressions in declaration order.
     *
     * NODE_FN_RETURN is a `return [expression]` statement inside a fn
     * body; first_child is the return-value expression (NULL for void).
     *
     * NODE_LET_FN is the `let name = name(args)` capture form. val.str
     * is the LHS variable name; first_child is the NODE_FN_CALL.
     */
    NODE_FN_DECL,
    NODE_FN_CALL,
    NODE_FN_RETURN,
    NODE_LET_FN,
} node_type_t;

/// Case item terminator types for fall-through behavior
typedef enum {
    CASE_TERM_BREAK,       ///< ;; - stop processing (default)
    CASE_TERM_FALLTHROUGH, ///< ;& - execute next item without pattern test
    CASE_TERM_CONTINUE,    ///< ;;& - continue testing next patterns
} case_terminator_t;

typedef enum {
    VAL_SINT = 1, ///< Signed integer (ssize_t)
    VAL_UINT,     ///< Unsigned integer (size_t)
    VAL_SLLONG,   ///< Signed 64-bit integer (int64_t)
    VAL_ULLONG,   ///< Unsigned 64-bit integer (uint64_t)
    VAL_FLOAT,    ///< Double-precision floating point
    VAL_LDOUBLE,  ///< Long double floating point
    VAL_CHR,      ///< Single character
    VAL_STR,      ///< Heap-allocated C string
} val_type_t;

typedef union {
    ssize_t sint;        ///< Signed integer storage
    size_t uint;         ///< Unsigned integer storage
    int64_t sllong;      ///< Signed 64-bit storage
    uint64_t ullong;     ///< Unsigned 64-bit storage
    double sfloat;       ///< Double-precision floating point storage
    long double ldouble; ///< Long double storage
    char ch;             ///< Single character storage
    char *str;           ///< Heap-allocated string (owner)
} symval_t;

typedef struct node {
    node_type_t type;                         ///< AST node kind
    val_type_t val_type;                      ///< Active union member of `val`
    symval_t val;                             ///< Node payload (type-tagged)
    size_t children;                          ///< Count of direct children
    struct node *first_child;                 ///< First child in linked list
    struct node *next_sibling, *prev_sibling; ///< Sibling linked-list pointers

    /// Source location tracking for error reporting
    source_location_t loc;
} node_t;

/**
 * @brief Create a new AST node
 *
 * Allocates and initializes a new AST node of the specified type.
 * The node is initialized with default values and no children.
 *
 * @param type Node type to create
 * @return New node on success, NULL on allocation failure
 */
node_t *new_node(node_type_t type);

/**
 * @brief Create a new AST node with source location
 *
 * Allocates and initializes a new AST node of the specified type,
 * capturing the source location for error reporting.
 *
 * @param type Node type to create
 * @param loc Source location from token
 * @return New node on success, NULL on allocation failure
 */
node_t *new_node_at(node_type_t type, source_location_t loc);

/**
 * @brief Add a child node to a parent
 *
 * Appends a child node to the parent's list of children.
 * The child's sibling pointers are updated appropriately.
 *
 * @param parent Parent node to add child to
 * @param child Child node to add
 */
void add_child_node(node_t *parent, node_t *child);

/**
 * @brief Free an entire AST tree
 *
 * Recursively frees all nodes in the AST starting from the given root.
 * Frees all children, siblings, and any allocated string values.
 *
 * @param node Root node of tree to free (NULL is safely ignored)
 */
void free_node_tree(node_t *node);

/**
 * @brief Set node value to a string
 *
 * Sets the node's value to the given string and updates the value type.
 * The node takes ownership of the string pointer.
 *
 * @param node Node to modify
 * @param str String value (node takes ownership, caller should not free)
 */
void set_node_val_str(node_t *node, char *str);

#endif
