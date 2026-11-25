/* Touchstone file model: directory walk, text loading, noise removal. */
#ifndef TOUCHSTONE_SCAN_H
#define TOUCHSTONE_SCAN_H

#include <stddef.h>

typedef struct {
    char *path;
    char *rel;
    char *text;
    size_t len;
    long lines;
    long nblank;
    int is_code;
    int is_doc;
} tst_file_t;

typedef struct {
    tst_file_t *items;
    size_t n;
    size_t cap;
} tst_file_list_t;

typedef struct {
    char *name;
    int line;
} tst_ident_t;

typedef struct {
    tst_ident_t *items;
    size_t n;
    size_t cap;
} tst_ident_list_t;

typedef struct {
    char *norm;
    int line;
} tst_normline_t;

typedef struct {
    tst_normline_t *items;
    size_t n;
    size_t cap;
} tst_normline_list_t;

void tst_file_list_free(tst_file_list_t *l);
void tst_ident_list_free(tst_ident_list_t *l);
void tst_normline_list_free(tst_normline_list_t *l);

int tst_walk(const char *root, const char **exts, size_t nexts,
    const char **skip, size_t nskip, const char *skip_file,
    tst_file_list_t *out);

const char *tst_file_ext(const char *path);

/* Copy text with comments and quoted strings blanked (newlines kept). */
void tst_strip_noise(const char *text, size_t len, const char *ext, char *out);
/* Copy text with comments blanked but quoted strings kept. */
void tst_strip_comments(const char *text, size_t len, const char *ext, char *out);

/* Identifiers found in noise-stripped text, with line numbers. */
void tst_collect_idents(const char *clean, size_t len, tst_ident_list_t *out);
/* Non-blank lines, comments stripped, whitespace collapsed. */
void tst_collect_normlines(const char *text, size_t len, const char *ext,
    tst_normline_list_t *out);

#endif
