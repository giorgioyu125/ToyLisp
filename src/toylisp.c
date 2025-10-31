/* Copyright (C) 2025 Salvatore Bertino */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#define MAX_ERR_MSG_LEN 256

/* ------------------------------ Utility functions ------------------------- */

static inline void *xmalloc(size_t size){
    if (size <= 0){
        goto error;
    }
    void* result = malloc(size);
    if (!result) {
        goto error;
    }
    return result;
error:
    fprintf(stderr, "Failed Allocation: Invalide size");
    return NULL;
}

char* xstrdup(const char* s) {
    if (s == NULL) {
        return NULL;
    }
    size_t len = strlen(s) + 1;
    char* new_str = xmalloc(len);
    if (new_str == NULL) {
        return NULL; 
    }
    return (char*)memcpy(new_str, s, len);
}


/* ---------------------- Definitions an Data Structures -------------------- */

typedef enum {
    TYPE_NIL,
    TYPE_NUMBER,
    TYPE_ATOM,
    TYPE_PRIMITIVE,
    TYPE_CONS,
    TYPE_STRING,
    TYPE_ERROR,
    TYPE_CLOSURE,
} ValueType;

struct Value;
struct ConsCell;
struct Closure;

typedef struct Value {
    ValueType type;
    union {
        double number;
        const char* atom_name;
        const char* string;
        char* err_msg;
        unsigned int primitive_index;
        struct ConsCell* cons;
        struct Closure* closure;
    } as;
} Value;

typedef struct ConsCell {
    Value car;
    Value cdr;
} ConsCell;

typedef struct Closure {
    Value params;
    Value body;
    Value env;
} Closure;

typedef Value (*PrimitiveFunc)(Value args, Value env);
typedef struct {
    const char* name;
    PrimitiveFunc func;
    size_t arity;
} PrimitiveEntry;
extern PrimitiveEntry primitives[];

/* ----------------- Memory Managment ----------------- */

Value make_number(double n) {
    Value v;
    v.type = TYPE_NUMBER;
    v.as.number = n;
    return v;
}

Value make_atom(const char* name) {
    Value v;
    v.type = TYPE_ATOM;
    v.as.atom_name = xstrdup(name);
    return v;
}

Value make_cons(Value car, Value cdr) {
    ConsCell* cell = (ConsCell*)xmalloc(sizeof(ConsCell));
    cell->car = car;
    cell->cdr = cdr;

    Value v;
    v.type = TYPE_CONS;
    v.as.cons = cell;
    return v;
}

size_t list_length(Value list) {
    size_t count = 0;
    while (list.type == TYPE_CONS) {
        count++;
        list = list.as.cons->cdr;
    }
    return count;
}

Value make_closure(Value params, Value body, Value env) {
    Closure* cl = (Closure*)xmalloc(sizeof(Closure));
    cl->params = params;
    cl->body = body;
    cl->env = env;

    Value v;
    v.type = TYPE_CLOSURE;
    v.as.closure = cl;
    return v;
}

Closure* make_closure_ptr(Value params, Value body, Value env) {
    Closure* cl = (Closure*)xmalloc(sizeof(Closure));
    cl->params = params;
    cl->body = body;
    cl->env = env;
    return cl;
}

Value make_string(const char* s) {
    Value v;
    v.type = TYPE_STRING;
    v.as.string = xstrdup(s);
    return v;
}

Value make_error(const char *format, ...) {
    char buffer[MAX_ERR_MSG_LEN];

    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    char *msg = strdup(buffer);
    if (!msg) {
        fprintf(stderr, "FATAL: Out of memory\n");
        exit(EXIT_FAILURE);
    }

    Value err_val;
    err_val.type = TYPE_ERROR;
    err_val.as.err_msg = msg;

    return err_val;
}

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
        default: return "unknown";
    }
}

// ---------------- Variable and constants ------------------

Value NIL_VALUE;
Value TRUE_VALUE;
Value ERROR_VALUE;
Value global_env;

bool is_nil(Value v) {
    return v.type == TYPE_NIL;
}

bool is_truthy(Value v) {
    return v.type != TYPE_NIL;
}

bool are_equal(Value a, Value b) {
    if (a.type != b.type) return false;
    switch (a.type) {
        case TYPE_NIL:     return true;
        case TYPE_NUMBER:  return a.as.number == b.as.number;
        case TYPE_STRING:  return strcmp(a.as.string, b.as.string) == 0;
        case TYPE_ATOM:    return strcmp(a.as.atom_name, b.as.atom_name) == 0;
        case TYPE_CONS:    return a.as.cons == b.as.cons;
        case TYPE_CLOSURE: return a.as.closure == b.as.closure;
        case TYPE_PRIMITIVE: return a.as.primitive_index == b.as.primitive_index;
        case TYPE_ERROR: return strcmp(a.as.err_msg, b.as.err_msg) == 0;
    }
    return false;
}

Value car(Value p) {
    if (p.type != TYPE_CONS) return ERROR_VALUE;
    return p.as.cons->car;
}

Value cdr(Value p) {
    if (p.type != TYPE_CONS) return ERROR_VALUE;
    return p.as.cons->cdr;
}

Value make_env_pair(Value var, Value val, Value env) {
    return make_cons(make_cons(var, val), env);
}

Value find_in_env(Value var, Value env) {
    while (!is_nil(env)) {
        Value pair = car(env);
        if (are_equal(var, car(pair))) {
            return cdr(pair);
        }
        env = cdr(env);
    }
    return ERROR_VALUE;
}

Value bind_args(Value params, Value args, Value env) {
    if (is_nil(params)) return env;
    if (params.type == TYPE_CONS) {
        return bind_args(cdr(params), cdr(args), make_env_pair(car(params), car(args), env));
    }
    return make_env_pair(params, args, env);
}

/* ------------------- Interpreter: eval and apply ---------------- */

Value eval_expression(Value expr, Value env);
void print_value(Value v);

Value eval_list(Value list, Value env) {
    if (is_nil(list)) {
        return NIL_VALUE;
    }
    Value evaluated_car = eval_expression(car(list), env);
    Value evaluated_cdr = eval_list(cdr(list), env);
    return make_cons(evaluated_car, evaluated_cdr);
}

// Normal Bultin Functions

Value prim_cons(Value args, Value env) {
    return make_cons(car(args), car(cdr(args)));
}

Value prim_car(Value args, Value env) {
    return car(car(args));
}

Value prim_cdr(Value args, Value env) {
    return cdr(car(args));
}

Value prim_len(Value list, Value env) {
    size_t count = 0;
    while (list.type == TYPE_CONS) {
        count++;
        list = list.as.cons->cdr;
    }
    Value result = {.type = TYPE_NUMBER, .as.number = count};
    return result;
}

Value prim_is_num(Value args, Value env) {
    while (!is_nil(args)) {
        Value current_arg = car(args);
        if (current_arg.type != TYPE_NUMBER) {
            return NIL_VALUE;
        }
        args = cdr(args);
    }
    return TRUE_VALUE;
}

Value prim_add(Value args, Value env) {
    double n = 0;
    while (!is_nil(args)) {
        Value arg = car(args);
        if (arg.type != TYPE_NUMBER) {
            return make_error("+: expected number, got %s", type_name(arg.type));
        }
        n += arg.as.number;
        args = cdr(args);
    }
    return make_number(n);
}

Value prim_sub(Value args, Value env) {
    if (is_nil(args)) {
        return make_error("-: requires at least one argument");
    }
    Value first = car(args);
    if (first.type != TYPE_NUMBER) {
        return make_error("-: expected number, got %s", type_name(first.type));
    }
    double n = first.as.number;
    args = cdr(args);

    if (is_nil(args)) {
        return make_number(-n);
    }

    while (!is_nil(args)) {
        Value arg = car(args);
        if (arg.type != TYPE_NUMBER) {
            return make_error("-: expected number, got %s", type_name(arg.type));
        }
        n -= arg.as.number;
        args = cdr(args);
    }
    return make_number(n);
}

Value prim_mul(Value args, Value env) {
    double n = 1;
    while (!is_nil(args)) {
        Value arg = car(args);
        if (arg.type != TYPE_NUMBER) {
            return make_error("*: expected number, got %s", type_name(arg.type));
        }
        n *= arg.as.number;
        args = cdr(args);
    }
    return make_number(n);
}

Value prim_div(Value args, Value env) {
    if (is_nil(args)) {
        return make_error("/: requires at least one argument");
    }

    Value first_arg = car(args);

    if (first_arg.type != TYPE_NUMBER) {
        return make_error("/: expected number, got %s", type_name(first_arg.type));
    }

    double result = first_arg.as.number;
    args = cdr(args);

    if (is_nil(args)) {
        if (result == 0.0) {
            return make_error("/: division by zero (inverse of 0)");
        }
        return make_number(1.0 / result);
    }

    while (!is_nil(args)) {
        Value current_divisor = car(args);
        if (current_divisor.type != TYPE_NUMBER) {
            return make_error("/: expected number, got %s", type_name(current_divisor.type));
        }
        double divisor = current_divisor.as.number;
        if (divisor == 0.0) {
            return make_error("/: division by zero");
        }
        result /= divisor;
        args = cdr(args);
    }

    return make_number(result);
}

Value prim_int(Value args, Value env) {
    return make_number((long long)car(args).as.number);
}

Value prim_lt(Value args, Value env) {
    return (car(args).as.number < car(cdr(args)).as.number) ? TRUE_VALUE : NIL_VALUE;
}

Value prim_eq(Value args, Value env) {
    return are_equal(car(args), car(cdr(args))) ? TRUE_VALUE : NIL_VALUE;
}

Value prim_is_pair(Value args, Value env) {
    return car(args).type == TYPE_CONS ? TRUE_VALUE : NIL_VALUE;
}

Value prim_not(Value args, Value env) {
    return is_truthy(car(args)) ? NIL_VALUE : TRUE_VALUE;
}

Value prim_display(Value args, Value env) {
    print_value(car(args));
    printf(" "); 
    return NIL_VALUE;
}

Value prim_eq_num(Value args, Value env) {
    return (car(args).as.number == car(cdr(args)).as.number) ? TRUE_VALUE : NIL_VALUE;
}

Value prim_tap(Value args, Value env) {
    size_t arg_count = list_length(args);
    if (arg_count < 1) {
        return make_error("tap: requires at least 1 argument.");
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

/* ----------- Special Forms ----------- */

Value prim_apply(Value args, Value env) {
    Value func = car(args);
    Value arg_list = car(cdr(args));
    if (func.type == TYPE_PRIMITIVE) {
        return primitives[func.as.primitive_index].func(arg_list, env);
    }
    if (func.type == TYPE_CLOSURE) {
        Value closure_env = func.as.closure->env;
        Value params = func.as.closure->params;
        Value body = func.as.closure->body;
        Value new_env = bind_args(params, arg_list, closure_env);
        return eval_expression(body, new_env);
    }
    return make_error("apply: not a function");
}

Value prim_eval(Value args, Value env) {
    return eval_expression(car(eval_list(args, env)), env);
}

Value prim_quote(Value args, Value env) {
    return car(args);
}

Value prim_or(Value args, Value env) {
    Value result = NIL_VALUE;
    while (!is_nil(args)) {
        result = eval_expression(car(args), env);
        if (is_truthy(result)) return result;
        args = cdr(args);
    }
    return result;
}

Value prim_and(Value args, Value env) {
    Value result = TRUE_VALUE;
    while (!is_nil(args)) {
        result = eval_expression(car(args), env);
        if (!is_truthy(result)) return result;
        args = cdr(args);
    }
    return result;
}

Value prim_cond(Value args, Value env) {
    while(!is_nil(args)) {
        Value clause = car(args);
        if (is_truthy(eval_expression(car(clause), env))) {
            return eval_expression(car(cdr(clause)), env);
        }
        args = cdr(args);
    }
    return NIL_VALUE;
}

Value prim_if(Value args, Value env) {
    Value cond = car(args);
    Value then_expr = car(cdr(args));
    Value else_expr = car(cdr(cdr(args)));

    Value branch_to_eval = is_truthy(eval_expression(cond, env)) ? then_expr : else_expr;

    return eval_expression(branch_to_eval, env);
}

Value prim_let_star(Value args, Value env) {
    Value bindings = car(args);
    Value body = cdr(args);

    while (!is_nil(bindings)) {
        Value binding = car(bindings);
        Value var = car(binding);
        Value val_expr = car(cdr(binding));

        if (var.type == TYPE_ATOM && val_expr.type == TYPE_CONS && car(val_expr).type == TYPE_ATOM && strcmp(car(val_expr).as.atom_name, "lambda") == 0) {
            Value params = car(cdr(val_expr));
            Value body_lambda = car(cdr(cdr(val_expr)));

            Closure* closure_ptr = make_closure_ptr(params, body_lambda, env);
            Value closure_val;
            closure_val.type = TYPE_CLOSURE;
            closure_val.as.closure = closure_ptr;

            Value recursive_env = make_env_pair(var, closure_val, env);
            closure_ptr->env = recursive_env;
            env = recursive_env;

        } else {
            Value val = eval_expression(val_expr, env);
            env = make_env_pair(var, val, env);
        }
        bindings = cdr(bindings);
    }

    Value last_val = NIL_VALUE;
    while (!is_nil(body)) {
        last_val = eval_expression(car(body), env);
        body = cdr(body);
    }
    return last_val;
}

Value prim_lambda(Value args, Value env) {
    Value params = car(args);
    Value body = car(cdr(args));
    return make_closure(params, body, env);
}

Value prim_define(Value args, Value env) {
    Value var = car(args);
    Value val_expr = car(cdr(args));

    if (var.type == TYPE_ATOM && val_expr.type == TYPE_CONS && car(val_expr).type == TYPE_ATOM && strcmp(car(val_expr).as.atom_name, "lambda") == 0) {
        Value params = car(cdr(val_expr));
        Value body = car(cdr(cdr(val_expr)));

        Closure* closure_ptr = make_closure_ptr(params, body, NIL_VALUE);
        Value closure_val;
        closure_val.type = TYPE_CLOSURE;
        closure_val.as.closure = closure_ptr;

        Value recursive_env = make_env_pair(var, closure_val, env);
        closure_ptr->env = recursive_env;

        global_env = recursive_env;
        return var;
    } 
    else {
        Value val = eval_expression(val_expr, env);
        global_env = make_env_pair(var, val, global_env);
        return var;
    }
}

PrimitiveEntry primitives[] = {
    /* Special Form */
    {"quote",       prim_quote,     1},
    {"if",          prim_if,        3},
    {"cond",        prim_cond,      SIZE_MAX},
    {"and",         prim_and,       SIZE_MAX},
    {"or",          prim_or,        SIZE_MAX},
    {"lambda",      prim_lambda,    SIZE_MAX},
    {"define",      prim_define,    2},
    {"let*",        prim_let_star,  2},

    /* Manipulate lists */
    {"cons",        prim_cons,      2},
    {"car",         prim_car,       1},
    {"cdr",         prim_cdr,       1},
    {"len",         prim_len,       1},

    /* Arithmetic Ops */
    {"+",           prim_add,       SIZE_MAX},
    {"-",           prim_sub,       SIZE_MAX},
    {"*",           prim_mul,       SIZE_MAX},
    {"/",           prim_div,       SIZE_MAX},
    {"int",         prim_int,       1},

    /* Predicates (?) */
    {"<",           prim_lt,        2},
    {"=",           prim_eq_num,    2},
    {"eq?",         prim_eq,        2},
    {"not",         prim_not,       1},
    {"pair?",       prim_is_pair,   1},
    {"number?",     prim_is_num,    SIZE_MAX},

    /* Meta-Functions */
    {"apply",       prim_apply,     2},
    {"eval",        prim_eval,      1},

    /* I/O */
    {"display",     prim_display,   1},

    /* Debugging Focused */
    {"tap",         prim_tap,       2},

    /* QOL functions */
    // TODO:{"clear",       prim_clear,     0},
    // TODO:{"exit",        prim_exit,      0},

    /* End of list */
    {NULL,          NULL,           0}
};




static inline bool is_special_form(const char* name) {
    if (!name) return false;

    switch (name[0]) {
        case 'q':
            return name[1] == 'u' && strcmp(name, "quote") == 0;
        case 'i':
            return name[1] == 'f' && name[2] == '\0';
        case 'c':
            return name[1] == 'o' && strcmp(name, "cond") == 0;
        case 'o':
            return name[1] == 'r' && name[2] == '\0';
        case 'a':
            return name[1] == 'n' && name[2] == 'd' && name[3] == '\0';
        case 'd':
            return name[1] == 'e' && strcmp(name, "define") == 0;
        case 'l':
            switch (name[1]) {
                case 'a':
                    return strcmp(name, "lambda") == 0;
                case 'e':
                    return name[2] == 't' && name[3] == '*' && name[4] == '\0';
                default:
                    return false;
            }
        default:
            return false;
    }
}

Value eval_expression(Value expr, Value env) {
    while (true) {
        switch (expr.type) {
            case TYPE_NIL:
            case TYPE_STRING:
            case TYPE_NUMBER:
                return expr;
            case TYPE_ATOM:
                return find_in_env(expr, env);
            case TYPE_CONS: {
                Value func = eval_expression(car(expr), env);
                if (func.type == TYPE_ERROR) return func;
                Value args = cdr(expr);

                if (func.type == TYPE_PRIMITIVE) {
                    const char* name = primitives[func.as.primitive_index].name;
                    if (is_special_form(name)) {
                        return primitives[func.as.primitive_index].func(args, env);
                    }
                }

                Value evaluated_args = NIL_VALUE;
                {
                    Value* tail_ptr = &evaluated_args;
                    Value p = args;
                    while (!is_nil(p)) {
                        Value evaluated_car = eval_expression(car(p), env);
                        if (evaluated_car.type == TYPE_ERROR) return evaluated_car;

                        Value new_cell = make_cons(evaluated_car, NIL_VALUE);
                        *tail_ptr = new_cell;
                        tail_ptr = &new_cell.as.cons->cdr;
                        p = cdr(p);
                    }
                }


                if (func.type == TYPE_CLOSURE) {
                    size_t expected = list_length(func.as.closure->params);
                    size_t actual = list_length(evaluated_args);
                    if (expected != actual) {
                        return make_error("Arity error: function expects %zu arguments, but got %zu", expected, actual);
                    }

                    env = bind_args(func.as.closure->params, evaluated_args, func.as.closure->env);
                    expr = func.as.closure->body;
                    continue;
                }

                if (func.type == TYPE_PRIMITIVE) {
                    const PrimitiveEntry* prim_info = &primitives[func.as.primitive_index];
                    if (prim_info->arity != SIZE_MAX) {
                        size_t actual = list_length(evaluated_args);
                        if (actual != prim_info->arity) {
                            return make_error("Arity error for '%s': expects %zu arguments, but got %zu",
                                              prim_info->name, prim_info->arity, actual);
                        }
                    }

                    return prim_info->func(evaluated_args, env);
                }

                return make_error("Type error: cannot apply a non-function value.");
            }
            default:
                return make_error("Evaluation error: unknown value type.");
        }
    }
}

/* ---------------- Parser  --------------- */

static FILE* current_input_stream;
static char current_token_buf[400];
static char lookahead_char = ' ';

void read_char() { lookahead_char = fgetc(current_input_stream); }
bool is_seeing(char c) { return c == ' ' ? (lookahead_char > 0 && lookahead_char <= c) : (lookahead_char == c); }
char get_char() { char c = lookahead_char; read_char(); return c; }

void scan_token() {
    int i = 0;
    while (is_seeing(' ')) read_char();
    if (lookahead_char == EOF) { current_token_buf[0] = '\0'; return; }

    if (lookahead_char == '"') {
        current_token_buf[i++] = get_char();
        while (lookahead_char != '"' && lookahead_char != EOF) {
            if (lookahead_char == '\\') {
                current_token_buf[i++] = get_char();
            }
            if (i < (sizeof(current_token_buf) - 1)) {
                current_token_buf[i++] = get_char();
            } else { get_char(); }
        }
        if (lookahead_char == '"') {
            current_token_buf[i++] = get_char();
        }
    } else if (is_seeing('(') || is_seeing(')') || is_seeing('\'')) {
        current_token_buf[i++] = get_char();
    } else {
        do {
            current_token_buf[i++] = get_char();
        } while (i < (sizeof(current_token_buf) - 1) && !is_seeing('(') && !is_seeing(')') && !is_seeing(' '));
    }
    current_token_buf[i] = '\0';
}

Value parse_from_current_token();

Value parse_atom() {
    int len = strlen(current_token_buf);
    if (len >= 2 && current_token_buf[0] == '"' && current_token_buf[len - 1] == '"') {
        current_token_buf[len - 1] = '\0';
        return make_string(current_token_buf + 1);
    }
    double num_val;
    int chars_read;
    if (sscanf(current_token_buf, "%lf%n", &num_val, &chars_read) > 0 && current_token_buf[chars_read] == '\0') {
        return make_number(num_val);
    }

    return make_atom(current_token_buf);
}

Value parse_list() {
    Value head = NIL_VALUE;
    Value* p_link = &head;

    while (true) {
        scan_token();

        if (current_token_buf[0] == '\0') { // Fine dell'input
            fprintf(stderr, "Parser Error: unclosed list\n");
            return ERROR_VALUE;
        }
        if (current_token_buf[0] == ')') {
            break;
        }

        if (strcmp(current_token_buf, ".") == 0) {
            scan_token();
            *p_link = parse_from_current_token();
            scan_token();
            if (current_token_buf[0] != ')') {
                 fprintf(stderr, "Parser Error: expected ')' after dot\n");
                 return ERROR_VALUE;
            }
            break;
        }

        *p_link = make_cons(parse_from_current_token(), NIL_VALUE);
        p_link = &(*p_link).as.cons->cdr;
    }
    return head;
}

Value parse_from_current_token() {
    if (current_token_buf[0] == '(') {
        return parse_list();
    }
    if (current_token_buf[0] == '\'') {
        scan_token(); 
        return make_cons(make_atom("quote"), make_cons(parse_from_current_token(), NIL_VALUE));
    }
    if (current_token_buf[0] == ')') {
        fprintf(stderr, "Parser Error: unexpected ')'\n");
        return ERROR_VALUE;
    }
    return parse_atom();
}

Value parse_expression() {
    scan_token();
    return parse_from_current_token();
}

/* ---------------- Print Value  ----------------- */

void print_list(Value list) {
    putchar('(');
    while (true) {
        print_value(car(list));
        list = cdr(list);
        if (is_nil(list)) break;
        if (list.type != TYPE_CONS) {
            printf(" . ");
            print_value(list);
            break;
        }
        putchar(' ');
    }
    putchar(')');
}

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

// ---------------- Main Loop (REPL/file) ------------------

int main(int argc, char* argv[]) {
    printf("ToyLisp");

    NIL_VALUE.type = TYPE_NIL;
    TRUE_VALUE = make_atom("#t");
    ERROR_VALUE = make_atom("ERR");

    global_env = make_env_pair(TRUE_VALUE, TRUE_VALUE, NIL_VALUE);
    for (unsigned int i = 0; primitives[i].name != NULL; ++i) {
        Value name = make_atom(primitives[i].name);
        Value prim;
        prim.type = TYPE_PRIMITIVE;
        prim.as.primitive_index = i;
        global_env = make_env_pair(name, prim, global_env);
    }

    if (argc == 1) {
        // ========== REPL MODE (Read-Eval-Print Loop) ==========
        current_input_stream = stdin;

        while (true) {
            printf("\n> ");
            Value expr = parse_expression();
            if (current_token_buf[0] == '\0') {
                printf("\nGoodbye!\n");
                break;
            }
            Value result = eval_expression(expr, global_env);
            print_value(result);
        }

    } else if (argc == 2) {
        // ========== FILE MODE ==========
        FILE* source_file = fopen(argv[1], "r");
        if (!source_file) {
            fprintf(stderr, "Error: Impossibile aprire il file '%s'\n", argv[1]);
            return 1;
        }

        current_input_stream = source_file;

        while (true) {
            Value expr = parse_expression();

            if (current_token_buf[0] == '\0') {
                break;
            }
            if (are_equal(expr, ERROR_VALUE)) {
                fprintf(stderr, "Error: file '%s' could not be parsed\n", argv[1]);
                break;
            }
            Value result = eval_expression(expr, global_env);

            print_value(result);
            printf("\n");
        }

        fclose(source_file);
    } else {
        // ========== WRONG USAGE ==========
        fprintf(stderr, "Usage: %s [filename]\n", argv[0]);
        return 1;
    }

    return 0;
}
