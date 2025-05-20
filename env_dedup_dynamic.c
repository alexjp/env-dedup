#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <stdbool.h>

// Original functions
typedef int (*real_setenv_t)(const char *, const char *, int);
typedef int (*real_putenv_t)(char *);
static real_setenv_t real_setenv = NULL;
static real_putenv_t real_putenv = NULL;

// Check if a variable should be deduplicated (e.g., colon-separated)
static bool should_dedup(const char *name) {
    const char *dedup_vars[] = {
        "GTK2_RC_FILES",
        "GTK_PATH",
        "GTK_RC_FILES",
        "INFOPATH",
        "LIBEXEC_PATH",
        "NIXPKGS_QT6_QML_IMPORT_PATH",
        "NIX_PATH",
        "PATH",
        "QML2_IMPORT_PATH",
        "QTWEBKIT_PLUGIN_PATH",
        "QT_PLUGIN_PATH",
        "TERMINFO_DIRS",
        "XCURSOR_PATH",
        "XDG_CONFIG_DIRS",
        "XDG_DATA_DIRS",
        NULL};

    for (int i = 0; dedup_vars[i]; i++) {
        if (strcmp(name, dedup_vars[i]) == 0) {
            return true;
        }
    }
    return false;
}

// Deduplicate a colon-separated string (e.g., PATH)
static char *dedup_colon_string(const char *value) {
    if (!value || !*value) return strdup("");

    char **segments = NULL;
    char *copy = strdup(value);
    int segment_count = 0;
    char *token, *rest = copy;

    // Split into segments
    while ((token = strtok_r(rest, ":", &rest))) {
        segments = realloc(segments, (segment_count + 1) * sizeof(char *));
        segments[segment_count++] = strdup(token);
    }

    // Mark duplicates (keep first occurrence)
    int *keep = calloc(segment_count, sizeof(int));
    for (int i = 0; i < segment_count; i++) {
        keep[i] = 1;
        for (int j = 0; j < i; j++) {
            if (strcmp(segments[j], segments[i]) == 0) {
                keep[i] = 0;
                break;
            }
        }
    }

    // Calculate required size
    size_t total_len = 1; // Null terminator
    for (int i = 0; i < segment_count; i++) {
        if (keep[i]) total_len += strlen(segments[i]) + 1; // +1 for colon
    }

    // Build result
    char *result = malloc(total_len);
    result[0] = '\0';
    int first = 1;
    for (int i = 0; i < segment_count; i++) {
        if (keep[i]) {
            if (!first) strcat(result, ":");
            strcat(result, segments[i]);
            first = 0;
        }
    }

    // Cleanup
    for (int i = 0; i < segment_count; i++) free(segments[i]);
    free(segments);
    free(keep);
    free(copy);

    return result;
}

// Clean a specific variable (if needed)
static void clean_var(const char *name) {
    if (!should_dedup(name)) return;

    char *value = getenv(name);
    if (!value) return;

    char *deduped = dedup_colon_string(value);
    if (!deduped) return;

    if (strcmp(value, deduped) != 0) {
        if (!real_setenv) real_setenv = (real_setenv_t)dlsym(RTLD_NEXT, "setenv");
        real_setenv(name, deduped, 1);
    }
    free(deduped);
}

// Hooked setenv
int setenv(const char *name, const char *value, int overwrite) {
    if (!real_setenv) real_setenv = (real_setenv_t)dlsym(RTLD_NEXT, "setenv");
    int ret = real_setenv(name, value, overwrite);
    clean_var(name); // Only clean the modified variable
    return ret;
}

// Hooked putenv (extract name from "NAME=value")
int putenv(char *string) {
    if (!real_putenv) real_putenv = (real_putenv_t)dlsym(RTLD_NEXT, "putenv");
    int ret = real_putenv(string);

    // Extract variable name from "NAME=value"
    char *eq = strchr(string, '=');
    if (eq) {
        char *name = strndup(string, eq - string);
        clean_var(name);
        free(name);
    }
    return ret;
}

// Initialize at load (optional)
__attribute__((constructor)) void init() {
    // Initialize original functions
    real_setenv = (real_setenv_t)dlsym(RTLD_NEXT, "setenv");
    real_putenv = (real_putenv_t)dlsym(RTLD_NEXT, "putenv");
}
