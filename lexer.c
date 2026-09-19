#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "lexer.h"

/* All 32 C keywords. Must have exactly MAX_KEYWORDS entries. */
static const char* keywords[MAX_KEYWORDS] = {
    "int", "float", "return", "if", "else", "while", "for", "do", "break", "continue",
    "char", "double", "void", "switch", "case", "default", "const", "static", "sizeof", "struct",
    "long", "unsigned", "short", "enum", "typedef", "union", "goto", "extern", "register",
    "signed", "volatile", "auto"
};

/* Every character that can be (part of) an operator. */
static const char* operators = "+-*/%=!<>|&^~?:.";

/* Characters that are always a token on their own. */
static const char* specialCharacters = ",;{}()[]";

/* Operators that are exactly two characters long (NULL marks the end). */
static const char* twoCharOperators[] = {
    "==", "!=", "<=", ">=", "&&", "||", "++", "--",
    "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=",
    "<<", ">>", "->",
    NULL
};

/* ====================================================================== */
/*  LEXER STATE - variables that live for the whole run                   */
/* ====================================================================== */

static FILE *fptr = NULL;      /* the source file being scanned                    */
static int expectHeader = 0;   /* 1 right after "#include": next token is <file.h> */

/* ====================================================================== */
/*  SMALL HELPER FUNCTIONS (static = only usable inside this file)        */
/* ====================================================================== */

/* Is the pair (first, second) one of the two-character operators? */
static int isTwoCharOperator(char first, char second)
{
    for (int i = 0; twoCharOperators[i] != NULL; i++)
    {
        if (twoCharOperators[i][0] == first && twoCharOperators[i][1] == second)
        {
            return 1;
        }
    }
    return 0;
}

/* Look at the next character in the file WITHOUT consuming it.
 * We read it with fgetc and immediately push it back with ungetc.
 * (ungetc(EOF) does nothing, so this is also safe at the end of the file.) */
static int peekChar(void)
{
    int c = fgetc(fptr);
    ungetc(c, fptr);
    return c;
}

/* Is the next NON-whitespace character equal to 'wanted'? (consumes nothing)
 * ungetc can only push back ONE character reliably, but here we may skip
 * many spaces. So we remember our position with ftell(), read ahead, and
 * jump back with fseek(). Used to detect  arr [ 5 ]  (space before '['). */
static int nextNonSpaceIs(int wanted)
{
    long saved = ftell(fptr);          /* where we are now                 */
    int c;

    if (saved == -1L)                  /* file cannot seek (e.g. a pipe):  */
    {
        return peekChar() == wanted;   /* only check the very next char    */
    }

    do {
        c = fgetc(fptr);               /* skip spaces, tabs, newlines      */
    } while (isspace(c));

    fseek(fptr, saved, SEEK_SET);      /* go back: we only wanted to look  */
    return c == wanted;
}

/* Reads the rest of a delimited item into token->lexeme. Used for:
 *      "string"      open = '"'   close = '"'   escapes allowed
 *      'c'           open = '\''  close = '\''  escapes allowed
 *      <stdio.h>     open = '<'   close = '>'   no escapes
 * The opening character has ALREADY been read by the caller.
 * Returns 1 if the closing character was found, 0 if the line or file ended
 * first (i.e. the literal is unterminated -> a lexical error). */
static int readDelimited(Token* token, int open, int close, int allowEscapes)
{
    int ch;
    int i = 1;                       /* lexeme[0] is the opening character */
    int closed = 0;

    token->lexeme[0] = open;

    /* Read until end of file or end of line: these items can't span lines. */
    while ((ch = fgetc(fptr)) != EOF && ch != '\n')
    {
        /* Store the character, but never write past the end of lexeme[].
         * Extra characters of a very long literal are read and dropped. */
        if (i < MAX_TOKEN_SIZE - 1)
        {
            token->lexeme[i++] = ch;
        }

        if (allowEscapes && ch == '\\')
        {
            /* A backslash escapes the next character (\" or \n or \\ ...).
             * Copy that character too WITHOUT checking whether it is the
             * closing quote - that is what lets  "say \"hi\""  work. */
            ch = fgetc(fptr);
            if (ch == EOF || ch == '\n')
            {
                break;
            }
            if (i < MAX_TOKEN_SIZE - 1)
            {
                token->lexeme[i++] = ch;
            }
        }
        else if (ch == close)        /* the real closing delimiter */
        {
            closed = 1;
            break;
        }
    }

    token->lexeme[i] = '\0';
    return closed;
}

/* Reads the rest of a number into token->lexeme.
 * 'i' = how many characters the caller has already stored (the first one).
 *
 * We deliberately read GREEDILY: digits, letters, '_' and '.', plus a sign
 * right after an exponent letter (1e-3, 2E+5). So  12abc  and  1.2.3  are
 * read as ONE bad token instead of being split into several valid-looking
 * ones. Whether the text is a VALID number is decided later by isConstant(). */
static void readNumber(Token* token, int i)
{
    int ch;

    while (i < MAX_TOKEN_SIZE - 1)
    {
        ch = fgetc(fptr);

        if (isalnum(ch) || ch == '_' || ch == '.')
        {
            token->lexeme[i++] = ch;

            /* 'e' / 'E' (and 'p' / 'P' for hex floats) may be followed by a
             * sign that belongs to the number:  1e-3  ->  one token. */
            if (ch == 'e' || ch == 'E' || ch == 'p' || ch == 'P')
            {
                int next = peekChar();
                if ((next == '+' || next == '-') && i < MAX_TOKEN_SIZE - 1)
                {
                    token->lexeme[i++] = fgetc(fptr);
                }
            }
        }
        else
        {
            ungetc(ch, fptr);        /* first char that is NOT part of the number */
            break;
        }
    }

    token->lexeme[i] = '\0';
}

/* ====================================================================== */
/*  initializeLexer - open the file                                       */
/* ====================================================================== */

void initializeLexer(const char* filename)
{
    expectHeader = 0;
    fptr = fopen(filename, "r");
    if (fptr == NULL)
    {
        printf("Open    : %s : Failed\n", filename);
        return;
    }
    printf("Open    : %s : Success\n", filename);
}

/* ====================================================================== */
/*  getNextToken - the heart of the lexer                                 */
/*                                                                        */
/*  Each call returns ONE token. The order of the checks below matters:   */
/*    0. after #include -> a header name                                  */
/*    1. skip whitespace and comments                                     */
/*    2. end of file                                                      */
/*    3. word     -> keyword / identifier / array                         */
/*    4. number   -> integer / float (or error)                           */
/*    5. "..."    -> string literal                                       */
/*    6. '...'    -> character literal                                    */
/*    7. #        -> preprocessor directive                               */
/*    8. , ; { } ( ) [ ]  -> special character                            */
/*    9. + - * / ...      -> operator (1, 2 or 3 characters)              */
/*   10. anything else    -> UNKNOWN (lexical error)                      */
/* ====================================================================== */

Token getNextToken(void)
{
    int ch, i = 0;               /* ch = current char, i = next free slot in lexeme */
    Token token;
    token.lexeme[0] = '\0';
    token.type = UNKNOWN;        /* UNKNOWN + empty lexeme = "end of file" */

    if (fptr == NULL)
    {
        return token;
    }

    /* ---- 0. Header name right after #include ---------------------------
     * "#include <stdio.h>": without this rule, <stdio.h> would be split
     * into  <  stdio  .  h  >.  Here we read it as ONE header-file token. */
    if (expectHeader)
    {
        expectHeader = 0;                      /* only applies to ONE token */

        do {
            ch = fgetc(fptr);                  /* skip blanks (not newlines) */
        } while (ch == ' ' || ch == '\t');

        if (ch == '<')
        {
            token.type = readDelimited(&token, '<', '>', 0) ? HEADER_FILE : UNKNOWN;
            return token;
        }
        if (ch == '"')
        {
            token.type = readDelimited(&token, '"', '"', 0) ? HEADER_FILE : UNKNOWN;
            return token;
        }
        ungetc(ch, fptr);                      /* not a header name: scan normally */
    }

    /* ---- 1. Skip whitespace AND comments -------------------------------
     * We loop because after a comment there may be more whitespace or
     * another comment. We only leave the loop when 'ch' is the first
     * character of a real token (or EOF). */
    for (;;)
    {
        int after;

        do {
            ch = fgetc(fptr);                  /* skip spaces, tabs, newlines */
        } while (isspace(ch));

        if (ch != '/')
        {
            break;                             /* can't be a comment: done */
        }

        /* We saw '/'. Look at the next char to decide what it is. */
        after = fgetc(fptr);

        if (after == '/')                      /* "//" line comment */
        {
            /* throw away everything up to the end of the line */
            while ((ch = fgetc(fptr)) != EOF && ch != '\n')
            {
            }
            continue;                          /* look for the next token */
        }

        if (after == '*')                      /* block comment */
        {
            int previous = 0, closed = 0;

            /* Keep reading until we see  * followed by / */
            while ((ch = fgetc(fptr)) != EOF)
            {
                if (previous == '*' && ch == '/')
                {
                    closed = 1;
                    break;
                }
                previous = ch;
            }

            if (!closed)                       /* file ended inside the comment */
            {
                strcpy(token.lexeme, "/*");
                token.type = UNKNOWN;          /* lexeme is not empty -> an error, not EOF */
                return token;
            }
            continue;                          /* look for the next token */
        }

        /* Just a plain '/' (division) or "/=": give the extra char back
         * and let the operator code below handle it. */
        ungetc(after, fptr);
        break;
    }

    /* ---- 2. End of file ------------------------------------------------ */
    if (ch == EOF)
    {
        return token;                          /* UNKNOWN + empty lexeme */
    }

    /* ---- 3. Keyword / identifier / array ------------------------------- */
    if (isalpha(ch) || ch == '_')
    {
        token.lexeme[i++] = ch;                /* first letter */
        while (i < MAX_TOKEN_SIZE - 1)
        {
            ch = fgetc(fptr);
            if (isalnum(ch) || ch == '_')
            {
                token.lexeme[i++] = ch;        /* letters, digits, '_' continue the word */
            }
            else
            {
                ungetc(ch, fptr);              /* not part of the word: give it back */
                break;
            }
        }
        token.lexeme[i] = '\0';

        if (isKeyword(token.lexeme))
        {
            token.type = KEYWORD;
        }
        else if (nextNonSpaceIs('['))          /* name directly followed by '[' */
        {
            token.type = ARRAY;                /* arr[10]  or  arr [i] */
        }
        else
        {
            token.type = IDENTIFIER;
        }
        return token;
    }

    /* ---- 4. Number ------------------------------------------------------
     * A number starts with a digit, OR with '.' followed by a digit (.5).
     * A lone '.' (as in p.x) is an operator and is handled further down. */
    if (isdigit(ch) || (ch == '.' && isdigit(peekChar())))
    {
        int kind;

        token.lexeme[i++] = ch;
        readNumber(&token, i);                 /* read the whole "number-like" word */

        kind = isConstant(token.lexeme);       /* 0 = invalid, 1 = integer, 2 = float */
        if (kind == 2)
        {
            token.type = FLOAT_CONSTANT;
        }
        else if (kind == 1)
        {
            token.type = CONSTANT;
        }
        else
        {
            token.type = UNKNOWN;              /* e.g. 12abc, 1.2.3, 089 */
        }
        return token;
    }

    /* ---- 5. String literal:  "..." ------------------------------------- */
    if (ch == '"')
    {
        token.type = readDelimited(&token, '"', '"', 1) ? LITERAL : UNKNOWN;
        return token;
    }

    /* ---- 6. Character literal:  'a'  '\n' ------------------------------ */
    if (ch == '\'')
    {
        int ok = readDelimited(&token, '\'', '\'', 1);

        if (ok && strlen(token.lexeme) == 2)   /* '' -> only the two quotes: empty, invalid */
        {
            ok = 0;
        }
        token.type = ok ? CHAR_LITERAL : UNKNOWN;
        return token;
    }

    /* ---- 7. Preprocessor directive:  #include  #define ... -------------- */
    if (ch == '#')
    {
        token.lexeme[i++] = '#';

        if (peekChar() == '#')                 /* "##" (token-pasting operator) */
        {
            token.lexeme[i++] = fgetc(fptr);
            token.lexeme[i] = '\0';
            token.type = PREPROCESSOR;
            return token;
        }

        /* Blanks are allowed between '#' and the name:  #   define */
        do {
            ch = fgetc(fptr);
        } while (ch == ' ' || ch == '\t');

        /* Collect the directive name (letters, digits, '_') */
        while (isalnum(ch) || ch == '_')
        {
            if (i < MAX_TOKEN_SIZE - 1)
            {
                token.lexeme[i++] = ch;
            }
            ch = fgetc(fptr);
        }
        ungetc(ch, fptr);                      /* first char after the name */
        token.lexeme[i] = '\0';
        token.type = PREPROCESSOR;

        if (strcmp(token.lexeme, "#include") == 0)
        {
            expectHeader = 1;                  /* next call reads <file.h> */
        }
        return token;
    }

    /* ---- 8. Special character:  , ; { } ( ) [ ] -------------------------- */
    if (isSpecialCharacter(ch))
    {
        token.lexeme[0] = ch;
        token.lexeme[1] = '\0';
        token.type = SPECIAL_CHARACTER;
        return token;
    }

    /* ---- 9. Operator: 1, 2 or 3 characters ------------------------------
     * We always take the LONGEST operator that matches (>>= beats >> beats >). */
    if (ch != '\0' && strchr(operators, ch) != NULL)
    {
        int next;

        token.lexeme[i++] = ch;                /* the first character */
        next = fgetc(fptr);                    /* peek at the following one */

        if (next != EOF && isTwoCharOperator(ch, next))
        {
            token.lexeme[i++] = next;          /* two-character operator */

            /* << and >> can grow into <<= and >>= */
            if ((ch == '<' || ch == '>') && next == ch)
            {
                int third = fgetc(fptr);
                if (third == '=')
                {
                    token.lexeme[i++] = third;
                }
                else
                {
                    ungetc(third, fptr);
                }
            }
        }
        else
        {
            ungetc(next, fptr);                /* not part of it: give it back */
        }

        token.lexeme[i] = '\0';
        token.type = OPERATOR;
        return token;
    }

    /* ---- 10. Anything else is a lexical error ----------------------------
     * (@  $  `  \  or a stray control character.) */
    if (ch == '\0')
    {
        strcpy(token.lexeme, "\\0");           /* keep lexeme non-empty so it isn't mistaken for EOF */
    }
    else
    {
        token.lexeme[0] = ch;
        token.lexeme[1] = '\0';
    }
    token.type = UNKNOWN;
    return token;
}

/* ====================================================================== */
/*  categorizeToken - decide the type of an already-read lexeme           */
/* ====================================================================== */

void categorizeToken(Token* token)
{
    int kind;

    if (isKeyword(token->lexeme))
    {
        token->type = KEYWORD;
    }
    else if (isOperator(token->lexeme))
    {
        token->type = OPERATOR;
    }
    else if (isSpecialCharacter(token->lexeme[0]) && token->lexeme[1] == '\0')
    {
        token->type = SPECIAL_CHARACTER;
    }
    else if (isIdentifier(token->lexeme))
    {
        token->type = IDENTIFIER;
    }
    else if ((kind = isConstant(token->lexeme)) != 0)
    {
        token->type = (kind == 2) ? FLOAT_CONSTANT : CONSTANT;
    }
    else
    {
        token->type = UNKNOWN;
    }
}

/* ====================================================================== */
/*  Classification functions                                              */
/* ====================================================================== */

/* Is str one of the 32 C keywords? */
int isKeyword(const char* str)
{
    for (int i = 0; i < MAX_KEYWORDS; i++)
    {
        if (strcmp(str, keywords[i]) == 0)
        {
            return 1;
        }
    }
    return 0;
}

/* Is str a complete operator?  (1 char, 2 chars from the table, or <<= / >>=) */
int isOperator(const char* str)
{
    if (str[0] == '\0')
    {
        return 0;                                  /* empty string */
    }

    if (str[1] == '\0')                            /* one character */
    {
        return strchr(operators, str[0]) != NULL;
    }

    if (str[2] == '\0')                            /* two characters */
    {
        return isTwoCharOperator(str[0], str[1]);
    }

    return strcmp(str, "<<=") == 0 || strcmp(str, ">>=") == 0;   /* three characters */
}

/* Is ch one of  , ; { } ( ) [ ]  ?
 * NOTE: strchr(s, '\0') "finds" the string's terminator, so the '\0'
 * character must be rejected explicitly. */
int isSpecialCharacter(char ch)
{
    return ch != '\0' && strchr(specialCharacters, ch) != NULL;
}

/* Is str a valid identifier?  First char: letter or '_'; the rest: letter,
 * digit or '_'.  (Use && here: "NOT a letter AND NOT an underscore".) */
int isIdentifier(const char* str)
{
    if (!isalpha(str[0]) && str[0] != '_')
    {
        return 0;
    }
    for (int i = 1; str[i] != '\0'; i++)
    {
        if (!isalnum(str[i]) && str[i] != '_')
        {
            return 0;
        }
    }
    return 1;
}

/* Checks whether str is a VALID numeric constant.
 *   integer: 42   017 (octal)   0x1F (hex)      + optional suffix u U l L (UL, ull ...)
 *   float  : 3.14   .5   3.   1e10   1.5E-3     + optional suffix f F l L
 * Returns 0 = not a number, 1 = integer, 2 = float. */
int isConstant(const char* str)
{
    const char* p = str;     /* walks through the string, one part at a time */
    int isFloat = 0;

    if (str[0] == '\0')
    {
        return 0;
    }

    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X'))
    {
        /* ---- hexadecimal: 0x followed by at least one hex digit ---- */
        p += 2;
        if (!isxdigit(*p))
        {
            return 0;                              /* "0x" alone is invalid */
        }
        while (isxdigit(*p))
        {
            p++;
        }
    }
    else
    {
        /* ---- decimal / octal / float ---- */
        const char* start = p;
        int digits = 0;                            /* counts digits before AND after the '.' */

        while (isdigit(*p))                        /* integer part */
        {
            p++;
            digits++;
        }

        if (*p == '.')                             /* fractional part */
        {
            isFloat = 1;
            p++;
            while (isdigit(*p))
            {
                p++;
                digits++;
            }
        }

        if (digits == 0)
        {
            return 0;                              /* e.g. "." or ".e5" has no digits */
        }

        if (*p == 'e' || *p == 'E')                /* exponent: e10  e-3  E+5 */
        {
            isFloat = 1;
            p++;
            if (*p == '+' || *p == '-')
            {
                p++;
            }
            if (!isdigit(*p))
            {
                return 0;                          /* "1e" or "1e+" is invalid */
            }
            while (isdigit(*p))
            {
                p++;
            }
        }

        /* An octal constant (starts with 0) may not contain 8 or 9: 089 is invalid. */
        if (!isFloat && start[0] == '0')
        {
            for (const char* q = start; q < p; q++)
            {
                if (*q == '8' || *q == '9')
                {
                    return 0;
                }
            }
        }
    }

    /* ---- optional suffix ---- */
    if (isFloat)
    {
        if (*p == 'f' || *p == 'F' || *p == 'l' || *p == 'L')
        {
            p++;                                   /* 2.5f  or  2.5L */
        }
    }
    else
    {
        int u = 0, l = 0;                          /* at most one u and up to two l */
        while (*p != '\0')
        {
            if ((*p == 'u' || *p == 'U') && !u)
            {
                u = 1;
            }
            else if ((*p == 'l' || *p == 'L') && l < 2)
            {
                l++;
            }
            else
            {
                break;
            }
            p++;
        }
    }

    /* If anything is left over (like the "abc" in 12abc), it is NOT a number. */
    if (*p != '\0')
    {
        return 0;
    }

    return isFloat ? 2 : 1;
}