/* Copyright (C) 2025 Salvatore Bertino */
/*
 * @file toylisp.c
 * @brief the main file of the project.
 */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdarg.h>
#include <stdint.h>
#include <math.h>
#include <string.h>

#if !defined(_WIN32) && !defined(_WIN64)
    #define _POSIX_C_SOURCE 200809L
    #include <unistd.h>
#endif

#include <stdbool.h>

#include "arena.h"

#define MAX_ERR_MSG_LEN 256

/* ---------------------- Definitions an Data Structures -------------------- */

/**
 * @struct Context
 * @brief Holds the execution context for the interpreter.
 *
 * This structure bundles together all the core services and state required
 * for parsing, evaluating, and managing memory within a single interpreter
 * instance. It is passed to most functions to provide access to these services,
 * ensuring thread-safety and modularity.
 */
typedef struct Context {
    /**
     * @brief A pointer to the pointer of the permanent memory arena.
     *
     * This arena is used for long-lived allocations that must persist
     * across multiple evaluation cycles, such as function definitions,
     * loaded source code, and global constants.
     */
    Arena** permanent_arena;

    /**
     * @brief A pointer to the pointer of the temporary (scratch) memory arena.
     *
     * This arena is used for short-lived allocations that are only needed
     * during a single evaluation cycle (e.g., for intermediate results).
     * Its contents can be discarded after each top-level expression is evaluated.
     */
    Arena** temporary_arena;
} Context;

/**
 * @enum ValueType
 * @brief Enumerates all possible data types for a Value object.
 *
 * This enumeration is used as a "tag" in the Value struct's union
 * to determine which field of the union is active.
 */
typedef enum {
    TYPE_NIL,       ///< The nil value (empty list or boolean false).
    TYPE_NUMBER,    ///< A double-precision floating-point number.
    TYPE_ATOM,      ///< A symbol/atom, represented by its name.
    TYPE_PRIMITIVE, ///< A built-in primitive function, identified by an index.
    TYPE_CONS,      ///< A cons cell, the basic building block for lists.
    TYPE_STRING,    ///< A character string.
    TYPE_CLOSURE,   ///< A user-defined function (lambda), with its environment.
    TYPE_MACRO,     ///< A user-define macro.
    TYPE_ERROR,     ///< An error object, containing a message.
    TYPE_UNDEFINED, ///< Used by the undefine! function to mark the unused memory
} ValueType;

/** @struct Value
 * @brief The universal data structure for every value in the interpreter.
 *
 * It uses a "tagged union" to efficiently represent all possible
 * data types defined in ValueType.
 */
typedef struct Value {
    ValueType type; ///< The tag indicating the current data type. */
    union {
        double number;                  ///< Valid if type is TYPE_NUMBER.
        const char* atom_name;          ///< Valid if type is TYPE_ATOM.
        const char* string;             ///< Valid if type is TYPE_STRING.
        char* err_msg;                  ///< Valid if type is TYPE_ERROR.
        unsigned int primitive_index;   ///< Valid if type is TYPE_PRIMITIVE.
        struct ConsCell* cons;          ///< Valid if type is TYPE_CONS.
        struct Closure* closure;        ///< Valid if type is TYPE_CLOSURE.
        struct Macro* macro;            ///< Valid if type is TYPE_MACRO.
    } as; ///< The union holding the actual data.
} Value;

/**
 * @struct ConsCell
 * @brief Represents a single "cons" pair, the basic building block of lists.
 *
 * A Lisp list is a chain of ConsCells that ends with a `nil`.
 */
typedef struct ConsCell {
    Value car; ///< The first element of the pair (the "contents").
    Value cdr; ///< The second element of the pair (the "pointer" to the rest of the list).
} ConsCell;

/**
 * @struct Closure
 * @brief Represents a lambda function created at runtime.
 *
 * It contains the formal parameters, the function body, and a reference
 * to the lexical environment in which it was defined (capturing variables).
 * @see make_closure
 */
typedef struct Closure {
    Value params; ///< A list of atoms representing the parameters.
    Value body;   ///< A list of expressions that make up the function body.
    Value env;    ///< The environment (a list of frames) captured at definition time.
} Closure;

/*
 * @struct Macro
 * @brief Represents a macro created by the user or at runtime.
 *
 * It contains all the right paramaters, and is built by using make_macro.
 * @see make_macro
 */
typedef struct Macro {
    Value params;
    Value body;
    Value env;
} Macro;

/**
 * @typedef PrimitiveFunc
 * @brief Pointer to a C function that implements a Lisp primitive.
 * @param args A Lisp list containing the arguments passed to the function.
 * @param env The current evaluation environment.
 * @return A Value object representing the result of the primitive.
 */
typedef Value (*PrimitiveFunc)(Value args, Value env, Context* arena);

/**
 * @struct PrimitiveEntry
 * @brief Describes a single primitive function in the system.
 *
 * Maps a Lisp name to a C function and specifies its arity.
 */
typedef struct {
    const char* name;    ///< The name the function is exposed as in Lisp (e.g., "+", "car"). */
    PrimitiveFunc func;  ///< The pointer to the C function that implements it. */
    size_t arity;        ///< The number of expected arguments (SIZE_MAX for variable arity). */
} PrimitiveEntry;

/**
 * @var primitives
 * @brief A global array that contains the table of all primitive functions.
 *
 * The interpreter uses this table to look up built-in functions by name.
 * It is terminated by an entry whose fields are NULL/0.
 */
extern PrimitiveEntry primitives[];

/* ----------------- Memory Managment ----------------- */

/**
 * @addtogroup value_constructors Value Object Constructors
 * @brief A set of factory functions for creating `Value` objects.
 *
 * These functions encapsulate the logic for allocating memory (where necessary)
 * and correctly setting the type tag and data for various `Value` types. They
 * are the primary way to create new values within the interpreter.
 * @{
 */

/**
 * @brief Creates a `Value` of type `TYPE_NUMBER`.
 * @param n The double-precision floating-point number.
 * @return A new `Value` object representing the number.
 */
Value make_number(double n) {
    Value v;
    v.type = TYPE_NUMBER;
    v.as.number = n;
    return v;
}

/**
 * @brief Creates a `Value` of type `TYPE_ATOM`.
 * @details Allocates memory and copies the provided name.
 * @param name The symbol name to be used for the atom (will be copied).
 * @param arena The arena used for the allocation.
 * @return A new `Value` object representing the atom.
 */
Value make_atom(const char* name, Arena** arena) {
    Value v;
    v.type = TYPE_ATOM;
    v.as.atom_name = arena_strdup(arena, name);
    return v;
}

/**
 * @brief Creates a `Value` of type `TYPE_CONS` (a list pair).
 * @details Allocates a new `ConsCell` to hold the car and cdr values.
 * @param car The first element of the pair (the value).
 * @param cdr The second element of the pair (the rest of the list).
 * @param arena The arena used for the allocation.
 * @return A new `Value` object pointing to the cons cell.
 */
Value make_cons(Value car, Value cdr, Arena** arena) {
    ConsCell* cell = (ConsCell*)arena_alloc(arena, sizeof(ConsCell));
    cell->car = car;
    cell->cdr = cdr;

    Value v;
    v.type = TYPE_CONS;
    v.as.cons = cell;
    return v;
}

/**
 * @brief Creates a `Value` of type `TYPE_STRING`.
 * @details Allocates memory and copies the provided C string.
 * @param s The character string to wrap (will be copied).
 * @param arena The arena used for the allocation.
 * @return A new `Value` object representing the string.
 */
Value make_string(const char* s, Arena** arena) {
    Value v;
    v.type = TYPE_STRING;
    v.as.string = arena_strdup(arena, s);
    return v;
}

/**
 * @brief Creates a `Value` of type `TYPE_ERROR` with a formatted message.
 * @param arena The arena used for the allocation.
 * @param format The printf-style format string for the error message.
 * @param ... The variable arguments for the format string.
 * @return A new `Value` object representing the error.
 */
Value make_error(Arena** arena, const char *format, ...) {
    char buffer[MAX_ERR_MSG_LEN];

    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    char *msg = arena_strdup(arena, buffer);
    if (!msg) {
        fprintf(stderr, "FATAL: Out of memory while creating error message\n");
        exit(EXIT_FAILURE);
    }

    Value err_val;
    err_val.type = TYPE_ERROR;
    err_val.as.err_msg = msg;
    return err_val;
}

/**
 * @brief Creates a `Value` of type `TYPE_CLOSURE`.
 * @details Allocates a `Closure` struct to hold the function's components.
 * @param params A list of atoms representing the function's formal parameters.
 * @param body A list of expressions representing the function's body.
 * @param env The captured lexical environment.
 * @param arena The arena used for the allocation.
 * @return A new `Value` object representing the closure.
 */
Value make_closure(Value params, Value body, Value env, Arena** arena) {
    Closure* cl = (Closure*)arena_alloc(arena, sizeof(Closure));
    cl->params = params;
    cl->body = body;
    cl->env = env;

    Value v;
    v.type = TYPE_CLOSURE;
    v.as.closure = cl;
    return v;
}

/**
 * @brief Creates and allocates a raw `Closure` struct pointer.
 * @details This is a helper for internal use, returning a pointer rather than a `Value`.
 * @param params A list of atoms representing the function's formal parameters.
 * @param body A list of expressions representing the function's body.
 * @param env The captured lexical environment.
 * @param arena The arena used for the allocation.
 * @return A pointer to the newly allocated `Closure`, not a `Value` object.
 */
Closure* make_closure_ptr(Value params, Value body, Value env, Arena** arena) {
    Closure* cl = (Closure*)arena_alloc(arena, sizeof(Closure));
    cl->params = params;
    cl->body = body;
    cl->env = env;
    return cl;
}


/**
 * @brief Creates a `Value` of type `TYPE_MACRO`.
 * @details Allocates a `Macro` struct to hold the macro's components and wraps it in a `Value`.
 *          This is the primary constructor for macros in the system.
 * @param params A list of atoms representing the macro's formal parameters.
 * @param body A list of expressions (unquoted template) representing the macro's expansion code.
 * @param env The captured lexical environment at macro definition time.
 * @param arena The arena used for the allocation.
 * @return A new `Value` object representing the macro.
 */
Value make_macro(Value params, Value body, Value env, Arena** arena) {
    Macro* m = (Macro*)arena_alloc(arena, sizeof(Macro));
    m->params = params;
    m->body = body;
    m->env = env;

    Value v;
    v.type = TYPE_MACRO;
    v.as.macro = m;
    return v;
}

/**
 * @brief Creates and allocates a raw `Macro` struct pointer.
 * @details This is a helper for internal use, returning a pointer rather than a `Value`.
 *          Used when manual control over wrapping is needed.
 * @param params A list of atoms representing the macro's formal parameters.
 * @param body A list of expressions representing the macro's expansion template.
 * @param env The captured lexical environment at macro definition time.
 * @param arena The arena used for the allocation.
 * @return A pointer to the newly allocated `Macro`, not a `Value` object.
 */
Macro* make_macro_ptr(Value params, Value body, Value env, Arena** arena) {
    Macro* m = (Macro*)arena_alloc(arena, sizeof(Macro));
    m->params = params;
    m->body = body;
    m->env = env;
    return m;
}

/** @} */ // End of value_constructors group



// ---------------- Variable and constants ------------------


Value NIL_VALUE;
Value TRUE_VALUE;
Value ERROR_VALUE;
Value global_env;

/*
 * @addtogroup predicates These are the non-primitive helpers to write
 *                        the predicates and other parts of the program
 * @brief Utility functions for implementing the prim_* and value-checking.
 * @{
 */

/**
 * @param v A value to check.
 * @return Returns a bool iff the input is a of type nil.
 */
bool is_nil(Value v) {
    return v.type == TYPE_NIL;
}

/**
 * @param v A value to value.
 * @return The opposite of is_nil, therefore returns true
 *         iff the value is not nil.
 */
bool is_truthy(Value v) {
    return v.type != TYPE_NIL;
}

/**
 * @brief A simple dispatcher to implement a function that return 
 *        true if two values are equal *IN MEMORY*.
 * @param a,b The value to check if they are equal.
 * @return True iff they are equal and false if thats not the case.
 */
bool are_equal(Value a, Value b) {
    if (a.type != b.type) return false;
    switch (a.type) {
        case TYPE_UNDEFINED:
        case TYPE_NIL: {
            return true;
        }
        case TYPE_NUMBER:  return a.as.number == b.as.number;
        case TYPE_STRING:  return strcmp(a.as.string, b.as.string) == 0;
        case TYPE_ATOM:    return strcmp(a.as.atom_name, b.as.atom_name) == 0;
        case TYPE_CONS:    return a.as.cons == b.as.cons;
        case TYPE_CLOSURE: return a.as.closure == b.as.closure;
        case TYPE_PRIMITIVE: return a.as.primitive_index == b.as.primitive_index;
        case TYPE_MACRO: return a.as.macro == b.as.macro;
        case TYPE_ERROR: return strcmp(a.as.err_msg, b.as.err_msg) == 0;
    }
    return false;
}
/* }@ // End of predicates group */



/*
 * @addtogroup Non-yet-primitive Value operators
 * @brief A group of functions dedicated to easily implement the primitives
 *        and get things like the car or cdr of a cons.
 * @{
 */

Value car(Value p) {
    if (p.type != TYPE_CONS) return ERROR_VALUE;
    return p.as.cons->car;
}

Value cdr(Value p) {
    if (p.type != TYPE_CONS) return ERROR_VALUE;
    return p.as.cons->cdr;
}

Value make_env_pair(Value var, Value val, Value env, Arena** arena) {
    return make_cons(make_cons(var, val, arena), env, arena);
}

Value find_env_frame(Value var, Value env) {
    while (!is_nil(env)) {
        Value frame = car(env);
        if (frame.type == TYPE_CONS && are_equal(car(frame), var)) {
            return frame;
        }
        env = cdr(env);
    }
    return NIL_VALUE;
}

Value find_in_env(Value var, Value env, Arena** arena) {
    while (!is_nil(env)) {
        Value pair = car(env);
        if (are_equal(var, car(pair))) {
            Value val = cdr(pair);
            if (val.type == TYPE_UNDEFINED) {
                if (var.type == TYPE_ATOM) {
                    return make_error(arena, "undefined variable: %s", var.as.atom_name);
                }
                return make_error(arena, "undefined variable");
            }
            return val;
        }
        env = cdr(env);
    }

    if (var.type == TYPE_ATOM) {
        return make_error(arena, "undefined variable: %s", var.as.atom_name);
    }
    return make_error(arena, "undefined variable");
}

Value bind_args(Value params, Value args, Value env, Arena** arena) {
    if (is_nil(params)) return env;
    if (params.type == TYPE_CONS) {
        return bind_args(cdr(params), cdr(args), make_env_pair(car(params), car(args), env, arena), arena);
    }
    return make_env_pair(params, args, env, arena);
}

/* @{ ///< The end of the group: Non-yet-primitive Value operators */

/**
 * @addtogroup list_utilities List Utility Functions
 * @brief Functions for operating on Lisp-style lists.
 *
 * These helpers provide common operations for lists, which are represented as
 * chains of `ConsCell`s terminating in `nil`.
 * @{
 */

/**
 * @brief Calculates the proper length of a Lisp list.
 * @details A proper list is a chain of `cons` cells ending in `nil`. This
 *          function traverses the list, counting the cells. If the list is
 *          improper (does not end in `nil`), it counts up to the non-cons cdr.
 * @param list The list `Value` to measure.
 * @return The number of `cons` cells in the list chain.
 */
size_t list_length(Value list) {
    size_t count = 0;
    while (list.type == TYPE_CONS) {
        count++;
        list = list.as.cons->cdr;
    }
    return count;
}


/**
 * @brief Checks if a given Value is a proper Lisp list.
 * @details A proper list is either the empty list (nil) or a chain of cons
 *          cells that terminates with nil. This function robustly checks for
 *          this condition by traversing the list structure. It also includes
 *          Floyd's cycle-finding algorithm (the "tortoise and the hare") to
 *          safely handle and detect circular lists, preventing infinite loops.
 *
 *          - `'(a b c)` is a proper list.
 *          - `'()` is a proper list.
 *          - `'(a . b)` (a dotted pair) is not a proper list.
 *          - A circular list like `#1=(a . #1#)` is not a proper list.
 *
 * @param list The Value object to check.
 * @return `true` if `list` is a proper list, `false` otherwise.
 */
bool is_proper_list(Value list) {
    Value slow = list;
    Value fast = list;

    while (true) {
        if (is_nil(slow)) {
            return true;
        }
        if (slow.type != TYPE_CONS) {
            return false;
        }

        slow = cdr(slow);
        if (fast.type != TYPE_CONS || cdr(fast).type != TYPE_CONS) {
            fast = NIL_VALUE; 
        } else {
            fast = cdr(cdr(fast));
        }

        if (!is_nil(fast) && are_equal(slow, fast)) {
            return false;
        }
    }
}

Value copy_value_to_arena(Value v, Arena** dest_arena) {
    switch (v.type) {
        case TYPE_NIL:
        case TYPE_NUMBER:
        case TYPE_PRIMITIVE:
            return v;

        case TYPE_ATOM:
            return make_atom(v.as.atom_name, dest_arena);

        case TYPE_STRING:
            return make_string(v.as.string, dest_arena);

        case TYPE_ERROR:
            return make_error(dest_arena, "%s", v.as.err_msg);

        case TYPE_CONS: {
            Value copied_car = copy_value_to_arena(v.as.cons->car, dest_arena);
            Value copied_cdr = copy_value_to_arena(v.as.cons->cdr, dest_arena);
            return make_cons(copied_car, copied_cdr, dest_arena);
        }
        case TYPE_CLOSURE: {
            Closure* old_cl = v.as.closure;
            Value copied_params = copy_value_to_arena(old_cl->params, dest_arena);
            Value copied_body = copy_value_to_arena(old_cl->body, dest_arena);
            return make_closure(copied_params, copied_body, old_cl->env, dest_arena);
        }
        default:
            return ERROR_VALUE;
    }
}

/** @} */ // End of list_utilities group

/**
 * @addtogroup type_helpers Type and Debugging Helpers
 * @brief Utility functions for introspection and debugging.
 * @{
 */

/**
 * @brief Returns a human-readable string representation of a `ValueType`.
 * @param type The enum value from `ValueType`.
 * @return A constant string name for the type, or "unknown" if not found.
 */
const char* type_name(ValueType type) {
    switch (type) {
        case TYPE_NIL: return "nil";
        case TYPE_NUMBER: return "number";
        case TYPE_ATOM: return "atom";
        case TYPE_STRING: return "string";
        case TYPE_CONS: return "pair";
        case TYPE_CLOSURE: return "closure";
        case TYPE_PRIMITIVE: return "primitive";
        case TYPE_ERROR: return "error";
        case TYPE_UNDEFINED: return "undefined";
        default: return "unknown";
    }
}

/** @} */ // End of type_helpers group


/* ------------------- Interpreter: eval and apply ---------------- */

void print_value(Value v);

/**
 * @addtogroup core_evaluator Core Expression Evaluator
 * @brief The heart of the Lisp interpreter, responsible for evaluating expressions.
 *
 * This group contains the mutually recursive functions `eval_expression` and `eval_list`,
 * which together form the core evaluation logic. They implement the classic 'eval/apply'
 * cycle of a Lisp interpreter. `eval_expression` is the main entry point for
 * evaluation, determining the type of an expression and deciding how to process it.
 * `eval_list` is a crucial helper function used to evaluate the arguments of a
 * function call before the function is applied.
 *
 * The interplay between these two functions forms a recursive dance:
 * 1.  `eval_expression` is called on a function-call expression like `(+ 1 2)`.
 * 2.  It identifies `+` as the function and `(1 2)` as the arguments.
 * 3.  It calls the corrisponding primitives on `(1 2)` to get a new list of evaluated results, `(1 2)`.
 * 4.  It then applies the function `+` to the resulting arguments.
 *
 * Both functions operate within the context of an `env` (environment), which is
 * a list of frames used for lexical variable lookup.
 * @{
 */

/**
 * @brief Evaluates a single Lisp expression in a given environment.
 *
 * This is the central `eval` function. It takes an expression and an environment
 * and returns the computed value. Its behavior depends on the type of the expression:
 *
 * - **Self-evaluating types:** Numbers, strings, `nil`, closures, and errors
 *   evaluate to themselves.
 * - **Symbols (Atoms):** The function looks up the symbol's value in the
 *   `env` (environment). If not found, it's an error.
 * - **Lists (Cons Cells):** A list is treated as a function application.
 *   - The `car` of the list is evaluated to get a function (either a primitive or a closure).
 *   - The `cdr` of the list (the arguments) is passed to primitives functions to be treated.
 *   - The resulting function is then applied to the evaluated arguments.
 *   - **Special Forms:** If the `car` is a special form (like `quote`, `if`, `define`, `lambda`),
 *     this function handles it specially, as they do not follow the standard
 *     rule of evaluating all arguments first.
 *
 * @param expr The `Value` object representing the Lisp expression to evaluate.
 * @param env The current evaluation environment, used for symbol lookup.
 * @return A `Value` object representing the result of the evaluation. This can
 *         be a computed value or an error object if evaluation fails.
 */
Value eval_expression(Value expr, Value env, Context* context);

/**
 * @brief Recursively evaluates every element in a list of expressions.
 *
 * For example, if given the list `((+ 1 1) 3)`, it will return a new list `(2 3)`.
 *
 * @param list A `Value` representing a list of expressions to be evaluated.
 *             Must be a proper list (ending in `nil`).
 * @param env The environment in which to evaluate each expression.
 * @return A new list containing the evaluated results of the input list's elements.
 *         If the input is `nil`, it returns `nil`.
 * @see eval_expression
 */
Value eval_list(Value list, Value env, Context* context) {
    if (is_nil(list)) {
        return NIL_VALUE;
    }

    Value evaluated_car = eval_expression(car(list), env, context);

    if (evaluated_car.type == TYPE_ERROR) {
        return evaluated_car;
    }

    Value evaluated_cdr = eval_list(cdr(list), env, context);

    if (evaluated_cdr.type == TYPE_ERROR) {
        return evaluated_cdr;
    }

    return make_cons(evaluated_car, evaluated_cdr, context->temporary_arena);
}

/** @} */ // End of the core_evaluator group

// Normal Bultin Functions

/**
 * @addtogroup core_primitives Primitives and Special Forms
 * @brief A collection of all primitive functions and special forms built into the interpreter.
 *
 * This group contains the C implementation of all the language's fundamental
 * operations. Given their basic nature and standard behavior (mirroring
 * that of Lisp/Scheme), they are not documented individually.
 *
 * They are divided into two main categories:
 *
 * 1.  **Normal Primitive Functions**:
 *     Includes operations like `+`, `cons`, `car`, `eq?`, etc. They follow
 *     standard evaluation rules: all their arguments are evaluated
 *     *before* the function itself is executed.
 *
 * 2.  **Special Forms**:
 *     Includes constructs like `if`, `define`, `lambda`, `quote`, `and`, `or`.
 *     These constructs **do not** follow the standard evaluation rules and
 *     control *how* and *if* their operands are evaluated. They are
 *     essential for implementing control flow, variable definitions, and
 *     fundamental abstractions.
 *
 * All of these functions share the same C signature: `Value func(Value args, Value env)`.
 * @{
 */
Value prim_cons(Value args, Value env, Context* context) {
    return make_cons(car(args), car(cdr(args)), context->temporary_arena);
}

Value prim_list(Value args, Value env, Context* context) {
    (void)env;
    return args;
}

Value prim_car(Value args, Value env, Context* context) {
    return car(car(args));
}

Value prim_cdr(Value args, Value env, Context* context) {
    return cdr(car(args));
}

Value prim_is_pair(Value args, Value env, Context* context);

Value prim_reverse(Value args, Value env, Context* context) {
    Value list_to_reverse = car(args);

    if (!is_proper_list(list_to_reverse)) {
        return make_error(context->temporary_arena, "reverse: the argument is not a proper list.");
    }

    Value current = list_to_reverse;
    Value reversed_list = NIL_VALUE;

    while (!is_nil(current)) {
        reversed_list = make_cons(car(current), reversed_list, context->temporary_arena);
        current = cdr(current);
    }

    return reversed_list;
}

Value prim_len(Value args, Value env, Context* context) {
    size_t count = 0;
    while (args.type == TYPE_CONS) {
        count++;
        args = args.as.cons->cdr;
    }
    Value result = {.type = TYPE_NUMBER, .as.number = count};
    return result;
}

Value prim_apply(Value args, Value env, Context* context);

Value prim_mapcar(Value args, Value env, Context* context) {
    Value func = car(args);
    Value list = car(cdr(args));

    if (!is_proper_list(list)) {
        return make_error(context->temporary_arena, "mapcar: The second argument is not a proper list.");
    }

    Value result = NIL_VALUE;
    Value* tail = &result;

    while (!is_nil(list)) {
        Value single_arg = make_cons(car(list), NIL_VALUE, context->temporary_arena);
        Value apply_args = make_cons(func, make_cons(single_arg, NIL_VALUE, context->temporary_arena), context->temporary_arena);

        Value mapped = prim_apply(apply_args, env, context);

        if (mapped.type == TYPE_ERROR) return mapped;

        Value new_cell = make_cons(mapped, NIL_VALUE, context->temporary_arena);
        *tail = new_cell;
        tail = &new_cell.as.cons->cdr;

        list = cdr(list);
    }

    return result;
}

Value prim_filter(Value args, Value env, Context* context) {
    if (args.type != TYPE_CONS || cdr(args).type != TYPE_CONS) {
        return make_error(context->temporary_arena, "filter: expected 2 arguments (predicate, list)");
    }

    Value pred = car(args);
    Value lst  = car(cdr(args));

    if (!is_proper_list(lst)) {
        return make_error(context->temporary_arena, "filter: second argument must be a proper list");
    }

    Value head = NIL_VALUE;
    Value* tail = &head;

    Value current = lst;

    while (!is_nil(current)) {
        Value element_to_check = car(current);

        Value predicate_arg_list = make_cons(element_to_check, NIL_VALUE, context->temporary_arena);
        Value apply_args = make_cons(pred, make_cons(predicate_arg_list, NIL_VALUE, context->temporary_arena), context->temporary_arena);

        Value result = prim_apply(apply_args, env, context);
        if (result.type == TYPE_ERROR) {
            return result; 
        }

        if (is_truthy(result)) {
            Value new_cell = make_cons(element_to_check, NIL_VALUE, context->temporary_arena);
            *tail = new_cell;
            tail = &new_cell.as.cons->cdr;
        }

        current = cdr(current);
    }

    return head;
}

Value prim_reduce(Value args, Value env, Context* context) {
    size_t arg_count = list_length(args);
    if (arg_count < 2 || arg_count > 3) {
        return make_error(context->temporary_arena, "reduce: expected 2 or 3 arguments, but got %zu", arg_count);
    }

    Value func = car(args);
    if (func.type != TYPE_CLOSURE && func.type != TYPE_PRIMITIVE) {
        return make_error(context->temporary_arena, "reduce: first argument must be a function, but got a %s", type_name(func.type));
    }

    Value accumulator;
    Value list_to_process;

    if (arg_count == 3) {
        accumulator = car(cdr(args));
        list_to_process = car(cdr(cdr(args)));

        if (!is_proper_list(list_to_process)) {
            return make_error(context->temporary_arena, "reduce: third argument must be a proper list");
        }
    } else {
        list_to_process = car(cdr(args));
        if (!is_proper_list(list_to_process)) {
            return make_error(context->temporary_arena, "reduce: second argument must be a proper list");
        }
        if (is_nil(list_to_process)) {
            return make_error(context->temporary_arena, "reduce: cannot reduce an empty list without an initial value");
        }
        accumulator = car(list_to_process);
        list_to_process = cdr(list_to_process);
    }

    Value current_node = list_to_process;
    while (!is_nil(current_node)) {
        Value current_element = car(current_node);

        Value reducer_args = make_cons(accumulator, make_cons(current_element, NIL_VALUE, context->temporary_arena), context->temporary_arena);
        Value apply_args = make_cons(func, make_cons(reducer_args, NIL_VALUE, context->temporary_arena), context->temporary_arena);

        accumulator = prim_apply(apply_args, env, context);

        if (accumulator.type == TYPE_ERROR) {
            return accumulator;
        }

        current_node = cdr(current_node);
    }

    return accumulator;
}

Value prim_is_num(Value args, Value env, Context* context) {
    while (!is_nil(args)) {
        Value current_arg = car(args);
        if (current_arg.type != TYPE_NUMBER) {
            return NIL_VALUE;
        }
        args = cdr(args);
    }
    return TRUE_VALUE;
}

Value prim_add(Value args, Value env, Context* context) {
    double n = 0;
    while (!is_nil(args)) {
        Value arg = car(args);
        if (arg.type != TYPE_NUMBER) {
            return make_error(context->temporary_arena, "+: expected number, got %s", type_name(arg.type));
        }
        n += arg.as.number;
        args = cdr(args);
    }
    return make_number(n);
}

Value prim_sub(Value args, Value env, Context* context) {
    if (is_nil(args)) {
        return make_error(context->temporary_arena, "-: requires at least one argument");
    }
    Value first = car(args);
    if (first.type != TYPE_NUMBER) {
        return make_error(context->temporary_arena, "-: expected number, got %s", type_name(first.type));
    }
    double n = first.as.number;
    args = cdr(args);

    if (is_nil(args)) {
        return make_number(-n);
    }

    while (!is_nil(args)) {
        Value arg = car(args);
        if (arg.type != TYPE_NUMBER) {
            return make_error(context->temporary_arena, "-: expected number, got %s", type_name(arg.type));
        }
        n -= arg.as.number;
        args = cdr(args);
    }
    return make_number(n);
}

Value prim_mul(Value args, Value env, Context* context) {
    double n = 1;
    while (!is_nil(args)) {
        Value arg = car(args);
        if (arg.type != TYPE_NUMBER) {
            return make_error(context->temporary_arena, "*: expected number, got %s", type_name(arg.type));
        }
        n *= arg.as.number;
        args = cdr(args);
    }
    return make_number(n);
}

Value prim_div(Value args, Value env, Context* context) {
    if (is_nil(args)) {
        return make_error(context->temporary_arena, "/: requires at least one argument");
    }

    Value first_arg = car(args);

    if (first_arg.type != TYPE_NUMBER) {
        return make_error(context->temporary_arena, "/: expected number, got %s", type_name(first_arg.type));
    }

    double result = first_arg.as.number;
    args = cdr(args);

    if (is_nil(args)) {
        if (result == 0.0) {
            return make_error(context->temporary_arena, "/: division by zero (inverse of 0)");
        }
        return make_number(1.0 / result);
    }

    while (!is_nil(args)) {
        Value current_divisor = car(args);
        if (current_divisor.type != TYPE_NUMBER) {
            return make_error(context->temporary_arena, "/: expected number, got %s", type_name(current_divisor.type));
        }
        double divisor = current_divisor.as.number;
        if (divisor == 0.0) {
            return make_error(context->temporary_arena, "/: division by zero");
        }
        result /= divisor;
        args = cdr(args);
    }

    return make_number(result);
}

Value prim_modulus(Value args, Value env, Context* context) {
    if (list_length(args) != 2) {
        return make_error(context->temporary_arena, "%: expected 2 arguments, but got %zu", list_length(args));
    }

    Value arg1 = car(args);
    Value arg2 = car(cdr(args));

    if (arg1.type != TYPE_NUMBER || arg2.type != TYPE_NUMBER){
        return make_error(context->temporary_arena, "%: expected 2 numbers, but got a %s and a %s",
                          type_name(arg1.type), type_name(arg2.type));
    }

    double a = arg1.as.number;
    double b = arg2.as.number;

    if (b == 0.0) {
        return make_error(context->temporary_arena, "%: cannot divide by zero");
    }

    return make_number(fmod(a, b));
}

Value prim_int(Value args, Value env, Context* context) {
    return make_number((long long)car(args).as.number);
}

Value prim_lt(Value args, Value env, Context* context) {
    Value arg1 = car(args);
    Value arg2 = car(cdr(args));

    if (arg1.type != TYPE_NUMBER || arg2.type != TYPE_NUMBER) { 
        return make_error(context->temporary_arena, ">: expects numbers as arguments"); 
    }

    return (car(args).as.number < car(cdr(args)).as.number) ? TRUE_VALUE : NIL_VALUE;
}

Value prim_lte(Value args, Value env, Context* context) {
    Value arg1 = car(args);
    Value arg2 = car(cdr(args));

    if (arg1.type != TYPE_NUMBER || arg2.type != TYPE_NUMBER) { 
        return make_error(context->temporary_arena, "<=: expects numbers as arguments"); 
    }

    return (car(args).as.number <= car(cdr(args)).as.number) ? TRUE_VALUE : NIL_VALUE;
}

Value prim_gte(Value args, Value env, Context* context) {
    Value arg1 = car(args);
    Value arg2 = car(cdr(args));

    if (arg1.type != TYPE_NUMBER || arg2.type != TYPE_NUMBER) { 
        return make_error(context->temporary_arena, ">=: expects numbers as arguments"); 
    }

    return (car(args).as.number >= car(cdr(args)).as.number) ? TRUE_VALUE : NIL_VALUE;
}

Value prim_gt(Value args, Value env, Context* context) {
    Value arg1 = car(args);
    Value arg2 = car(cdr(args));

    if (arg1.type != TYPE_NUMBER || arg2.type != TYPE_NUMBER) { 
        return make_error(context->temporary_arena, ">: expects numbers as arguments"); 
    }

    return (arg1.as.number > arg2.as.number) ? TRUE_VALUE : NIL_VALUE;
}

Value prim_eq(Value args, Value env, Context* context) {
    return are_equal(car(args), car(cdr(args))) ? TRUE_VALUE : NIL_VALUE;
}

Value prim_is_pair(Value args, Value env, Context* context) {
    return car(args).type == TYPE_CONS ? TRUE_VALUE : NIL_VALUE;
}

Value prim_is_list(Value args, Value env, Context* context) {
    Value list_to_check = car(args);

    while (list_to_check.type == TYPE_CONS) {
        list_to_check = cdr(list_to_check);
    }

    return (list_to_check.type == TYPE_NIL) ? TRUE_VALUE : NIL_VALUE;
}

Value prim_not(Value args, Value env, Context* context) {
    return is_truthy(car(args)) ? NIL_VALUE : TRUE_VALUE;
}

Value prim_display(Value args, Value env, Context* context) {
    print_value(car(args));
    printf(" ");
    return NIL_VALUE;
}

Value prim_eq_num(Value args, Value env, Context* context) {
    return (car(args).as.number == car(cdr(args)).as.number) ? TRUE_VALUE : NIL_VALUE;
}

Value prim_tap(Value args, Value env, Context* context) {
    size_t arg_count = list_length(args);
    if (arg_count < 1) {
        return make_error(context->temporary_arena, "tap: requires at least 1 argument.");
    }

    Value value_to_tap = car(args);

    Value rest_args = cdr(args);
    if (!is_nil(rest_args)) {
        Value label = car(rest_args);
        if (label.type == TYPE_STRING || label.type == TYPE_ATOM) {
            print_value(label);
        }
    }

    print_value(value_to_tap);
    printf("\n");

    return value_to_tap;
}

Value prim_mem_stats(Value args, Value env, Context* context) {
    (void)args;
    (void)env;

    arena_print_stats(*context->permanent_arena, "Permanent");
    arena_print_stats(*context->temporary_arena, "Temporary");

    return NIL_VALUE;
}

Value prim_clear(Value args, Value env, Context* context) {
    printf("\033[2J\033[H");
    return NIL_VALUE;
}

Value prim_exit(Value args, Value env, Context* context) {
    exit(0);
}

/* ----------- Special Forms ----------- */

Value prim_apply(Value args, Value env, Context* context) {
    Value func = car(args);
    Value arg_list = car(cdr(args));
    if (func.type == TYPE_PRIMITIVE) {
        return primitives[func.as.primitive_index].func(arg_list, env, context);
    }
    if (func.type == TYPE_CLOSURE) {
        Value closure_env = func.as.closure->env;
        Value params = func.as.closure->params;
        Value body = func.as.closure->body;
        Value new_env = bind_args(params, arg_list, closure_env, context->temporary_arena);
        return eval_expression(body, new_env, context);
    }
    return make_error(context->temporary_arena, "apply: not a function");
}

Value prim_eval(Value args, Value env, Context* context) {
    return eval_expression(car(eval_list(args, env, context)),
                           env, context);
}

Value prim_quote(Value args, Value env, Context* context) {
    return car(args);
}

Value prim_backquote(Value args, Value env, Context* context) {
    Value template = car(args);

    if (template.type != TYPE_CONS) {
        return template;
    }

    if (car(template).type == TYPE_ATOM && strcmp(car(template).as.atom_name, "comma") == 0) {
        if (cdr(template).type == TYPE_CONS && is_nil(cdr(cdr(template)))) {
             return eval_expression(car(cdr(template)), env, context);
        } else {
             return make_error(context->temporary_arena, "comma: forma di unquote non valida");
        }
    }

    Value car_args = make_cons(car(template), NIL_VALUE, context->temporary_arena);
    Value expanded_car = prim_backquote(car_args, env, context);
    if (expanded_car.type == TYPE_ERROR) {
        return expanded_car;
    }

    Value cdr_args = make_cons(cdr(template), NIL_VALUE, context->temporary_arena);
    Value expanded_cdr = prim_backquote(cdr_args, env, context);
    if (expanded_cdr.type == TYPE_ERROR) {
        return expanded_cdr;
    }

    if (are_equal(expanded_car, car(template)) && are_equal(expanded_cdr, cdr(template))) {
        return template;
    }

    return make_cons(expanded_car, expanded_cdr, context->temporary_arena);
}

Value prim_or(Value args, Value env, Context* context) {
    Value result = NIL_VALUE;
    while (!is_nil(args)) {
        result = eval_expression(car(args), env, context);
        if (is_truthy(result)) return result;
        args = cdr(args);
    }
    return result;
}

Value prim_and(Value args, Value env, Context* context) {
    Value result = TRUE_VALUE;
    while (!is_nil(args)) {
        result = eval_expression(car(args), env, context);
        if (!is_truthy(result)) return result;
        args = cdr(args);
    }
    return result;
}

Value prim_cond(Value args, Value env, Context* context) {
    while(!is_nil(args)) {
        Value clause = car(args);
        if (is_truthy(eval_expression(car(clause), env, context))) {
            return eval_expression(car(cdr(clause)), env, context);
        }
        args = cdr(args);
    }
    return NIL_VALUE;
}

Value prim_if(Value args, Value env, Context* context) {
    Value cond = car(args);
    Value then_expr = car(cdr(args));
    Value else_expr = car(cdr(cdr(args)));

    Value branch_to_eval = is_truthy(eval_expression(cond, env, context)) ? then_expr : else_expr;

    return eval_expression(branch_to_eval, env, context);
}

Value prim_let_star(Value args, Value env, Context* context) {
    Value bindings = car(args);
    Value body = cdr(args);

    while (!is_nil(bindings)) {
        Value binding = car(bindings);
        Value var = car(binding);
        Value val_expr = car(cdr(binding));

        if (var.type == TYPE_ATOM && val_expr.type == TYPE_CONS && car(val_expr).type == TYPE_ATOM && strcmp(car(val_expr).as.atom_name, "lambda") == 0) {
            Value params = car(cdr(val_expr));
            Value body_lambda = car(cdr(cdr(val_expr)));

            Closure* closure_ptr = make_closure_ptr(params, body_lambda, env, context->temporary_arena);
            Value closure_val;
            closure_val.type = TYPE_CLOSURE;
            closure_val.as.closure = closure_ptr;

            Value recursive_env = make_env_pair(var, closure_val, env, context->temporary_arena);
            closure_ptr->env = recursive_env;
            env = recursive_env;

        } else {
            Value val = eval_expression(val_expr, env, context);
            env = make_env_pair(var, val, env, context->temporary_arena);
        }
        bindings = cdr(bindings);
    }

    Value last_val = NIL_VALUE;
    while (!is_nil(body)) {
        last_val = eval_expression(car(body), env, context);
        body = cdr(body);
    }
    return last_val;
}

Value prim_lambda(Value args, Value env, Context* context) {
    Value params = car(args);
    Value body = car(cdr(args));
    return make_closure(params, body, env, context->temporary_arena);
}

Value prim_macro(Value args, Value env, Context* context) {
    Value params = car(args);
    Value body = car(cdr(args));
    return make_macro(params, body, env, context->temporary_arena);
}

Value prim_define(Value args, Value env, Context* context) {
    Value var = car(args);
    Value val_expr = car(cdr(args));

    Value existing_frame = find_env_frame(var, global_env);

    if (!is_nil(existing_frame)) {
        Value current_val = cdr(existing_frame);

        if (current_val.type == TYPE_UNDEFINED) {
            Value new_val = eval_expression(val_expr, env, context);
            if (new_val.type == TYPE_ERROR) {
                return new_val;
            }

            Value perm_val = copy_value_to_arena(new_val, context->permanent_arena);
            existing_frame.as.cons->cdr = perm_val;  

            if (perm_val.type == TYPE_CLOSURE) {
                perm_val.as.closure->env = global_env;
            }

            return var;
        }

        return make_error(context->temporary_arena,
                         "define: '%s' already defined. Use set! to modify it",
                         var.type == TYPE_ATOM ? var.as.atom_name : "?");
    }

    Value new_val = eval_expression(val_expr, env, context);
    if (new_val.type == TYPE_ERROR) {
        return new_val;
    }

    Value perm_val = copy_value_to_arena(new_val, context->permanent_arena);
    Value perm_var = copy_value_to_arena(var, context->permanent_arena);
    global_env = make_env_pair(perm_var, perm_val, global_env, context->permanent_arena);

    if (perm_val.type == TYPE_CLOSURE) {
        perm_val.as.closure->env = global_env;
    }

    return var;
}

Value prim_set(Value args, Value env, Context* context) {
    Value var = car(args);
    Value val_expr = car(cdr(args));

    Value existing_frame = find_env_frame(var, global_env);
    if (is_nil(existing_frame)) {
        return make_error(context->temporary_arena, 
                         "set!: undefined variable: %s", 
                         var.type == TYPE_ATOM ? var.as.atom_name : "?");
    }

    Value new_val = eval_expression(val_expr, env, context);
    if (new_val.type == TYPE_ERROR) {
        return new_val;
    }

    Value perm_val = copy_value_to_arena(new_val, context->permanent_arena);
    existing_frame.as.cons->cdr = perm_val;

    if (perm_val.type == TYPE_CLOSURE) {
        perm_val.as.closure->env = global_env;
    }

    return var;
}

Value prim_undefine(Value args, Value env, Context* context) {
    Value var = car(args);

    if (var.type != TYPE_ATOM) {
        return make_error(context->temporary_arena, "undefine!: argument must be a symbol");
    }

    Value frame = find_env_frame(var, global_env);

    if (is_nil(frame)) {
        return make_error(context->temporary_arena,
                         "undefine!: variable '%s' not defined", 
                         var.as.atom_name);
    }

    Value undefined_val;
    undefined_val.type = TYPE_UNDEFINED;

    frame.as.cons->cdr = undefined_val;

    return var;
}

/* @{ // End of the group: core_primitives */

PrimitiveEntry primitives[] = {
    /* Special Form */
    {"quote",                 prim_quote,     1},
    {"backquote",             prim_backquote, 1},
    {"if",                    prim_if,        3},
    {"cond",                  prim_cond,      SIZE_MAX},
    {"and",                   prim_and,       SIZE_MAX},
    {"or",                    prim_or,        SIZE_MAX},
    {"lambda",                prim_lambda,    SIZE_MAX},
    {"macro",                 prim_macro,     SIZE_MAX},
    {"define",                prim_define,    2},
    {"set!",                  prim_set,       2},
   {"undefine!",             prim_undefine,  1},
   {"let*",                  prim_let_star,  2},

    /* Manipulate lists */
    {"cons",                 prim_cons,      2},
    {"list",                 prim_list,      SIZE_MAX},
    {"car",                  prim_car,       1},
    {"cdr",                  prim_cdr,       1},
    {"reverse",              prim_reverse,   1},
    {"len",                  prim_len,       1},

    /* Higher order functions */
    {"mapcar",               prim_mapcar,    2},
    {"filter",               prim_filter,    2},
    {"reduce",               prim_reduce,    SIZE_MAX},

    /* Arithmetic Ops */
    {"+",                    prim_add,       SIZE_MAX},
    {"-",                    prim_sub,       SIZE_MAX},
    {"*",                    prim_mul,       SIZE_MAX},
    {"/",                    prim_div,       SIZE_MAX},
    {"%",                    prim_modulus,   2},
    {"int",                  prim_int,       1},

    /* Predicates (?) */
    {"<",                    prim_lt,        2},
    {">",                    prim_gt,        2},
    {"<=",                   prim_lte,       2},
    {">=",                   prim_gte,       2},
    {"=",                    prim_eq_num,    2},
    {"eq?",                  prim_eq,        2},
    {"not",                  prim_not,       1},
    {"pair?",                prim_is_pair,   1},
    {"list?",                prim_is_list,   1},
    {"number?",              prim_is_num,    SIZE_MAX},

    /* Meta-Functions */
    {"apply",                prim_apply,     2},
    {"eval",                 prim_eval,      1},

    /* I/O */
    {"display",              prim_display,   1},

    /* Debugging Focused */
    {"tap",                  prim_tap,       2},
    {"print-memory-stats",   prim_mem_stats, 0},

    /* QOL functions */
    {"clear",                prim_clear,     0},
    {"exit",                 prim_exit,      0},

    /* End of list */
    {NULL,                   NULL,           0}
};


/**
 * @brief Checks if a primitive is a special form with unique evaluation rules.
 * This is a critical dispatch point for the evaluator, determining whether to
 * evaluate a function's arguments before calling it.
 *
 * @warning Any primitive that must control its own argument evaluation (like
 * 'if', 'lambda', or 'macro') MUST be added here to function correctly.
 */
static inline bool is_special_form(const char* name) {
    if (!name) return false;

    switch (name[0]) {
        case 'a': // and
            return strcmp(name, "and") == 0;

        case 'b': // backquote
            return strcmp(name, "backquote") == 0;

        case 'c': // cond
            return strcmp(name, "cond") == 0;

        case 'd': // define
            return strcmp(name, "define") == 0;

        case 'i': // if
            return strcmp(name, "if") == 0;

        case 'l': // lambda, let*
            switch (name[1]) {
                case 'a': // lambda
                    return strcmp(name, "lambda") == 0;
                case 'e': // let*
                    return strcmp(name, "let*") == 0;
                default:
                    return false;
            }

        case 'm': // macro
            return strcmp(name, "macro") == 0;

        case 'o': // or
            return strcmp(name, "or") == 0;

        case 'q': // quote
            return strcmp(name, "quote") == 0;

        case 's': // set!
            return strcmp(name, "set!") == 0;

        case 'u': // undefine!
            return strcmp(name, "undefine!") == 0;

        default:
            return false;
    }
}

Value eval_expression(Value expr, Value env, Context* context) {
    while (true) {
        switch (expr.type) {
            case TYPE_NIL:
            case TYPE_STRING:
            case TYPE_NUMBER:
                return expr;
            case TYPE_ATOM:
                return find_in_env(expr, env, context->temporary_arena);
            case TYPE_CONS: {
                Value func = eval_expression(car(expr), env, context);
                if (func.type == TYPE_ERROR) return func;
                Value args = cdr(expr);

                if (func.type == TYPE_MACRO) {
                    size_t expected = list_length(func.as.macro->params);
                    size_t actual = list_length(args);
                    if (expected != actual) {
                        return make_error(context->temporary_arena, "Arity error: macro expects %zu arguments, but got %zu", expected, actual);
                    }

                    Value macro_env = bind_args(func.as.macro->params, args, func.as.macro->env, context->temporary_arena);
                    Value expanded_code = eval_expression(func.as.macro->body, macro_env, context);
                    if (expanded_code.type == TYPE_ERROR) {
                        return expanded_code;
                    }
                    expr = expanded_code;
                    continue;
                }


                if (func.type == TYPE_PRIMITIVE) {
                    const char* name = primitives[func.as.primitive_index].name;
                    if (is_special_form(name)) {
                        return primitives[func.as.primitive_index].func(args, env, context);
                    }
                }

                Value evaluated_args = NIL_VALUE;
                {
                    Value* tail_ptr = &evaluated_args;
                    Value p = args;
                    while (!is_nil(p)) {
                        Value evaluated_car = eval_expression(car(p), env, context);
                        if (evaluated_car.type == TYPE_ERROR) return evaluated_car;

                        Value new_cell = make_cons(evaluated_car, NIL_VALUE, context->temporary_arena);
                        *tail_ptr = new_cell;
                        tail_ptr = &new_cell.as.cons->cdr;
                        p = cdr(p);
                    }
                }

                if (func.type == TYPE_CLOSURE) {
                    size_t expected = list_length(func.as.closure->params);
                    size_t actual = list_length(evaluated_args);
                    if (expected != actual) {
                        return make_error(context->temporary_arena, "Arity error: function expects %zu arguments, but got %zu", expected, actual);
                    }

                    env = bind_args(func.as.closure->params, evaluated_args, func.as.closure->env, context->temporary_arena);
                    expr = func.as.closure->body;
                    continue;
                }

                if (func.type == TYPE_PRIMITIVE) {
                    const PrimitiveEntry* prim_info = &primitives[func.as.primitive_index];
                    if (prim_info->arity != SIZE_MAX) {
                        size_t actual = list_length(evaluated_args);
                        if (actual != prim_info->arity) {
                            return make_error(context->temporary_arena, "Arity error for '%s': expects %zu arguments, but got %zu",
                                              prim_info->name, prim_info->arity, actual);
                        }
                    }

                    return prim_info->func(evaluated_args, env, context);
                }

                return make_error(context->temporary_arena, "Type error: cannot apply a non-function value.");
            }
            default:
                return make_error(context->temporary_arena, "Evaluation error: unknown value type.");
        }
    }
}

/* ------------------------ Parser --------------------- */

/**
 * @addtogroup parser Recursive Descent Parser
 * @brief Functions for parsing Lisp S-expressions from a text stream.
 *
 * This is a simple recursive descent parser that tokenizes the input
 * and builds `Value` objects representing the abstract syntax tree.
 * @{
 */

/// @brief Max size of the current_token_buf buffer.
#define MAX_TOKEN_SIZE 500
/// @brief Buffer to hold the currently scanned token string.
static char current_token_buf[MAX_TOKEN_SIZE];
/// @brief The next character to be processed from the input stream (lookahead).
static char lookahead_char = ' ';

/// @brief Reads a single character from the input stream into the lookahead buffer.
void read_char(FILE* stream) { lookahead_char = fgetc(stream); }
/// @brief Checks if the lookahead char matches `c`. A space matches any whitespace.
bool is_seeing(char c) { return c == ' ' ? (lookahead_char > 0 && lookahead_char <= c) : (lookahead_char == c); }
/// @brief Consumes and returns the current lookahead char, then advances the stream.
char get_char(FILE* stream) { char c = lookahead_char; read_char(stream); return c; }

/**
 * @brief Scans the input stream to form the next complete token.
 * @details Skips leading whitespace, then reads characters into `current_token_buf`
 *          until a delimiter is found. It handles strings, parentheses, and atoms.
 */
void scan_token(FILE* stream) {
    int i = 0;
    while (is_seeing(' ')) read_char(stream);
    if (lookahead_char == EOF) { current_token_buf[0] = '\0'; return; }

    if (lookahead_char == '"') {
        current_token_buf[i++] = get_char(stream);
        while (lookahead_char != '"' && lookahead_char != EOF) {
            if (lookahead_char == '\\') {
                current_token_buf[i++] = get_char(stream);
            }
            if (i < (sizeof(current_token_buf) - 1)) {
                current_token_buf[i++] = get_char(stream);
            } else { get_char(stream); }
        }
        if (lookahead_char == '"') {
            current_token_buf[i++] = get_char(stream);
        }
    } else if (is_seeing('(') || is_seeing(')') || is_seeing('\'')
               || is_seeing(',') || is_seeing('`')) {
        current_token_buf[i++] = get_char(stream);
    } else {
        do {
            current_token_buf[i++] = get_char(stream);
            if (i > (sizeof(current_token_buf) - 1)) {
                fprintf(stderr, "Lexer Error: '%s...' exceeds maximum length of %d.",
                        current_token_buf, MAX_TOKEN_SIZE);
                break;
            }
        } while (!is_seeing('(') && !is_seeing(')') && !is_seeing(' '));
    }
    current_token_buf[i] = '\0';
}

// Forward declaration for mutual recursion.
Value parse_from_current_token(FILE* stream, Arena** arena);

/// @brief Parses the string in `current_token_buf` into a number, string, or atom Value.
Value parse_atom(Arena** arena) {
    int len = strlen(current_token_buf);
    if (len >= 2 && current_token_buf[0] == '"' && current_token_buf[len - 1] == '"') {
        current_token_buf[len - 1] = '\0';
        return make_string(current_token_buf + 1, arena);
    }
    double num_val;
    int chars_read;
    if (sscanf(current_token_buf, "%lf%n", &num_val, &chars_read) > 0 && current_token_buf[chars_read] == '\0') {
        return make_number(num_val);
    }
    return make_atom(current_token_buf, arena);
}

/**
 * @brief Parses a list expression from the token stream.
 * @details Recursively calls `parse_from_current_token` for each element and
 *          handles dotted-pair notation (e.g., `(a . b)`).
 */
Value parse_list(FILE* stream, Arena** arena) {
    #define MAX_LIST_ELEMENTS 1024
    Value elements[MAX_LIST_ELEMENTS];
    size_t count = 0;
    bool is_dotted = false;
    Value dotted_tail = NIL_VALUE;

    while (true) {
        scan_token(stream);

        if (current_token_buf[0] == '\0') {
            fprintf(stderr, "Parser Error: unclosed list\n");
            return ERROR_VALUE;
        }

        if (current_token_buf[0] == ')') {
            break;
        }

        if (strcmp(current_token_buf, ".") == 0) {
            scan_token(stream);
            dotted_tail = parse_from_current_token(stream, arena);
            is_dotted = true;
            scan_token(stream);
            if (current_token_buf[0] != ')') {
                fprintf(stderr, "Parser Error: expected ')' after dot\n");
                return ERROR_VALUE;
            }
            break;
        }

        if (count >= MAX_LIST_ELEMENTS) {
            fprintf(stderr, "Parser Error: list exceeds maximum length of %d elements\n", 
                    MAX_LIST_ELEMENTS);
            return ERROR_VALUE;
        }

        elements[count++] = parse_from_current_token(stream, arena);
    }

    Value result = is_dotted ? dotted_tail : NIL_VALUE;
    for (int i = (int)count - 1; i >= 0; i--) {
        result = make_cons(elements[i], result, arena);
    }

    return result;
}

/**
 * @brief The main recursive parsing function.
 * @details Dispatches to the appropriate parsing function (`parse_list` or `parse_atom`)
 *          based on the token currently in the buffer. Handles `'` as a shorthand for `quote`.
 */
Value parse_from_current_token(FILE* stream, Arena** arena) {
    switch (current_token_buf[0]) {
        case '(':
            return parse_list(stream, arena);

        case '\'':
            scan_token(stream);
            return make_cons(make_atom("quote", arena),
                             make_cons(parse_from_current_token(stream, arena), NIL_VALUE, arena), arena);

        case ',':
            scan_token(stream);
            return make_cons(make_atom("comma", arena),
                             make_cons(parse_from_current_token(stream, arena), NIL_VALUE, arena), arena);

        case '`':
            scan_token(stream);
            return make_cons(make_atom("backquote", arena),
                             make_cons(parse_from_current_token(stream, arena), NIL_VALUE, arena), arena);

        case ')':
            fprintf(stderr, "Parser Error: unexpected ')'\n");
            return ERROR_VALUE;

        default:
            return parse_atom(arena);
    }
}

/// @brief Top-level function to parse a single complete S-expression from the stream.
Value parse_expression(FILE* stream, Arena** arena) {
    scan_token(stream);
    return parse_from_current_token(stream, arena);
}

/** @} */ // End of parser group


/**
 * @addtogroup printer Value Printer
 * @brief Functions for printing `Value` objects in a human-readable S-expression format.
 * @{
 */

/**
 * @brief Recursively prints the elements of a list.
 * @details Correctly handles the printing of proper and improper (dotted) lists.
 * @param list The list `Value` to print. Must be of type `TYPE_CONS`.
 */
void print_list(Value list) {
    putchar('(');
    while (true) {
        print_value(car(list));
        list = cdr(list);
        if (is_nil(list)) break; // End of a proper list
        if (list.type != TYPE_CONS) { // End of an improper list
            printf(" . ");
            print_value(list);
            break;
        }
        putchar(' ');
    }
    putchar(')');
}

/// @brief Prints a single `Value` object to standard output based on its type.
void print_value(Value v) {
    switch (v.type) {
        case TYPE_NIL: printf("()"); break;
        case TYPE_NUMBER: printf("%.10lg", v.as.number); break;
        case TYPE_STRING: printf("\"%s\"", v.as.string); break;
        case TYPE_ATOM: printf("%s", v.as.atom_name); break;
        case TYPE_PRIMITIVE: printf("<primitive:%s>", primitives[v.as.primitive_index].name); break;
        case TYPE_CONS: print_list(v); break;
        case TYPE_CLOSURE: printf("<closure>"); break;
        case TYPE_ERROR: printf("%s", v.as.err_msg); break;
        default: printf("<ERROR: unknown type>");
    }
}

/** @} // End of printer group */

// ---------------- Main Loop (REPL/file) ------------------


/* @brief The entry point of the program 
 * @note This is where the following resource are initialized:
 *          1. Arenas
 *          2. Runtime and global Context
 *          3. NIL, TRUE and ERROR values
 * */
int main(int argc, char* argv[]) {
    Arena* eval_arena = arena_init(0);
    Arena* global_arena = arena_init(0);

    Context global_context = { &global_arena, &eval_arena};

    NIL_VALUE.type = TYPE_NIL;
    TRUE_VALUE = make_atom("#t", &global_arena);
    ERROR_VALUE = make_atom("ERR", &global_arena);

    global_env = make_env_pair(TRUE_VALUE, TRUE_VALUE, NIL_VALUE, &global_arena);
    for (unsigned int i = 0; primitives[i].name != NULL; ++i) {
        Value name = make_atom(primitives[i].name, &global_arena);
        Value prim;
        prim.type = TYPE_PRIMITIVE;
        prim.as.primitive_index = i;
        global_env = make_env_pair(name, prim, global_env, &global_arena);
    }

    if (argc == 1) {
        // ========== REPL MODE (Read-Eval-Print Loop) ==========
        printf("ToyLisp");
        while (true) {
            printf("\n> ");
            Value expr = parse_expression(stdin, &eval_arena);
            if (current_token_buf[0] == '\0') {
                printf("\nGoodbye!\n");
                break;
            }
            Value result = eval_expression(expr, global_env, &global_context);
            print_value(result);
            arena_reset(eval_arena);
        }

    } else if (argc == 2) {
        // ========== FILE MODE ==========
        FILE* source_file = fopen(argv[1], "r");
        if (!source_file) {
            fprintf(stderr, "Error: Impossibile aprire il file '%s'\n", argv[1]);
            return 1;
        }

#ifdef _POSIX_VERSION
        struct timespec start_time;
        clock_gettime(CLOCK_MONOTONIC, &start_time);
#endif

        while (true) {
            Value expr = parse_expression(source_file, &eval_arena);

            if (current_token_buf[0] == '\0') {
                break;
            }
            if (are_equal(expr, ERROR_VALUE)) {
                fprintf(stderr, "Error: file '%s' could not be parsed\n", argv[1]);
                break;
            }
            Value result = eval_expression(expr, global_env, &global_context);

            print_value(result);
            printf("\n");
            arena_reset(eval_arena);
        }

#ifdef _POSIX_VERSION
        struct timespec end_time;
        clock_gettime(CLOCK_MONOTONIC, &end_time);

        double seconds_spent = (end_time.tv_sec - start_time.tv_sec) +
                               (end_time.tv_nsec - start_time.tv_nsec) / 1.0e9;

        printf("Execution time: %.9f seconds\n", seconds_spent);
#endif

        fclose(source_file);
    } else {
        // ========== WRONG USAGE ==========
        fprintf(stderr, "Usage: %s [filename]\n", argv[0]);
        return 1;
    }

    return 0;
}
