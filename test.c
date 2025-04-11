#include "cfg_parse.h"

int main(int argc, const char** argv) {
    if (argc != 2) {
        printf("Usage: %s [CONFIGURATION_FILE]\n", argv[0]);
        return 1;
    }

    Config *config;
    int status = ParseConfig(argv[1], &config);
    if (status < 0) {
        printf("%s", ErrToString(status));
        return 1;
    }

    DumpConfig(stdout, config);
    FreeConfig(config);
    return 0;
}
