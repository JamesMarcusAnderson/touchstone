/* TrueSmith file model: directory walk, text loading, noise removal. */
#ifndef TRUESMITH_SCAN_H
#define TRUESMITH_SCAN_H

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
} ts_file_t;

typedef struct {
    ts_file_t *items;
    size_t n;
    size_t cap;
} ts_file_list_t;

typedef struct {
    char *name;
    int line;
} ts_ident_t;

typedef struct {
    ts_ident_t *items;
    size_t n;
    size_t cap;
} ts_ident_list_t;

typedef struct {
    char *norm;
    int line;
} ts_normline_t;

typedef struct {
    ts_normline_t *items;
    size_t n;
    size_t cap;
} ts_normline_list_t;

void ts_file_list_free(ts_file_list_t *l);
void ts_ident_list_free(ts_ident_list_t *l);
void ts_normline_list_free(ts_normline_list_t *l);

int ts_walk(const char *root, const char **exts, size_t nexts,
    const char **skip, size_t nskip, const char *skip_file,
    ts_file_list_t *out);

const char *ts_file_ext(const char *path);

/* Copy text with comments and quoted strings blanked (newlines kept). */
void ts_strip_noise(const char *text, size_t len, const char *ext, char *out);
/* Copy text with comments blanked but quoted strings kept. */
void ts_strip_comments(const char *text, size_t len, const char *ext, char *out);

/* Identifiers found in noise-stripped text, with line numbers. */
void ts_collect_idents(const char *clean, size_t len, ts_ident_list_t *out);
/* Non-blank lines, comments stripped, whitespace collapsed. */
void ts_collect_normlines(const char *text, size_t len, const char *ext,
    ts_normline_list_t *out);

#endif
