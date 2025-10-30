/* Copyright (C) 2025 Salvatore Bertino */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

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

Value prim_add(Value args, Value env) {
    double n = car(args).as.number;
    while (is_truthy(args = cdr(args))) {
        n += car(args).as.number;
    }
    return make_number(n);
}

Value prim_sub(Value args, Value env) {
    double n = car(args).as.number;
    while (is_truthy(args = cdr(args))) {
        n -= car(args).as.number;
    }
    return make_number(n);
}

Value prim_mul(Value args, Value env) {
    double n = car(args).as.number;
    while (is_truthy(args = cdr(args))) {
        n *= car(args).as.number;
    }
    return make_number(n);
}

Value prim_div(Value args, Value env) {
    double n = car(args).as.number;
    while (is_truthy(args = cdr(args))) {
        n /= car(args).as.number;
    }
    return make_number(n);
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

/* ----------- Special Forms ----------- */

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


typedef Value (*PrimitiveFunc)(Value args, Value env);
struct { const char* name; PrimitiveFunc func; } primitives[] = {
    /* Special Form */
    {"quote",       prim_quote},
    {"if",          prim_if},
    {"cond",        prim_cond},
    {"and",         prim_and},
    {"or",          prim_or},
    {"lambda",      prim_lambda},
    {"define",      prim_define},
    {"let*",        prim_let_star},

    /* Manipulate lists */
    {"cons",        prim_cons},
    {"car",         prim_car},
    {"cdr",         prim_cdr},

    /* Arithmetic and Numbers */
    {"+",           prim_add},
    {"-",           prim_sub},
    {"*",           prim_mul},
    {"/",           prim_div},
    {"int",         prim_int},

    /* Predicates */
    {"<",           prim_lt},
    {"=",           prim_eq_num},
    {"eq?",         prim_eq},
    {"not",         prim_not},
    {"pair?",       prim_is_pair},

    /* Meta-functions */
    {"eval",        prim_eval},

    /* I/O */
    {"display",     prim_display},

    {NULL,          NULL}
};

Value bind_args(Value params, Value args, Value env) {
    if (is_nil(params)) return env;
    if (params.type == TYPE_CONS) {
        return bind_args(cdr(params), cdr(args), make_env_pair(car(params), car(args), env));
    }
    return make_env_pair(params, args, env);
}

Value apply(Value func, Value args, Value env) {
    if (func.type == TYPE_PRIMITIVE) {
        return primitives[func.as.primitive_index].func(args, env);
    }
    if (func.type == TYPE_CLOSURE) {
        Value closure_env = func.as.closure->env;
        Value params = func.as.closure->params;
        Value body = func.as.closure->body;
        Value evaluated_args = eval_list(args, env);
        Value new_env = bind_args(params, evaluated_args, closure_env);
        return eval_expression(body, new_env);
    }
    return ERROR_VALUE;
}


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
                        Value new_cell = make_cons(eval_expression(car(p), env), NIL_VALUE);
                        *tail_ptr = new_cell;
                        tail_ptr = &new_cell.as.cons->cdr;
                        p = cdr(p);
                    }
                }


                if (func.type == TYPE_CLOSURE) {
                    env = bind_args(func.as.closure->params, evaluated_args, func.as.closure->env);
                    expr = func.as.closure->body;
                    continue;
                }

                if (func.type == TYPE_PRIMITIVE) {
                    return primitives[func.as.primitive_index].func(evaluated_args, env);
                }

                return ERROR_VALUE;
            }
            default:
                return ERROR_VALUE;
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
