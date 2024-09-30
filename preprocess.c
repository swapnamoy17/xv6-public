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
    int from_file;  // 1 if from file (#define), 0 if from command line
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
    return (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9');
}

int is_identifier_char(char c) {
    return is_alnum(c) || (c == '_');  // Allow alphanumeric characters and underscore
}

// Recursive function to resolve definitions, now detects cyclic dependencies
int resolve_definition(char *value) {
    int i;
    for (i = 0; i < def_count; i++) {
        int id_len = custom_strlen(definitions[i].identifier);
        int val_len = custom_strlen(value);
        if (val_len == id_len && custom_strncmp(value, definitions[i].identifier, id_len) == 0) {
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
    int i;
    for (i = 0; i < def_count; i++) {
        resolve_definition(definitions[i].value);
    }
}

// Updated function to handle redefinition of variables
void add_definition(char *arg, int from_file) {
    char *eq = custom_strchr(arg, '=');
    if (eq == 0 || eq == arg + 2) {
        // Case where no macro name is given (e.g., -D=)
        printf(2, "error: no macro name given in #define directive\n");
        return;
    }

    int id_len = eq - arg - 2;  // -2 to skip "-D"
    int val_len = custom_strlen(eq + 1);  // This can be 0 (i.e., empty string)

    if (id_len >= MAX_IDENTIFIER_LENGTH || val_len >= MAX_VALUE_LENGTH) {
        printf(2, "Identifier or value too long: %s\n", arg);
        return;
    }

    // Check if this identifier already exists in definitions
    int i;
    for (i = 0; i < def_count; i++) {
        if (custom_strncmp(definitions[i].identifier, arg + 2, id_len) == 0 &&
            custom_strlen(definitions[i].identifier) == id_len) {
            // Variable already defined
            if (definitions[i].from_file == 0 && from_file == 1) {
                // Existing definition is from command line, new one from file
                printf(2, "Warning: variable '%s' is being redefined via #define\n", definitions[i].identifier);
                // Overwrite the existing definition with the one from the file
                custom_strncpy(definitions[i].value, eq + 1, val_len);
                definitions[i].value[val_len] = '\0';
                definitions[i].from_file = 1;  // Update source to file
            } else if (definitions[i].from_file == 1 && from_file == 0) {
                // Existing definition is from file, new one from command line
                printf(2, "Warning: variable '%s' is already defined via #define, command line definition ignored\n", definitions[i].identifier);
                // Do not overwrite
            } else {
                // Both definitions are from the same source, overwrite
                custom_strncpy(definitions[i].value, eq + 1, val_len);
                definitions[i].value[val_len] = '\0';
            }
            return;
        }
    }

    // If no existing definition found, add a new one
    if (def_count >= MAX_DEFINITIONS) {
        printf(2, "Maximum number of definitions reached.\n");
        return;
    }
    custom_strncpy(definitions[def_count].identifier, arg + 2, id_len);
    definitions[def_count].identifier[id_len] = '\0';
    custom_strcpy(definitions[def_count].value, eq + 1);  // This can copy an empty string
    definitions[def_count].resolving = 0;  // Initialize resolving flag
    definitions[def_count].from_file = from_file;  // Set the source flag
    def_count++;
}

char* replace_variables(char *line) {
    static char new_line[MAX_LINE_LENGTH];
    char *result = new_line;
    char *word_start = line;
    int i;
    int inside_single_quotes = 0;  // Flag for single quotes
    int inside_double_quotes = 0;  // Flag for double quotes

    while (*line) {
        if (*line == '\'' && !inside_double_quotes) {
            inside_single_quotes = !inside_single_quotes;
        } else if (*line == '\"' && !inside_single_quotes) {
            inside_double_quotes = !inside_double_quotes;
        }

        // Check for identifier replacements if not inside quotes
        if (!inside_single_quotes && !inside_double_quotes && is_identifier_char(*line) &&
            (line == word_start || !is_identifier_char(*(line - 1)))) {
            int replaced = 0;
            for (i = 0; i < def_count; i++) {
                int len = custom_strlen(definitions[i].identifier);
                if (custom_strncmp(line, definitions[i].identifier, len) == 0 &&
                    (!is_identifier_char(line[len]))) {
                    // Replace variable
                    while (word_start < line) {
                        *result++ = *word_start++;
                    }
                    custom_strcpy(result, definitions[i].value);
                    result += custom_strlen(definitions[i].value);
                    line += len;
                    word_start = line;
                    replaced = 1;
                    break;
                }
            }
            if (!replaced) {
                line++;
            }
        } else {
            line++;
        }
    }
    while (*word_start) {
        *result++ = *word_start++;
    }
    *result = '\0';
    return new_line;
}

int main(int argc, char *argv[]) {
    int fd, n, i;
    char line[MAX_LINE_LENGTH];
    // Removed 'empty_file' variable and related logic

    if (argc < 2) {
        printf(2, "Usage: %s <input_file> [-D<var1>=<val1> [-D<var2>=<val2> ...]]\n", argv[0]);
        exit();
    }

    // Open input file for the first pass to process #define directives
    fd = open(argv[1], O_RDONLY);
    if (fd < 0) {
        printf(2, "Failed to open input file: %s\n", argv[1]);
        exit();
    }

    // First pass: process #define directives
    while ((n = read(fd, line, sizeof(line))) > 0) {
        // Process each line separately
        int start = 0;
        int end = 0;
        while (end < n) {
            if (line[end] == '\n' || end == n - 1) {
                // Get a complete line
                char temp_line[MAX_LINE_LENGTH];
                int len = end - start + 1;
                if (line[end] != '\n' && end == n - 1)
                    len++;  // Include the last character
                custom_strncpy(temp_line, line + start, len);
                temp_line[len] = '\0';

                // Handle #define directive
                if (custom_strncmp(temp_line, "#define", 7) == 0) {
                    char *line_ptr = temp_line + 7;
                    while (*line_ptr == ' ' || *line_ptr == '\t') line_ptr++;

                    // Extract identifier and value from #define
                    char identifier[MAX_IDENTIFIER_LENGTH];
                    char value[MAX_VALUE_LENGTH];

                    char *id_start = line_ptr;
                    while (is_identifier_char(*line_ptr)) line_ptr++;
                    int id_len = line_ptr - id_start;
                    custom_strncpy(identifier, id_start, id_len);
                    identifier[id_len] = '\0';

                    while (*line_ptr == ' ' || *line_ptr == '\t') line_ptr++;
                    char *val_start = line_ptr;
                    while (*line_ptr != '\0' && *line_ptr != ' ' && *line_ptr != '\t' && *line_ptr != '\n') line_ptr++;
                    int val_len = line_ptr - val_start;
                    custom_strncpy(value, val_start, val_len);
                    value[val_len] = '\0';

                    // Add #define as if it were passed as -D argument
                    char def_str[MAX_IDENTIFIER_LENGTH + MAX_VALUE_LENGTH + 3];
                    def_str[0] = '-';
                    def_str[1] = 'D';
                    custom_strcpy(def_str + 2, identifier);
                    int def_len = custom_strlen(def_str);
                    def_str[def_len] = '=';
                    custom_strcpy(def_str + def_len + 1, value);

                    add_definition(def_str, 1);  // 1 indicates from file
                }

                start = end + 1;
            }
            end++;
        }
    }

    close(fd);

    // Now process command-line definitions after processing #define directives
    for (i = 2; i < argc; i++) {
        if (custom_strncmp(argv[i], "-D", 2) == 0) {
            add_definition(argv[i], 0);  // 0 indicates command line
        }
    }

    // Preprocess all definitions (resolve any nested references)
    preprocess_definitions();

    // Reopen the file for the second pass
    fd = open(argv[1], O_RDONLY);
    if (fd < 0) {
        printf(2, "Failed to reopen input file: %s\n", argv[1]);
        exit();
    }

    // Second pass: process and print the lines
    while ((n = read(fd, line, sizeof(line))) > 0) {
        // Process each line separately
        int start = 0;
        int end = 0;
        while (end < n) {
            if (line[end] == '\n' || end == n - 1) {
                // Get a complete line
                char temp_line[MAX_LINE_LENGTH];
                int len = end - start + 1;
                if (line[end] != '\n' && end == n - 1)
                    len++;  // Include the last character
                custom_strncpy(temp_line, line + start, len);
                temp_line[len] = '\0';

                // If line starts with #define, skip printing it
                if (custom_strncmp(temp_line, "#define", 7) != 0) {
                    // Replace variables
                    char *processed_line = replace_variables(temp_line);
                    printf(1, "%s", processed_line);
                }

                start = end + 1;
            }
            end++;
        }
    }

    close(fd);
    exit();
}
