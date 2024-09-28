#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

#define MAX_LINE_LENGTH 1024
#define MAX_DEFINITIONS 50
#define MAX_IDENTIFIER_LENGTH 64
#define MAX_VALUE_LENGTH 256

struct Definition {
    char identifier[MAX_IDENTIFIER_LENGTH];
    char value[MAX_VALUE_LENGTH];
    int resolving;  // To detect cyclic dependencies
};

struct Definition definitions[MAX_DEFINITIONS];
int def_count = 0;

// Custom string functions
int custom_strlen(const char *s) {
    int len = 0;
    while (s[len]) len++;
    return len;
}

void custom_strcpy(char *dest, const char *src) {
    while ((*dest++ = *src++));
}

int custom_strncmp(const char *s1, const char *s2, int n) {
    while (n--)
        if (*s1++ != *s2++)
            return (unsigned char) *(s1 - 1) - (unsigned char) *(s2 - 1);
    return 0;
}

char *custom_strchr(const char *s, int c) {
    while (*s != (char) c)
        if (!*s++)
            return 0;
    return (char *) s;
}

void custom_strncpy(char *dest, const char *src, int n) {
    int i;
    for (i = 0; i < n && src[i] != '\0'; i++)
        dest[i] = src[i];
    for (; i < n; i++)
        dest[i] = '\0';
}

int is_alnum(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

// Recursive function to resolve definitions, now detects cyclic dependencies
int resolve_definition(char *value) {
    for (int i = 0; i < def_count; i++) {
        if (custom_strncmp(value, definitions[i].identifier, custom_strlen(definitions[i].identifier)) == 0) {
            if (definitions[i].resolving) {
                // Cyclic dependency detected, set to "1"
                custom_strcpy(definitions[i].value, "1");  // Set the value to "1" in the case of a cycle
                return 1;
            }
            // Mark the definition as being resolved
            definitions[i].resolving = 1;
            resolve_definition(definitions[i].value);
            custom_strcpy(value, definitions[i].value);  // Update the current definition's value
            definitions[i].resolving = 0;  // Mark resolution as complete
            return 1;
        }
    }
    return 0;  // No further resolution needed
}

void preprocess_definitions() {
    for (int i = 0; i < def_count; i++) {
        resolve_definition(definitions[i].value);
    }
}

// Updated function to handle redefinition of variables
void add_definition(char *arg) {
    char *eq = custom_strchr(arg, '=');
    if (eq == 0) {
        printf(2, "Invalid definition: %s\n", arg);
        return;
    }

    int id_len = eq - arg - 2;  // -2 to skip "-D"
    int val_len = custom_strlen(eq + 1);  // This can be 0 (i.e., empty string)

    if (id_len >= MAX_IDENTIFIER_LENGTH || val_len >= MAX_VALUE_LENGTH) {
        printf(2, "Identifier or value too long: %s\n", arg);
        return;
    }

    // Check if this identifier already exists in definitions
    for (int i = 0; i < def_count; i++) {
        if (custom_strncmp(definitions[i].identifier, arg + 2, id_len) == 0 &&
            custom_strlen(definitions[i].identifier) == id_len) {
            // Overwrite the existing definition
            custom_strncpy(definitions[i].value, eq + 1, val_len);
            definitions[i].value[val_len] = '\0';
            return;
        }
    }

    // If no existing definition found, add a new one
    custom_strncpy(definitions[def_count].identifier, arg + 2, id_len);
    definitions[def_count].identifier[id_len] = '\0';
    custom_strcpy(definitions[def_count].value, eq + 1);  // This can copy an empty string
    definitions[def_count].resolving = 0;  // Initialize resolving flag
    def_count++;
}

int is_identifier_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

char* replace_variables(char *line) {
    static char new_line[MAX_LINE_LENGTH];
    char *result = new_line;
    char *word_start = line;
    int i;

    while (*line) {
        // Check if we're at the start of a potential identifier
        if (is_identifier_char(*line) && (line == word_start || !is_identifier_char(*(line - 1)))) {
            for (i = 0; i < def_count; i++) {
                int len = custom_strlen(definitions[i].identifier);
                if (custom_strncmp(line, definitions[i].identifier, len) == 0 &&
                    (!is_identifier_char(line[len]))) {
                    // We found a match. Copy everything before this word.
                    while (word_start < line) {
                        *result++ = *word_start++;
                    }
                    // Copy the replacement (can be an empty string)
                    custom_strcpy(result, definitions[i].value);
                    result += custom_strlen(definitions[i].value);
                    line += len;
                    word_start = line;
                    break;
                }
            }
            if (i == def_count) {
                // No replacement found, move to next character
                line++;
            }
        } else {
            // Not start of an identifier, move to next character
            line++;
        }
    }
    // Copy any remaining characters
    while (*word_start) {
        *result++ = *word_start++;
    }
    *result = '\0';
    return new_line;
}

int main(int argc, char *argv[]) {
    int fd, n, i;
    char line[MAX_LINE_LENGTH];
    int empty_file = 1;  // Flag to check if file is empty

    if (argc < 3) {
        printf(2, "Usage: %s <input_file> -D<var1>=<val1> [-D<var2>=<val2> ...]\n", argv[0]);
        exit();
    }

    // Open input file
    fd = open(argv[1], O_RDONLY);
    if (fd < 0) {
        printf(2, "Failed to open input file: %s\n", argv[1]);
        exit();
    }

    // Process definitions
    for (i = 2; i < argc; i++) {
        if (custom_strncmp(argv[i], "-D", 2) == 0) {
            add_definition(argv[i]);
        }
    }

    // Preprocess all definitions (resolve any nested references)
    preprocess_definitions();

    // Process input file
    while ((n = read(fd, line, sizeof(line))) > 0) {
        empty_file = 0;  // File has content
        char *processed_line = replace_variables(line);
        printf(1, "%s", processed_line);
    }

    // If file is empty, print a message
    if (empty_file) {
        printf(1, "File is empty\n");
    }

    close(fd);
    exit();
}
