#include "cfg_parse.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#define pushback(file, n) fseek(file, -(n), SEEK_CUR);

/* Syntax for the grammar:
 * <config_line> ::= <string> "=" <value> ";" | <string> "=" <array> ";"
 * <array> ::= "[" <value> ( "," <value> )* "]"
 * <value> ::= <quoted_string> | <number> | <decimal>
 * <quoted_string> ::= "'" <atoms>+ "'"
 * <atoms> ::= <digit> | <letter>
 * <number> ::= <digit>+
 * <decimal> ::= <digit>+ "." <digit>+
 * <digit> ::= [0-9]
 * <string> ::= <letter>+
 *
 * NOTE: 1) anything is a pseudo symbol that consumes every possible
 * character seen by the lexer
 * 2) <EOF> signifies the end of file
 */

static void ParseComment(FILE *file) {
    // The '#' has already been consumed
    // keep consuming everything until you see a newline or EOF
    // <comment> ::= "#" (anything)* ("\n" | <EOF>)
    while (1) {
        int c = fgetc(file);
        if (c == '\n' || c == EOF)
            break;
    }
}

static int IsSpecialSymbol(int c) {
    switch (c) {
    case '$':
    case '.':
    case '_':
        return 1;
    default:
        return 0;
    }
}

static int expect(FILE *file, int c) {
    int ch = fgetc(file);
    if (c != ch)
        return -1;
    return 1;
}

static int consumeLetter(FILE *file) {
    int c = fgetc(file);
    if (!isalpha(c))
        return -1;
    return 1;
}

static void consumeSpace(FILE *file) {
    while (1) {
        int c = fgetc(file);
        if (!isspace(c)) {
            pushback(file, 1);
            break;
        }
    }
}

static char* ParseString(FILE *file) {
    // <string> ::= <letter> (<letter>|<digit>|<symbol>)
    int len = 0;
    if (consumeLetter(file) < 0)
        return NULL;
    len++;

    while (1) {
        int c = fgetc(file);
        if (!isalnum(c) && !IsSpecialSymbol(c)) {
            pushback(file, 1);
            break;
        }

        len++;
    }

    pushback(file, len);
    char *string = malloc(sizeof(char) * (len + 1));
    fgets(string, len + 1, file);
    string[len] = '\0';

    return string;
}

static int ParseQuotedString(FILE *file, PrimitiveValue *value) {
    int len = 0;
    while (1) {
        int ch = fgetc(file);
        if (ch == EOF)
            return UNEXPECTED_EOF;

        if (ch == '\'')
            break;

        len += 1;
    }

    pushback(file, len + 1); // putback the ''' too
    value->String = malloc(sizeof(char) * (len + 1));
    fgets(value->String, len + 1, file);
    value->String[len] = '\0';
    fgetc(file); // remove the last '''
    return 1;
}

static int IsHexDigit(int c) {
    switch (tolower(c)) {
        case 'a' : case 'b' : case 'c':
        case 'd' : case 'e' : case 'f': return 1;
        default: return 0;
    }
}

static int ParseGenericNumber(FILE *file, PrimitiveValue *value) {
    // <number>  ::= <digit>+
    // <decimal> ::= <digit>+ '.' <digit>+
    // <generic_number> ::= <number> | <decimal>
    int len = 0, first_dot = 1, is_float = 0;
    while (1) {
        int ch = fgetc(file);
        if (ch == EOF) 
            return UNEXPECTED_TOKEN;

        if (isdigit(ch) || IsHexDigit(ch) || ch == 'x' || ch == 'X') 
            len++;
        else if (ch == '.' && first_dot) {
            len++;
            is_float = 1;
            first_dot = 0;
        }
        else {
            pushback(file, 1);
            break;
        }
    }

    pushback(file, len);
    char* str = malloc(sizeof(char) * (len + 1));
    fgets(str, len + 1, file);
    str[len] = '\0';

    if (is_float) {
        value->Type = DECIMAL_TYPE;
        char* endptr = "";
        value->Decimal = strtod(str, &endptr);
        if (*endptr != '\0')
            return INVALID_DECIMAL_LITERAL;
    }
    else {
        value->Type = NUMBER_TYPE;
        char* endptr = "";
        value->Number = strtol(str, &endptr, 0);
        if (*endptr != '\0')
            return INVALID_INTEGER_LITERAL;
    }

    free(str);
    return 1; 
}

static int ParseValue(FILE *file, Value *value) {
    // <value> = <generic_number> | <decimal> | <quoted_string>
    // At this point we are already pointing at some token
    int ch = fgetc(file);
    if (ch == EOF)
        return UNEXPECTED_EOF;

    value->Primitive = malloc(sizeof(PrimitiveValue));

    if (ch == '\'') {
        value->Primitive->Type = STRING_TYPE;
        return ParseQuotedString(file, value->Primitive);
    }

    else if (isdigit(ch)) {
        pushback(file, 1);
        return ParseGenericNumber(file, value->Primitive);
    }

    return UNEXPECTED_TOKEN;
}

static int ParseVector(FILE *file, Value *entry) {
    // vector ::= '[' <value> (',' <value>)* ']'
    entry->Array = malloc(sizeof(Vector));
    Vector *vec = entry->Array;
    vec->Data = malloc(sizeof(Value *) * 1);

    vec->Length = 0;
    int more = 1;
    while (1) {
        consumeSpace(file);
        int ch = fgetc(file);

        if (ch == EOF)
            return UNEXPECTED_EOF;

        else if (ch == ']')
            break;

        else if (ch == ',') {
            more = 1;
            continue;
        }

        if (!more)
            return UNEXPECTED_TOKEN;

        vec->Length++;
        pushback(file, 1);

        if (vec->Length == 1) {
            // Already allocated for us
            vec->Data[0] = malloc(sizeof(Value));
            if (ParseValue(file, vec->Data[0]) < 0)
                return INVALID_ARRAY_ELEMENT;
        }

        else {
            vec->Data = realloc(vec->Data, vec->Length * sizeof(Value*));
            vec->Data[vec->Length - 1] = malloc(sizeof(Value));
            if (ParseValue(file, vec->Data[vec->Length - 1]) < 0)
                return INVALID_ARRAY_ELEMENT;
        }

        more = 0;
    }

    return 1;
}

static int ParseConfigLine(FILE *file, ConfigEntry *entry) {
    // <config_line> ::= <string> "=" (<array> | <value>) ";"
    entry->Key = ParseString(file);
    if (!entry->Key)
        return INVALID_CONFIG_KEY;

    consumeSpace(file);
    if (expect(file, '=') < 0)
        return UNEXPECTED_TOKEN;

    consumeSpace(file);

    int ch = fgetc(file);
    if (ch == EOF)
        return UNEXPECTED_EOF;

    int status = 0;

    entry->Value = malloc(sizeof(Value));
    if (ch == '[') {
        entry->Type = ARRAY_TYPE;
        status = ParseVector(file, entry->Value);
    } else {
        entry->Type = PRIMITIVE_TYPE;
        pushback(file, 1);
        status = ParseValue(file, entry->Value);
    }

    consumeSpace(file);

    if (expect(file, ';') < 0)
        return UNEXPECTED_TOKEN;

    return status;
}

static int ParseCfg(FILE *file, ConfigEntry *entry) {
    // entry is already allocated for us
    // we will allocate entry->Next for the next configuration
    // initialising it with NULL_TYPE, ONLY IF what we are
    // parsing is an actual configuration

    // <cfg> ::= <empty> | <comment> | <config_line>
    // <empty> ::= '\n'
    while (1) {
        int c = fgetc(file);
        if (c == EOF)
            return UNEXPECTED_EOF;

        if (c == '\n')
            return 1;

        if (isspace(c))
            continue;

        if (c == '#') {
            ParseComment(file);
            return 1;
        }

        if (isalpha(c)) {
            pushback(file, 1);
            int status = ParseConfigLine(file, entry);
            if (status < 0)
                return status;
            break;
        }

        return UNEXPECTED_TOKEN;
    }

    // We came here after successfully parsing a config_line
    entry->Next = malloc(sizeof(ConfigEntry));
    entry->Next->Type = NULL_TYPE;
    entry->Next->Next = NULL;
    return 1;
}

int ParseConfig(const char *name, Config **result) {
    FILE *file = fopen(name, "r");
    if (!file)
        return FILE_NO_ACCESS;

    *result = malloc(sizeof(Config));
    if (!*result) {
        fclose(file);
        return OUT_OF_MEMORY;
    }

    Config *config = *result;
    config->Entries = 0;
    config->List = malloc(sizeof(ConfigEntry));
    config->List->Type = NULL_TYPE;
    config->List->Next = NULL;

    ConfigEntry *Tail = config->List; // maintain tail for fast access

    while (1) {
        int c = fgetc(file);
        // <prog> ::= <EOF>
        if (c == EOF)
            break;

        if (isspace(c))
            continue;

        // <prog> ::= <cfg>
        pushback(file, 1);
        int status = ParseCfg(file, Tail);
        if (status < 0) {
            free(config);
            fclose(file);
            return status;
        }

        if (Tail->Next) {
            Tail = Tail->Next;
            config->Entries++;
        }
    }

    free(Tail);
    fclose(file);
    return 1;
}

static void PrintPrimitive(FILE* file, PrimitiveValue* pv) {
    switch (pv->Type) {
        case NUMBER_TYPE: fprintf(file, "%ld", pv->Number); break;
        case DECIMAL_TYPE: fprintf(file, "%lf", pv->Decimal); break;
        case STRING_TYPE: fprintf(file, "'%s'", pv->String); break;
        default: printf("Unreachable!\n"); exit(1);
    }
}

static void PrintVector(FILE* file, Vector* vec) {
    fprintf(file, "[");
    for (uint64_t i = 0; i < vec->Length; i++) {
        PrimitiveValue* v = vec->Data[i]->Primitive;
        PrintPrimitive(file, v);
        if (i + 1 != vec->Length)
            fprintf(file, ",");
    }
    
    fprintf(file, "]");
}

void DumpConfig(FILE* file, Config* config) {
    ConfigEntry* ce = config->List;
    for (int i = 0; i < config->Entries; i++) {
        fprintf(file, "%s = ", ce->Key);
        Value* value = ce->Value;
        if (ce->Type == PRIMITIVE_TYPE)
            PrintPrimitive(file, value->Primitive);
        else {
            PrintVector(file, value->Array);
        }
        ce = ce->Next;
        fprintf(file, ";\n");
    }
}

void FreeConfig(Config* config) {
    ConfigEntry* ce = config->List;
    for (int i = 0; i < config->Entries; i++) {
        ConfigEntry* next = ce->Next;
        if (!ce)
            break;
        free(ce->Key);
        Value* v = ce->Value;
        if (ce->Type == PRIMITIVE_TYPE) {
            PrimitiveValue* pv = v->Primitive;
            if (pv->Type == STRING_TYPE)
                free(pv->String);
            free(pv);
        }
        else {
            Vector* vec = v->Array;
            for (int i = 0; i < vec->Length; i++) {
                PrimitiveValue* pv = vec->Data[i]->Primitive;
                if (pv->Type == STRING_TYPE)
                free(pv->String);
                free(pv);
                free(vec->Data[i]);
            }
            free(vec->Data);
            free(vec);
        }

        free(v);
        free(ce);

        if (next)
            ce = next;
        else
            break;
    }

    free(config);
}

Value* FindValue(Config* config, int ty, const char* Key) {
    ConfigEntry* ce = config->List;
    for (int i = 0; i < config->Entries; i++) {
        if (strcmp(ce->Key, Key) == 0 && ce->Type == ty)
            return ce->Value;
        ce = ce->Next;
    }

    return NULL;
}

PrimitiveValue* GetElement(Vector* v, uint64_t idx) {
    if (v->Length <= idx)
        return NULL;
    return v->Data[idx]->Primitive;
}

const char* ErrToString(int status) {
    switch (status) {
        case FILE_NO_ACCESS: return "Config file inaccessible"; 
        case OUT_OF_MEMORY:  return "Out of memory";
        case UNEXPECTED_EOF: return "Unexpected End Of File while parsing";
        case UNEXPECTED_TOKEN: return "Unexpected token while parsing";
        case INVALID_CONFIG_KEY: return "Configuration key must be a string";
        case INVALID_ARRAY_ELEMENT:
        return "Array elements must follow this syntax: [ele1 , ele2, ...]";
        case INVALID_INTEGER_LITERAL: return "Invalid integer literal";
        case INVALID_DECIMAL_LITERAL: return "Invalid floating point literal";
        default: return "Unknown error.";
    }
}

