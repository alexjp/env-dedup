#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <stdbool.h>
#include <stdint.h>


// Original functions
typedef int (*real_setenv_t)(const char *, const char *, int);
typedef int (*real_putenv_t)(char *);
static real_setenv_t real_setenv = NULL;
static real_putenv_t real_putenv = NULL;

// Check if a variable should be deduplicated (e.g., colon-separated)

/* 1.  O(1) lookup ---------------------------------------------------- */
static inline bool should_dedup(const char *name)
{
    /* 14 is small enough for a jump table */
    switch (name[0]) {
    case 'G':
        if (strcmp(name, "GTK2_RC_FILES") == 0) return true;
        if (strcmp(name, "GTK_PATH")      == 0) return true;
        if (strcmp(name, "GTK_RC_FILES")  == 0) return true;
        return false;
    case 'I':
        return strcmp(name, "INFOPATH") == 0;
    case 'L':
        return strcmp(name, "LIBEXEC_PATH") == 0;
    case 'N':
        if (strcmp(name, "NIXPKGS_QT6_QML_IMPORT_PATH") == 0) return true;
        return strcmp(name, "NIX_PATH") == 0;
    case 'P':
        if (strcmp(name, "PATH") == 0) return true;
        return false;
    case 'Q':
        if (strcmp(name, "QML2_IMPORT_PATH") == 0) return true;
        if (strcmp(name, "QTWEBKIT_PLUGIN_PATH") == 0) return true;
        if (strcmp(name, "QT_PLUGIN_PATH") == 0) return true;
        return false;
    case 'T':
        return strcmp(name, "TERMINFO_DIRS") == 0;
    case 'X':
        if (strcmp(name, "XCURSOR_PATH") == 0) return true;
        if (strcmp(name, "XDG_CONFIG_DIRS") == 0) return true;
        return strcmp(name, "XDG_DATA_DIRS") == 0;
    }
    return false;
}

// Deduplicate a colon-separated string (e.g., PATH)
/* 2.  Single-pass dedup ---------------------------------------------- */
static inline char *dedup_colon_string(const char *value)
{
    if (!value || !*value) return strdup("");

    /* conservative first guess – avoids realloc in 99 % of cases */
    size_t  seg_cap = 32;
    char  **seg     = malloc(seg_cap * sizeof *seg);
    size_t  seg_cnt = 0;

    uint64_t seen_mask = 0;            /* bitmask for first 64 segments */
    size_t   out_len   = 1;            /* final '\0' */

    char *copy = strdup(value);
    for (char *tok = copy, *saveptr;; tok = NULL) {
        char *p = strtok_r(tok, ":", &saveptr);
        if (!p) break;

        /* linear search through the *unique* ones we already have */
        bool dup = false;
        for (size_t i = 0; i < seg_cnt; ++i) {
            if (strcmp(seg[i], p) == 0) { dup = true; break; }
        }
        if (dup) continue;

        /* grow array if needed */
        if (seg_cnt == seg_cap) {
            seg_cap *= 2;
            seg = reallocarray(seg, seg_cap, sizeof *seg);
        }
        seg[seg_cnt] = p;
        out_len += strlen(p) + 1;   /* +1 for ':' or '\0' */
        ++seg_cnt;
    }

    /* build result */
    char *out = malloc(out_len), *wp = out;
    for (size_t i = 0; i < seg_cnt; ++i) {
        size_t len = strlen(seg[i]);
        if (i) *wp++ = ':';
        memcpy(wp, seg[i], len);
        wp += len;
    }
    *wp = '\0';

    free(copy);          /* we did *not* strdup the individual segments */
    free(seg);
    return out;
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
/* 3.  Cached dlsym --------------------------------------------------- */
__attribute__((constructor))
static void init(void)
{
    real_setenv = (real_setenv_t)dlsym(RTLD_NEXT, "setenv");
    real_putenv = (real_putenv_t)dlsym(RTLD_NEXT, "putenv");
}
