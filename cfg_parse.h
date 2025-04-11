#pragma once
#include <stdint.h>
#include <stdio.h>

typedef struct PrimtiveValue {
    union {
        int64_t Number;
        double Decimal;
        char *String;
    };
    int Type;
} PrimitiveValue;

struct Value;
typedef struct Vector {
    uint64_t Length;
    struct Value **Data;
} Vector;

typedef struct Value {
    PrimitiveValue *Primitive;
    Vector *Array;
} Value;

typedef struct ConfigEntry {
    char *Key;
    Value *Value;
    struct ConfigEntry *Next;
    int Type;
} ConfigEntry;

typedef struct Config {
    uint64_t Entries;
    ConfigEntry *List;
} Config;

#define NULL_TYPE (0)
#define NUMBER_TYPE (1)
#define DECIMAL_TYPE (2)
#define STRING_TYPE (3)
#define ARRAY_TYPE (4)
#define PRIMITIVE_TYPE (5)

#define FILE_NO_ACCESS (-1)
#define OUT_OF_MEMORY (-2)
#define UNEXPECTED_EOF (-3)
#define UNEXPECTED_TOKEN (-4)
#define INVALID_CONFIG_KEY (-5)
#define INVALID_ARRAY_ELEMENT (-6)
#define INVALID_INTEGER_LITERAL (-7)
#define INVALID_DECIMAL_LITERAL (-8)
// Returns < 0 on failure, 1 on success
int ParseConfig(const char *name, Config **result);

// Serialise 'config' into 'file'
void DumpConfig(FILE* file, Config* config);

// Free config
void FreeConfig(Config* config);

// Find value corresponding to configuration option 'Key' of type 'ty'
Value* FindValue(Config* config, int ty, const char* Key);

// Get Element 'idx' of v, iff idx < v->Length, othwerwise NULL
PrimitiveValue* GetElement(Vector* v, uint64_t idx);

// Convert error code from ParseConfig() to string
const char* ErrToString(int status); 
