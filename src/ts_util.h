/* TrueSmith shared helpers: growable buffers, text handling, mixing hash. */
#ifndef TRUESMITH_UTIL_H
#define TRUESMITH_UTIL_H

#include <stdarg.h>
#include <stddef.h>

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} ts_buf_t;

void ts_buf_init(ts_buf_t *b);
void ts_buf_free(ts_buf_t *b);
void ts_buf_push(ts_buf_t *b, const char *s, size_t n);
void ts_buf_pushc(ts_buf_t *b, char c);
void ts_buf_pushf(ts_buf_t *b, const char *fmt, ...);
void ts_buf_pushv(ts_buf_t *b, const char *fmt, va_list ap);

char *ts_dup_range(const char *s, size_t n);
char *ts_dup(const char *s);
void *ts_xmalloc(size_t n);
void *ts_xcalloc(size_t n, size_t size);
void *ts_xrealloc(void *p, size_t n);
int ts_lower_eq(const char *a, const char *b);
int ts_lower_contains(const char *hay, const char *needle);
void ts_lower_copy(char *dst, const char *src, size_t n);

unsigned long long ts_mix_hash(const char *s, size_t n);
unsigned long long ts_mix_step(unsigned long long h, unsigned long long v);

int ts_is_ident_start(int c);
int ts_is_ident_char(int c);

#endif
