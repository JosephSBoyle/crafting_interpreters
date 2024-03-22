#include <stdio.h>
#include <stdlib.h>

#include "common.h"
#include "compiler.h"
#include "scanner.h"

#ifdef DEBUG_PRINT_CODE
#include "debug.h"
#endif

typedef struct {
    Token current;
    Token previous;
    bool  hadError;
    bool  panicMode;
} Parser;


/* Operator precedences, from highest to lowest.

C implicitly gives successively larger numbers for enums.
Therefore, PREC_NONE < PREC_ASSIGNMENT etc.

We need this for handling expressions that need to be evaluated in
specific orders. For instance:

    -x.y * z

Needs to be evaluated as it would be spoken:
    "the negative of x's y attribute, multiplied by z."

A naive evaluator of this expression would evaluate in the wrong
order.
*/
typedef enum {
    PREC_NONE,
    PREC_ASSIGNMENT, // =
    PREC_OR,         // or
    PREC_AND,        // and
    PREC_EQUALITY,   // == !=
    PREC_COMPARISON, // < > <= >=
    PREC_TERM,       // + -
    PREC_FACTOR,     // * /
    PREC_UNARY,      // ! -
    PREC_CALL,       // . ()
    PREC_PRIMARY,
} Precedence;

// Type of a function which takes no args, and returns void.
typedef void (*ParseFn)();

/* Row in parser table containing:
    1. The function to compile a prefix expression beginning with a
        token of that type.
    2. The function to compile an infix expression whose left operand
        is followed by a token of that type
    3. The precedence of an infix expression that uses that token as
        an operator.
*/
typedef struct {
    ParseFn    prefix;
    ParseFn    infix;
    Precedence precedence;
} ParseRule;

/* Singleton parser instance */
Parser parser;

Chunk* compilingChunk;

static Chunk* currentChunk() {
    return compilingChunk;
}

static void errorAt(Token* token, const char* message) {
    // Panic after the first error: don't throw errors
    // after the first.
    if (parser.panicMode) return;
    parser.panicMode = true;

    fprintf(stderr, "[line %d] Error", token->line);

    if (token->type == TOKEN_EOF) {
        fprintf(stderr, " at end");
    } else if (token->type == TOKEN_ERROR) {
        // Nothing.
    } else {
        fprintf(stderr, " at '%.*s'", token->length, token->start);
    }

    fprintf(stderr, ": %s\n", message);
    parser.hadError = true;
}

static void error(const char* message) {
    errorAt(&parser.previous, message);
}

static void errorAtCurrent(const char* message) {
    errorAt(&parser.current, message);
}

static void advance() {
    parser.previous = parser.current;

    for (;;) {
        // Consume any TOKEN_ERROR's
        parser.current = scanToken();
        if (parser.current.type != TOKEN_ERROR) break;
        
        errorAtCurrent(parser.current.start);
    }
}

/* Consume the next token and validate it's type is as expected */
static void consume(TokenType type, const char* message) {
    if (parser.current.type == type) {
        advance();
        return;
    }
    errorAtCurrent(message);
}

static bool check(TokenType type) {
    return parser.current.type == type;
}

static bool match(TokenType type) {
    if (!check(type)) return false;
    advance();
    return true;
}

/* Write a sinlge bytecode */
static void emitByte(uint8_t byte) {
    writeChunk(currentChunk(), byte, parser.previous.line);
}

/* Utility function equivalent to to calling emitByte twice */
static void emitBytes(uint8_t byte1, uint8_t byte2) {
    emitByte(byte1);
    emitByte(byte2);
}

static void emitReturn() {
    emitByte(OP_RETURN);
}

/* Make a constant, first checking that we haven't defined
   too many constants! */
static uint8_t makeConstant(Value value) {
    int constant = addConstant(currentChunk(), value);
    if (constant > UINT8_MAX) {
        error("Too many constants in one chunk.");
        return 0;
    }

    return (uint8_t)constant;
}

static void emitConstant(Value value) {
    emitBytes(OP_CONSTANT, makeConstant(value));
}

static void endCompiler() {
    emitReturn();

#ifdef DEBUG_PRINT_CODE
    if (!parser.hadError) {
        disassembleChunk(currentChunk(), "code");
    }
#endif
}

// Forward declarations for handling declaration cycle.
static void       expression();
static void       statement();
static void       declaration();
static ParseRule* getRule(TokenType type);
static void       parsePrecedence(Precedence precedence);

static void binary() {
    // Binary operators are left-assosciative for the same operator:
    //    1 + 2 + 3
    // should be parsed as
    //    (1 + 2) + 3
    //
    // For different operators, e.g:
    //    2 * 3 + 4
    // The RHS operand of '*' should only capture '3', not '3+4'.
    // This is because '+' has a lower precedence than '*', and
    // thus should be evaluated later.

    // Remember the operator.
    TokenType operatorType = parser.previous.type;

    // Compile the RHS operand.
    ParseRule* rule = getRule(operatorType);
    parsePrecedence((Precedence)(rule->precedence + 1));

    // Emit the operator instruction.
    switch (operatorType)
    {
        case TOKEN_BANG_EQUAL:      emitBytes(OP_EQUAL, OP_NOT); break;
        case TOKEN_EQUAL_EQUAL:     emitByte(OP_EQUAL); break;
        case TOKEN_GREATER:         emitByte(OP_GREATER); break;
        case TOKEN_GREATER_EQUAL:   emitBytes(OP_LESS, OP_NOT); break;
        case TOKEN_LESS:            emitByte(OP_LESS); break;
        case TOKEN_LESS_EQUAL:      emitBytes(OP_GREATER, OP_NOT); break;
        case TOKEN_PLUS:    emitByte(OP_ADD);       break;
        case TOKEN_MINUS:   emitByte(OP_SUBTRACT);  break;
        case TOKEN_STAR:    emitByte(OP_MULTIPLY);  break;
        case TOKEN_SLASH:   emitByte(OP_DIVIDE);    break;
        default:
            return; // Unreachable.
    }
}

static void literal() {
    switch (parser.previous.type) {
        case TOKEN_FALSE: emitByte(OP_FALSE); break;
        case TOKEN_NIL:   emitByte(OP_NIL);   break;
        case TOKEN_TRUE:  emitByte(OP_TRUE);  break;
        default: return; // Unreachable.
    }
}
/* Handle parenthetic expressions such as ((1 + 2) * 3) */
static void grouping() {

    // Recursively called
    expression();

    consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
}

/* Write a parsed number literal as a const */
static void number() {
    // Assume that the number literal has been consumed
    // and stored in `parser.previous`.
    double value = strtod(parser.previous.start, NULL);
    emitConstant(NUMBER_VAL(value));
}

static void string() {
    // +1 because string starts after the first quotation mark.
    // -2 because the length of the string doesn't count the quotes. 
    emitConstant(OBJ_VAL(copyString(parser.previous.start  + 1,
                                    parser.previous.length - 2)));
}

/* Dispatch a unary operator to the appropriate byte emitter */ 
static void unary() {
    TokenType operatorType = parser.previous.type;

    // Recursively called
    expression(PREC_UNARY);

    // Emit the operator's instruction
    switch (operatorType) {
        case TOKEN_MINUS: emitByte(OP_NEGATE); break;
        case TOKEN_BANG : emitByte(OP_NOT); break;
        default:
            return; // Unreachable.
    }
}

/* Table of rules for each type of token.
   Each line is a `ParseRule`, mapping a token to functions for
   handling that token as a unary and/or binary operator where
   applicable, as well as the precedence of that operator token.
*/
ParseRule rules[] = {
    [TOKEN_LEFT_PAREN]    = {grouping,  NULL,   PREC_NONE},
    [TOKEN_RIGHT_PAREN]   = {NULL,      NULL,   PREC_NONE},
    [TOKEN_LEFT_BRACE]    = {NULL,      NULL,   PREC_NONE},
    [TOKEN_RIGHT_BRACE]   = {NULL,      NULL,   PREC_NONE},
    [TOKEN_COMMA]         = {NULL,      NULL,   PREC_NONE},
    [TOKEN_DOT]           = {NULL,      NULL,   PREC_NONE},
    [TOKEN_MINUS]         = {unary,     binary, PREC_TERM},
    [TOKEN_PLUS]          = {NULL,      binary, PREC_TERM},
    [TOKEN_SEMICOLON]     = {NULL,      NULL,   PREC_NONE},
    [TOKEN_SLASH]         = {NULL,      binary, PREC_FACTOR},
    [TOKEN_STAR]          = {NULL,      binary, PREC_FACTOR},
    [TOKEN_BANG]          = {unary,     NULL,   PREC_NONE},
    [TOKEN_BANG_EQUAL]    = {NULL,      binary, PREC_EQUALITY},
    [TOKEN_EQUAL]         = {NULL,      NULL,   PREC_NONE},
    [TOKEN_EQUAL_EQUAL]   = {NULL,      binary, PREC_EQUALITY},
    [TOKEN_GREATER]       = {NULL,      binary, PREC_COMPARISON},
    [TOKEN_GREATER_EQUAL] = {NULL,      binary, PREC_COMPARISON},
    [TOKEN_LESS]          = {NULL,      binary, PREC_COMPARISON},
    [TOKEN_LESS_EQUAL]    = {NULL,      binary, PREC_COMPARISON},
    [TOKEN_IDENTIFIER]    = {NULL,      NULL,   PREC_NONE},
    [TOKEN_STRING]        = {string,    NULL,   PREC_NONE},
    [TOKEN_NUMBER]        = {number,    NULL,   PREC_NONE},
    [TOKEN_AND]           = {NULL,      NULL,   PREC_NONE},
    [TOKEN_CLASS]         = {NULL,      NULL,   PREC_NONE},
    [TOKEN_ELSE]          = {NULL,      NULL,   PREC_NONE},
    [TOKEN_FALSE]         = {literal,   NULL,   PREC_NONE},
    [TOKEN_FOR]           = {NULL,      NULL,   PREC_NONE},
    [TOKEN_FUN]           = {NULL,      NULL,   PREC_NONE},
    [TOKEN_IF]            = {NULL,      NULL,   PREC_NONE},
    [TOKEN_NIL]           = {literal,   NULL,   PREC_NONE},
    [TOKEN_OR]            = {NULL,      NULL,   PREC_NONE},
    [TOKEN_PRINT]         = {NULL,      NULL,   PREC_NONE},
    [TOKEN_RETURN]        = {NULL,      NULL,   PREC_NONE},
    [TOKEN_SUPER]         = {NULL,      NULL,   PREC_NONE},
    [TOKEN_THIS]          = {NULL,      NULL,   PREC_NONE},
    [TOKEN_TRUE]          = {literal,   NULL,   PREC_NONE},
    [TOKEN_VAR]           = {NULL,      NULL,   PREC_NONE},
    [TOKEN_WHILE]         = {NULL,      NULL,   PREC_NONE},
    [TOKEN_ERROR]         = {NULL,      NULL,   PREC_NONE},
    [TOKEN_EOF]           = {NULL,      NULL,   PREC_NONE},
};

static void parsePrecedence(Precedence precedence) {
    // See page 315 of the book for a diagram.
    advance();
    ParseFn prefixRule = getRule(parser.previous.type)->prefix;
    if (prefixRule == NULL) {
        error ("Expect expression.");
        return;
    }

    prefixRule();

    // Recurisvely handle operators with higher precedent.
    while (precedence <= getRule(parser.current.type)->precedence) {
        advance();
        ParseFn infixRule = getRule(parser.previous.type)->infix;
        infixRule();
    }
}

static ParseRule* getRule(TokenType type) {
    return &rules[type];
}

static void expression() {
    parsePrecedence(PREC_ASSIGNMENT);
}

/* Consume an expression statement into an instruction byte */
static void expressionStatement() {
    expression();
    consume(TOKEN_SEMICOLON, "Expect ';' after expression.");
    emitByte(OP_POP);
}

/* Consume a print statement into a print instruction byte */
static void printStatement() {
    expression();
    consume(TOKEN_SEMICOLON, "Expect ';' after value.");
    emitByte(OP_PRINT);
}

static void declaration() {
    statement();
}

static void statement() {
    if (match(TOKEN_PRINT)) { 
        printStatement();
    } else {
        expressionStatement();
    }
}


/* Compile the scanned source to bytecode.
   Return true on success and false if an error occurred
   during parsing */
bool compile(const char* source, Chunk* chunk) {
    initScanner(source);
    compilingChunk = chunk;

    parser.hadError  = false;
    parser.panicMode = false;

    advance();

    while (!match(TOKEN_EOF)) {
        declaration();
    }

    endCompiler();
    return !parser.hadError;
}
