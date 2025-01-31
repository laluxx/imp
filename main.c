#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>

// TODO Tail call optimization.

// TODO Frame pointer omission: For simple functions,
// we could omit the frame pointer (rbp) entirely,
// saving two instructions per function.

typedef enum {
    TOKEN_IDENTIFIER,
    TOKEN_DOUBLE_COLON,
    TOKEN_PROC,
    TOKEN_LPAREN,
    TOKEN_RPAREN,
    TOKEN_LBRACE,
    TOKEN_RBRACE,
    TOKEN_EOF
} TokenType;

typedef struct {
    size_t row;    // Current line number (1-based)
    size_t col;    // Current column number (1-based)
    char* source;  // Pointer to the source string
    size_t point;  // Current position in source
} Cursor;

typedef struct {
    TokenType type;
    char* lexeme;  // The actual text of the token
    size_t row;    // Line number where token appears
    size_t col;    // Column number where token starts
} Token;

typedef struct Procedure {
    char* name;               // Name of the procedure
    struct Procedure** calls; // Array of procedures this procedure calls
    size_t num_calls;         // Number of procedure calls
    size_t calls_capacity;    // Capacity of calls array
} Procedure;

typedef struct {
    Procedure **array;
    size_t num;
    size_t capacity;
} Procedures;

typedef struct {
    Cursor cursor;
    Token current_token;
    Procedures procedures;
    FILE* output_file; // Assembly output file
} Compiler;

// Function prototypes
Cursor cursor_new(const char* source);
void cursor_advance(Cursor* cursor);
char cursor_peek(Cursor* cursor);
bool cursor_is_at_end(Cursor* cursor);
Token token_new(TokenType type, const char* lexeme, size_t row, size_t col);
Procedure* procedure_new(const char* name);
void procedure_add_call(Procedure* proc, Procedure* called_proc);
void procedure_free(Procedure* proc);
Compiler* compiler_new(const char* source);
void compiler_free(Compiler* c);
void lex(Compiler* c);
void parse(Compiler* c);
void generate_code(Compiler* c);
void error(Compiler* c, const char* message);

// Cursor functions
Cursor cursor_new(const char* source) {
    Cursor cursor = {
        .row = 1,
        .col = 1,
        .source = strdup(source),
        .point = 0
    };
    return cursor;
}

void cursor_advance(Cursor* cursor) {
    if (cursor->source[cursor->point] == '\n') {
        cursor->row++;
        cursor->col = 1;
    } else {
        cursor->col++;
    }
    cursor->point++;
}

char cursor_peek(Cursor* cursor) {
    return cursor->source[cursor->point];
}

bool cursor_is_at_end(Cursor* cursor) {
    return cursor->source[cursor->point] == '\0';
}

// Token functions
Token token_new(TokenType type, const char* lexeme, size_t row, size_t col) {
    Token token = {
        .type = type,
        .lexeme = strdup(lexeme),
        .row = row,
        .col = col
    };
    return token;
}

// Procedure functions
Procedure* procedure_new(const char* name) {
    Procedure* proc = malloc(sizeof(Procedure));
    proc->name = strdup(name);
    proc->calls = NULL;
    proc->num_calls = 0;
    proc->calls_capacity = 0;
    return proc;
}

void procedure_add_call(Procedure* proc, Procedure* called_proc) {
    if (proc->num_calls >= proc->calls_capacity) {
        proc->calls_capacity = proc->calls_capacity == 0 ? 2 : proc->calls_capacity * 2;
        proc->calls = realloc(proc->calls, proc->calls_capacity * sizeof(Procedure*));
    }
    proc->calls[proc->num_calls++] = called_proc;
}

void procedure_free(Procedure* proc) {
    free(proc->name);
    free(proc->calls);
    free(proc);
}

// Compiler functions
Compiler *compiler_new(const char *source) {
    Compiler *c = malloc(sizeof(Compiler));
    c->cursor = cursor_new(source);
    c->procedures.array = NULL;
    c->procedures.num = 0;
    c->procedures.capacity = 0;
    c->output_file = NULL;
    return c;
}

void compiler_free(Compiler *c) {
    free(c->cursor.source);
    for (size_t i = 0; i < c->procedures.num; i++) {
        procedure_free(c->procedures.array[i]);
    }
    free(c->procedures.array);
    if (c->output_file) {
        fclose(c->output_file);
    }
    free(c);
}

// Lexer
void lex(Compiler* c) {
    char lexeme[256];
    size_t lexeme_len = 0;

    while (!cursor_is_at_end(&c->cursor)) {
        size_t start_row = c->cursor.row;
        size_t start_col = c->cursor.col;
        lexeme_len = 0;

        // Skip whitespace
        while (isspace(cursor_peek(&c->cursor))) {
            cursor_advance(&c->cursor);
        }

        if (cursor_is_at_end(&c->cursor)) {
            c->current_token = token_new(TOKEN_EOF, "", start_row, start_col);
            return;
        }

        char ch = cursor_peek(&c->cursor);

        if (isalpha(ch) || ch == '_') {
            // Identifier
            while (isalnum(cursor_peek(&c->cursor)) || cursor_peek(&c->cursor) == '_') {
                lexeme[lexeme_len++] = cursor_peek(&c->cursor);
                cursor_advance(&c->cursor);
            }
            lexeme[lexeme_len] = '\0';

            if (strcmp(lexeme, "proc") == 0) {
                c->current_token = token_new(TOKEN_PROC, lexeme, start_row, start_col);
            } else {
                c->current_token = token_new(TOKEN_IDENTIFIER, lexeme, start_row, start_col);
            }
        } else if (ch == ':') {
            cursor_advance(&c->cursor);
            if (cursor_peek(&c->cursor) == ':') {
                cursor_advance(&c->cursor);
                c->current_token = token_new(TOKEN_DOUBLE_COLON, "::", start_row, start_col);
            } else {
                error(c, "Expected ':' after ':'");
            }
        } else if (ch == '(') {
            cursor_advance(&c->cursor);
            c->current_token = token_new(TOKEN_LPAREN, "(", start_row, start_col);
        } else if (ch == ')') {
            cursor_advance(&c->cursor);
            c->current_token = token_new(TOKEN_RPAREN, ")", start_row, start_col);
        } else if (ch == '{') {
            cursor_advance(&c->cursor);
            c->current_token = token_new(TOKEN_LBRACE, "{", start_row, start_col);
        } else if (ch == '}') {
            cursor_advance(&c->cursor);
            c->current_token = token_new(TOKEN_RBRACE, "}", start_row, start_col);
        } else {
            error(c, "Unexpected character");
        }

        return;
    }

    c->current_token = token_new(TOKEN_EOF, "", c->cursor.row, c->cursor.col);
}

// Parser
Procedure *find_procedure(Compiler *c, const char *name) {
    for (size_t i = 0; i < c->procedures.num; i++) {
        if (strcmp(c->procedures.array[i]->name, name) == 0) {
            return c->procedures.array[i];
        }
    }
    return NULL;
}

void parse_procedure(Compiler* c) {
    if (c->current_token.type != TOKEN_IDENTIFIER) {
        error(c, "Expected procedure name");
    }

    char* proc_name = strdup(c->current_token.lexeme);
    Procedure* proc = find_procedure(c, proc_name);

    if (!proc) {
        proc = procedure_new(proc_name);
        if (c->procedures.num >= c->procedures.capacity) {
            c->procedures.capacity =
                c->procedures.capacity == 0 ? 2 : c->procedures.capacity * 2;
            c->procedures.array = realloc(
                                          c->procedures.array, c->procedures.capacity * sizeof(Procedure *));
        }
        c->procedures.array[c->procedures.num++] = proc;
    }

    free(proc_name);

    lex(c); // Consume procedure name

    if (c->current_token.type != TOKEN_DOUBLE_COLON) {
        error(c, "Expected '::'");
    }
    lex(c); // Consume '::'

    if (c->current_token.type != TOKEN_PROC) {
        error(c, "Expected 'proc'");
    }
    lex(c); // Consume 'proc'

    if (c->current_token.type != TOKEN_LPAREN) {
        error(c, "Expected '('");
    }
    lex(c); // Consume '('

    if (c->current_token.type != TOKEN_RPAREN) {
        error(c, "Expected ')'");
    }
    lex(c); // Consume ')'

    if (c->current_token.type != TOKEN_LBRACE) {
        error(c, "Expected '{'");
    }
    lex(c); // Consume '{'

    // Clear existing calls for this procedure
    proc->num_calls = 0;

    while (c->current_token.type != TOKEN_RBRACE) {
        if (c->current_token.type != TOKEN_IDENTIFIER) {
            error(c, "Expected procedure call");
        }

        // Find or create the called procedure
        Procedure* called_proc = find_procedure(c, c->current_token.lexeme);
        if (!called_proc) {
            called_proc = procedure_new(c->current_token.lexeme);
            if (c->procedures.num >= c->procedures.capacity) {
                c->procedures.capacity =
                    c->procedures.capacity == 0 ? 2 : c->procedures.capacity * 2;
                c->procedures.array =
                    realloc(c->procedures.array,
                            c->procedures.capacity * sizeof(Procedure *));
            }
            c->procedures.array[c->procedures.num++] = called_proc;
        }

        procedure_add_call(proc, called_proc);

        lex(c); // Consume procedure name

        if (c->current_token.type != TOKEN_LPAREN) {
            error(c, "Expected '('");
        }
        lex(c); // Consume '('

        if (c->current_token.type != TOKEN_RPAREN) {
            error(c, "Expected ')'");
        }
        lex(c); // Consume ')'
    }

    lex(c); // Consume '}'
}

void parse(Compiler* c) {
    lex(c); // Get the first token

    while (c->current_token.type != TOKEN_EOF) {
        parse_procedure(c);
    }
}

// Code generator
void generate_code(Compiler* c) {
    c->output_file = fopen("output.asm", "w");
    if (!c->output_file) {
        error(c, "Could not create output file");
    }

    // Write assembly header
    fprintf(c->output_file, "global _start\n\n");
    fprintf(c->output_file, "section .text\n\n");

    // Generate code for each procedure
    for (size_t i = 0; i < c->procedures.num; i++) {
        Procedure* proc = c->procedures.array[i];
        fprintf(c->output_file, "%s:\n", proc->name);
        fprintf(c->output_file, "    push rbp\n");
        fprintf(c->output_file, "    mov rbp, rsp\n");

        // Generate calls
        for (size_t j = 0; j < proc->num_calls; j++) {
            fprintf(c->output_file, "    call %s\n", proc->calls[j]->name);
        }

        fprintf(c->output_file, "    mov rsp, rbp\n");
        fprintf(c->output_file, "    pop rbp\n");
        fprintf(c->output_file, "    ret\n\n");
    }

    // Write _start function
    fprintf(c->output_file, "_start:\n");
    fprintf(c->output_file, "    call main\n");
    fprintf(c->output_file, "    mov rax, 60\n");
    fprintf(c->output_file, "    xor rdi, rdi\n");
    fprintf(c->output_file, "    syscall\n");

    fclose(c->output_file);
    c->output_file = NULL;
}

void error(Compiler* c, const char* message) {
    fprintf(stderr, "Error at line %zu, column %zu: %s\n", c->cursor.row, c->cursor.col, message);
    exit(1);
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <source_file>\n", argv[0]);
        return 1;
    }

    // Read source file
    FILE* source_file = fopen(argv[1], "r");
    if (!source_file) {
        fprintf(stderr, "Error: Could not open file '%s'\n", argv[1]);
        return 1;
    }

    // Get file size
    fseek(source_file, 0, SEEK_END);
    size_t file_size = ftell(source_file);
    rewind(source_file);

    // Read file contents
    char* source = malloc(file_size + 1);
    fread(source, 1, file_size, source_file);
    source[file_size] = '\0';
    fclose(source_file);

    // Initialize compiler context
    Compiler* c = compiler_new(source);

    // Compile
    parse(c);
    generate_code(c);

    // Cleanup
    free(source);
    compiler_free(c);

    // Assemble and link
    system("nasm -f elf64 output.asm");
    system("ld -o a.out output.o");

    printf("Compilation successful. Executable 'a.out' created.\n");

    return 0;
}
