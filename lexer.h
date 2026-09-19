#ifndef LEXER_H
#define LEXER_H

/* Number of entries in the keywords[] table in lexer.c.
 * The table MUST contain exactly this many strings (a missing entry would be
 * NULL and crash strcmp). C89 has 32 keywords. */
#define MAX_KEYWORDS 32

/* Biggest lexeme we store, including the terminating '\0'. */
#define MAX_TOKEN_SIZE 100

/* Every kind of token the lexer can report. */
typedef enum {
    KEYWORD,            /* int  return  while  unsigned ...                    */
    OPERATOR,           /* +  ==  &&  ->  <<=  ?  :  .  ...                    */
    SPECIAL_CHARACTER,  /* ,  ;  {  }  (  )  [  ]                              */
    CONSTANT,           /* integer constant:  10   0x1F   017   10UL           */
    IDENTIFIER,         /* variable / function names                           */
    LITERAL,            /* string literal:  "Hello World\n"                    */
    CHAR_LITERAL,       /* character literal:  'a'   '\n'                      */
    FLOAT_CONSTANT,     /* floating constant:  3.14   .5   1e-3   2.5f         */
    ARRAY,              /* identifier used as an array:  arr[10]               */
    PREPROCESSOR,       /* #include  #define  #ifdef ...                       */
    HEADER_FILE,        /* <stdio.h>  "myheader.h"  (only right after #include)*/
    UNKNOWN             /* invalid token, or end of file if lexeme is ""       */
} TokenType;

typedef struct {
    char lexeme[MAX_TOKEN_SIZE];   /* the text of the token, e.g. "while" */
    TokenType type;                /* what kind of token it is            */
} Token;

void initializeLexer(const char* filename);
Token getNextToken(void);
void categorizeToken(Token* token);
int isKeyword(const char* str);
int isOperator(const char* str);
int isSpecialCharacter(char ch);
int isIdentifier(const char* str);

/* Checks whether str is a valid numeric constant.
 * Returns 0 = not a number, 1 = integer constant, 2 = floating constant. */
int isConstant(const char* str);

#endif