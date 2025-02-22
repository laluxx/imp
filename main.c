#include "theme.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>

// TODO Tail call optimization.
// TODO Scope of Scopes
// TODO --return-symbol-at POINT
// TODO Compilers should also be lsp's
// TODO Capture tests on the buffer or region

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
    size_t point;  // Current position in source
    size_t line;   // Current line in the buffer
} Cursor;

typedef struct {
    char *content;   // Text content
    size_t size;     // Current size of content
    size_t capacity; // Allocated capacity
    char *name;      // Buffer name
} Buffer;

typedef struct {
    size_t start;
    size_t end;
    Color bg;
    Color fg;
} Face;

typedef struct {
    TokenType type;
    char* lexeme;  // The actual text of the token
    size_t row;    // Line number where token appears
    size_t col;    // Column number where token starts
    size_t line;   // Line of the token
    Face face;
} Token;

typedef struct {
    Token *tokens;
    size_t count;
    size_t capacity;
} TokenHistory;

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
    Buffer buffer;
    Cursor cursor;
    Token current_token;
    Procedures procedures;
    FILE *output_file; // Assembly output file
    TokenHistory history;
} Compiler;

bool single_highlight_mode = true;

// Function prototypes
Cursor cursor_new(const char* source);
void cursor_advance(Cursor *cursor, Buffer *buffer);
char cursor_peek(Cursor *cursor, Buffer *buffer);
bool cursor_is_at_end(Cursor *cursor, Buffer *buffer);
Token token_new(TokenType type, const char *lexeme, size_t row, size_t col,
                size_t line, size_t start, size_t end);
Procedure* procedure_new(const char* name);
void procedure_add_call(Procedure* proc, Procedure* called_proc);
void procedure_free(Procedure* proc);
Compiler* compiler_new(const char* source);
void compiler_free(Compiler* c);
void lex(Compiler* c);
void parse(Compiler* c);
void generate_code(Compiler* c);
void error(Compiler* c, const char* message);

void token_history_init(TokenHistory *history);
void token_history_add(TokenHistory *history, Token token);
void token_history_free(TokenHistory *history);

void token_history_init(TokenHistory *history) {
    history->tokens = NULL;
    history->count = 0;
    history->capacity = 0;
}

void token_history_free(TokenHistory *history) {
    for (size_t i = 0; i < history->count; i++) {
        free(history->tokens[i].lexeme);
    }
    free(history->tokens);
    history->tokens = NULL;
    history->count = 0;
    history->capacity = 0;
}

void token_history_add(TokenHistory *history, Token token) {
    if (history->count >= history->capacity) {
        history->capacity = history->capacity == 0 ? 8 : history->capacity * 2;
        history->tokens =
            realloc(history->tokens, history->capacity * sizeof(Token));
    }
    history->tokens[history->count++] = token;
}

// Cursor functions
Cursor cursor_new(const char* source) {
    Cursor cursor = {
        .row = 1,
        .col = 1,
        .point = 0,
        .line = 0
    };
    return cursor;
}

void cursor_advance(Cursor *cursor, Buffer *buffer) {
    if (buffer->content[cursor->point] == '\n') {
        cursor->row++;
        cursor->col = 1;
        cursor->line++;
    } else {
        cursor->col++;
    }
    cursor->point++;
}

char cursor_peek(Cursor* cursor, Buffer *buffer) {
    return buffer->content[cursor->point];
}

bool cursor_is_at_end(Cursor* cursor, Buffer *buffer) {
    return buffer->content[cursor->point] == '\0';
}

// Token functions
Token token_new(TokenType type, const char *lexeme, size_t row, size_t col,
                size_t line, size_t start, size_t end) {
    Color fg;
    switch (type) {
    case TOKEN_IDENTIFIER:
        fg = CT.variable;
        break;
    case TOKEN_DOUBLE_COLON:
        fg = CT.function;
        break;
    case TOKEN_PROC:
        fg = CT.keyword;
        break;
    case TOKEN_LPAREN:
    case TOKEN_RPAREN:
        fg = CT.preprocessor;
        break;
    case TOKEN_LBRACE:
    case TOKEN_RBRACE:
        fg = CT.type;
        break;
    default:
        fg = CT.text;
    }

    Face face = {.start = start, .end = end, .bg = CT.bg, .fg = fg};

    Token token = {.type = type,
                   .lexeme = strdup(lexeme),
                   .row = row,
                   .col = col,
                   .line = line,
                   .face = face};
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
    c->buffer.content = strdup(source);
    c->buffer.size = strlen(source);
    c->buffer.capacity = c->buffer.size + 1;
    c->buffer.name = strdup("source");
    c->cursor.row = 1;
    c->cursor.col = 1;
    c->cursor.point = 0;
    c->cursor.line = 0;
    c->procedures.array = NULL;
    c->procedures.num = 0;
    c->procedures.capacity = 0;
    c->output_file = NULL;
    token_history_init(&c->history);
    return c;
}

void compiler_free(Compiler *c) {
    free(c->buffer.content);
    free(c->buffer.name);
    for (size_t i = 0; i < c->procedures.num; i++) {
        procedure_free(c->procedures.array[i]);
    }
    free(c->procedures.array);
    if (c->output_file) {
        fclose(c->output_file);
    }
    token_history_free(&c->history);
    free(c);
}

// Lexer
void lex(Compiler *c) {
    // Skip whitespace
    while (isspace(cursor_peek(&c->cursor, &c->buffer))) {
        cursor_advance(&c->cursor, &c->buffer);
    }

    // Check for end of file
    if (cursor_is_at_end(&c->cursor, &c->buffer)) {
        size_t end_pos = c->cursor.point;
        c->current_token = token_new(TOKEN_EOF, "", c->cursor.row, c->cursor.col,
                                     c->cursor.line, end_pos, end_pos);
        token_history_add(&c->history, c->current_token);
        return;
    }

    size_t start_row = c->cursor.row;
    size_t start_col = c->cursor.col;
    size_t start_line = c->cursor.line;
    size_t start_pos = c->cursor.point;
    char ch = cursor_peek(&c->cursor, &c->buffer);

    if (isalpha(ch) || ch == '_') {
        // Identifier or keyword
        char lexeme[256];
        size_t lexeme_len = 0;
        while (isalnum(cursor_peek(&c->cursor, &c->buffer)) ||
               cursor_peek(&c->cursor, &c->buffer) == '_') {
            lexeme[lexeme_len++] = cursor_peek(&c->cursor, &c->buffer);
            cursor_advance(&c->cursor, &c->buffer);
        }
        lexeme[lexeme_len] = '\0';
        size_t end_pos = c->cursor.point;

        if (strcmp(lexeme, "proc") == 0) {
            c->current_token = token_new(TOKEN_PROC, lexeme, start_row, start_col,
                                         start_line, start_pos, end_pos);
        } else {
            c->current_token = token_new(TOKEN_IDENTIFIER, lexeme, start_row,
                                         start_col, start_line, start_pos, end_pos);
        }
    } else if (ch == ':') {
        cursor_advance(&c->cursor, &c->buffer);
        if (cursor_peek(&c->cursor, &c->buffer) == ':') {
            cursor_advance(&c->cursor, &c->buffer);
            size_t end_pos = c->cursor.point;
            c->current_token = token_new(TOKEN_DOUBLE_COLON, "::", start_row,
                                         start_col, start_line, start_pos, end_pos);
        } else {
            error(c, "Expected ':' after ':'");
        }
    } else if (ch == '(') {
        cursor_advance(&c->cursor, &c->buffer);
        size_t end_pos = c->cursor.point;
        c->current_token = token_new(TOKEN_LPAREN, "(", start_row, start_col,
                                     start_line, start_pos, end_pos);
    } else if (ch == ')') {
        cursor_advance(&c->cursor, &c->buffer);
        size_t end_pos = c->cursor.point;
        c->current_token = token_new(TOKEN_RPAREN, ")", start_row, start_col,
                                     start_line, start_pos, end_pos);
    } else if (ch == '{') {
        cursor_advance(&c->cursor, &c->buffer);
        size_t end_pos = c->cursor.point;
        c->current_token = token_new(TOKEN_LBRACE, "{", start_row, start_col,
                                     start_line, start_pos, end_pos);
    } else if (ch == '}') {
        cursor_advance(&c->cursor, &c->buffer);
        size_t end_pos = c->cursor.point;
        c->current_token = token_new(TOKEN_RBRACE, "}", start_row, start_col,
                                     start_line, start_pos, end_pos);
    } else {
        error(c, "Unexpected character");
    }

    token_history_add(&c->history, c->current_token);
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

#include <lume.h>

int sw = 1920;
int sh = 1080;

void drawCompilerState(Font *font, Compiler *c, int step_count) {
    char state_info[256];
    const char *lexeme = "(null)";

    if (c->current_token.lexeme) {
        lexeme = c->current_token.lexeme;
    }

    snprintf(state_info, sizeof(state_info),
             "Step: %d, Token: %s, Line: %zu, Col: %zu", step_count, lexeme,
             c->cursor.row, c->cursor.col);

    drawText(font, state_info, 10, 40, CT.text);
}

void drawBuffer(Compiler *c, Font *font, float startX, float startY,
                float scrollX, float scrollY) {
    if (!c || !font || !c->buffer.content)
        return;

    const char *text = c->buffer.content;
    float x = startX - scrollX;
    float y = startY + scrollY;
    size_t charIndex = 0;
    size_t tokenIndex = 0;

    useShader("text");

    while (charIndex < c->buffer.size) {
        if (text[charIndex] == '\n') {
            x = startX - scrollX;
            y -= (font->ascent + font->descent);
            charIndex++;
            continue;
        }

        Color currentColor = CT.text; // Default color

        if (charIndex == c->cursor.point) {
            currentColor = CT.bg; // Highlight color for cursor position
        } else {
            if (single_highlight_mode) {
                // Only highlight the current token
                if (charIndex >= c->current_token.face.start &&
                    charIndex < c->current_token.face.end) {
                    currentColor = c->current_token.face.fg;
                }
            } else {
                // Highlight all tokens
                while (tokenIndex < c->history.count &&
                       charIndex >= c->history.tokens[tokenIndex].face.end) {
                    tokenIndex++;
                }

                if (tokenIndex < c->history.count &&
                    charIndex >= c->history.tokens[tokenIndex].face.start &&
                    charIndex < c->history.tokens[tokenIndex].face.end) {
                    currentColor = c->history.tokens[tokenIndex].face.fg;
                }
            }
        }

        drawChar(font, text[charIndex], x, y, 1.0, 1.0, currentColor);

        x += getCharacterWidth(font, text[charIndex]);
        charIndex++;
    }

    flush();
}

/* void drawBuffer(Compiler *c, Font *font, float startX, float startY, */
/*                 float scrollX, float scrollY) { */
/*     if (!c || !font || !c->buffer.content) */
/*         return; */

/*     const char *text = c->buffer.content; */
/*     float x = startX - scrollX; */
/*     float y = startY + scrollY; */
/*     size_t charIndex = 0; */
/*     size_t tokenIndex = 0; */

/*     useShader("text"); */

/*     while (charIndex < c->buffer.size) { */
/*         if (text[charIndex] == '\n') { */
/*             x = startX - scrollX; */
/*             y -= (font->ascent + font->descent); */
/*             charIndex++; */
/*             continue; */
/*         } */

/*         Color currentColor = CT.text; // Default color */

/*         if (single_highlight_mode) { */
/*             // Only highlight the current token */
/*             if (charIndex >= c->current_token.face.start && */
/*                 charIndex < c->current_token.face.end) { */
/*                 currentColor = c->current_token.face.fg; */
/*             } */
/*         } else { */
/*             // Highlight all tokens */
/*             while (tokenIndex < c->history.count && */
/*                    charIndex >= c->history.tokens[tokenIndex].face.end) { */
/*                 tokenIndex++; */
/*             } */

/*             if (tokenIndex < c->history.count && */
/*                 charIndex >= c->history.tokens[tokenIndex].face.start && */
/*                 charIndex < c->history.tokens[tokenIndex].face.end) { */
/*                 currentColor = c->history.tokens[tokenIndex].face.fg; */
/*             } */
/*         } */

/*         drawChar(font, text[charIndex], x, y, 1.0, 1.0, currentColor); */

/*         x += getCharacterWidth(font, text[charIndex]); */
/*         charIndex++; */
/*     } */

/*     flush(); */
/* } */

/* void drawBuffer(Compiler *c, Font *font, float startX, float startY, */
/*                 float scrollX, float scrollY) { */
/*     if (!c || !font || !c->buffer.content) */
/*         return; */

/*     const char *text = c->buffer.content; */
/*     float x = startX - scrollX; */
/*     float y = startY + scrollY; */
/*     size_t charIndex = 0; */
/*     size_t tokenIndex = 0; */

/*     useShader("text"); */

/*     while (charIndex < c->buffer.size) { */
/*         if (text[charIndex] == '\n') { */
/*             x = startX - scrollX; */
/*             y -= (font->ascent + font->descent); */
/*             charIndex++; */
/*             continue; */
/*         } */

/*         Color currentColor = CT.text; // Default color */

/*         // Find the current token */
/*         while (tokenIndex < c->history.count && */
/*                charIndex >= c->history.tokens[tokenIndex].face.end) { */
/*             tokenIndex++; */
/*         } */

/*         if (tokenIndex < c->history.count && */
/*             charIndex >= c->history.tokens[tokenIndex].face.start && */
/*             charIndex < c->history.tokens[tokenIndex].face.end) { */
/*             currentColor = c->history.tokens[tokenIndex].face.fg; */
/*         } */

/*         drawChar(font, text[charIndex], x, y, 1.0, 1.0, currentColor); */

/*         x += getCharacterWidth(font, text[charIndex]); */
/*         charIndex++; */
/*     } */

/*     flush(); */
/* } */

void drawCursor(Compiler *c, Font *font, float startX, float startY,
                float scrollX, float scrollY, Color cursorColor) {
    float cursorX = startX - scrollX;
    float cursorY = startY + scrollY;
    int lineCount = 0;

    for (size_t i = 0; i < c->cursor.point; i++) {
        if (c->buffer.content[i] == '\n') {
            lineCount++;
            cursorX = startX - scrollX;
        } else {
            cursorX += getCharacterWidth(font, c->buffer.content[i]);
        }
    }

    float cursorWidth =
        (c->cursor.point < c->buffer.size &&
         c->buffer.content[c->cursor.point] != '\n')
        ? getCharacterWidth(font, c->buffer.content[c->cursor.point])
        : getCharacterWidth(font, ' ');

    cursorY -= lineCount * (font->ascent + font->descent) + (font->descent * 2);

    Vec2f cursorPosition = {cursorX, cursorY};
    Vec2f cursorSize = {cursorWidth, font->ascent + font->descent};

    useShader("simple");
    drawRectangle(cursorPosition, cursorSize, cursorColor);
    flush();
}

int  fontsize    = 82;
char *fontPath   = "fan.otf";
bool should_step = false;
int  step_count  = 0;

void keyCallback(int key, int action, int mods) {
    bool shiftPressed = mods & GLFW_MOD_SHIFT;
    bool ctrlPressed = mods & GLFW_MOD_CONTROL;
    bool altPressed = mods & GLFW_MOD_ALT;
    if (action == GLFW_PRESS || action == GLFW_REPEAT) {
        switch (key) {
        case KEY_J:
        case KEY_N:
        case KEY_SPACE:
        case KEY_F:
            should_step = true;
            break;
          break;
        case KEY_MINUS:
            previousTheme();
          break;
        case KEY_H:
            single_highlight_mode = !single_highlight_mode;
          break;
        case KEY_EQUAL:
            nextTheme();
          break;
        }
    }
}

bool read_file(const char *filename, char **content, size_t *size) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        fprintf(stderr, "Error: Could not open file '%s'\n", filename);
        return false;
    }

    fseek(file, 0, SEEK_END);
    *size = ftell(file);
    rewind(file);

    *content = malloc(*size + 1);
    if (!*content) {
        fprintf(stderr, "Error: Memory allocation failed\n");
        fclose(file);
        return false;
    }

    size_t read_size = fread(*content, 1, *size, file);
    if (read_size != *size) {
        fprintf(stderr, "Error: Failed to read entire file\n");
        free(*content);
        fclose(file);
        return false;
    }

    (*content)[*size] = '\0';
    fclose(file);
    return true;
}

int main(int argc, char *argv[]) {
    bool step_mode = false;
    const char *source_file_name = NULL;
    initThemes();

    // Parse command line arguments
    if (argc == 3 &&
        (strcmp(argv[1], "-s") == 0 || strcmp(argv[1], "--step") == 0)) {
        step_mode = true;
        source_file_name = argv[2];
    } else if (argc == 2) {
        source_file_name = argv[1];
    } else {
        fprintf(stderr, "Usage: %s [-s|--step] <source_file>\n", argv[0]);
        return 1;
    }

    // Read source file
    char *source = NULL;
    size_t file_size = 0;
    if (!read_file(source_file_name, &source, &file_size)) {
        return 1;
    }

    // Initialize compiler
    Compiler *c = compiler_new(source);
    if (!c) {
        fprintf(stderr, "Error: Failed to initialize compiler\n");
        free(source);
        return 1;
    }

    if (step_mode) {
        // Initialize window and input
        initWindow(sw, sh, "imp - Lex Stepper");
        registerKeyCallback(keyCallback);

        sw = getScreenWidth();  // NOTE Currently get screen dimentions
        sh = getScreenHeight(); // only once at startup

        // Load font
        Font *font = loadFont(fontPath, fontsize, "fun");
        if (!font) {
            fprintf(stderr, "Failed to load font\n");
            compiler_free(c);
            free(source);
            closeWindow();
            return 1;
        }

        lex(c); // Initialize the first token

        while (!windowShouldClose()) {
            updateInput();

            beginDrawing();
            clearBackground(CT.bg);


            drawCursor(c, font, 0, sh - font->ascent + font->descent, 0, 0, CT.cursor);
            drawBuffer(c, font, 0, sh - font->ascent + font->descent, 0, 0);
            drawCompilerState(font, c, step_count);

            if (should_step) {
                if (c->current_token.type != TOKEN_EOF) {
                    lex(c);
                    step_count++;
                } else {
                    drawText(font, "Lexical analysis complete", 10, sh - 30, CT.region);
                }
                should_step = false;
            }

            endDrawing();
        }

        freeFont(font);
        closeWindow();
    } else {
        // Non-step mode: parse and generate code
        parse(c);
        generate_code(c);

        // Assemble and link
        int result = system("nasm -f elf64 output.asm && ld -o a.out output.o");
        if (result == 0) {
            printf("Compilation successful. Executable 'a.out' created.\n");
        } else {
            fprintf(stderr, "Error: Compilation failed\n");
        }
    }

    // Cleanup
    compiler_free(c);
    free(source);

    return 0;
}

