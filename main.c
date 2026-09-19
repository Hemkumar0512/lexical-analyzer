#include <stdio.h>
#include "lexer.h"

/* Converts a token type into the label printed on screen.
 * Change the text here if you want different labels. */
static const char* typeName(TokenType type)
{
    switch (type)
    {
        case KEYWORD:           return "Keyword";
        case OPERATOR:          return "Operator";
        case SPECIAL_CHARACTER: return "Special Char";
        case CONSTANT:          return "Integer";
        case FLOAT_CONSTANT:    return "Float";
        case IDENTIFIER:        return "Identifier";
        case ARRAY:             return "Array";
        case LITERAL:           return "Literal";
        case CHAR_LITERAL:      return "Char Literal";
        case PREPROCESSOR:      return "Preprocessor";
        case HEADER_FILE:       return "Header File";
        default:                return "Unknown";
    }
}

int main(int argc, char *argv[])
{
    Token token;

    /* The program needs exactly one argument: the C file to analyse. */
    if (argc != 2)
    {
        printf("Usage: %s <.c file>\n", argv[0]);
        return 1;
    }

    initializeLexer(argv[1]);            /* opens the file, prints Open : ... */
    printf("Parsing  : %s : Started\n\n", argv[1]);

    for (;;)
    {
        token = getNextToken();

        /* getNextToken returns type UNKNOWN both at the end of the file and
         * for an invalid token. At the end of the file the lexeme is empty,
         * so an empty lexeme means "stop". A real invalid token has text
         * and is printed as "Unknown" while we keep going. */
        if (token.type == UNKNOWN && token.lexeme[0] == '\0')
            break;

        printf("%-13s: %s\n", typeName(token.type), token.lexeme);
    }

    printf("\nParsing  : %s : Done\n", argv[1]);
    return 0;
}