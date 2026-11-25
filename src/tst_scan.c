/* Touchstone file model: directory walk, text loading, noise removal. */
#include "tst_scan.h"
#include "tst_util.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void tst_file_list_free(tst_file_list_t *l)
{
    size_t i;
    for (i = 0; i < l->n; i++) {
        free(l->items[i].path);
        free(l->items[i].rel);
        free(l->items[i].text);
    }
    free(l->items);
    l->items = NULL;
    l->n = 0;
    l->cap = 0;
}

void tst_ident_list_free(tst_ident_list_t *l)
{
    size_t left = l->n;
    tst_ident_t *items = l->items;
    while (left > 0) {
        left--;
        free(items[left].name);
    }
    free(items);
    l->items = NULL;
    l->n = 0;
    l->cap = 0;
}

void tst_normline_list_free(tst_normline_list_t *l)
{
    size_t i;
    for (i = 0; i < l->n; i++)
        free(l->items[i].norm);
    free(l->items);
    l->items = NULL;
    l->n = 0;
    l->cap = 0;
}

const char *tst_file_ext(const char *path)
{
    const char *dot = strrchr(path, '.');
    const char *slash = strrchr(path, '/');
    if (dot == NULL)
        return "";
    if (slash != NULL && dot < slash)
        return "";
    return dot + 1;
}

static int tst_ext_wanted(const char *ext, const char **exts, size_t nexts)
{
    size_t i;
    char low[32];
    size_t n = strlen(ext);
    if (n == 0 || n >= sizeof(low))
        return 0;
    tst_lower_copy(low, ext, n);
    for (i = 0; i < nexts; i++) {
        if (strcmp(low, exts[i]) == 0)
            return 1;
    }
    return 0;
}

static int tst_rel_skipped(const char *rel, const char **skip, size_t nskip)
{
    size_t i;
    for (i = 0; i < nskip; i++) {
        size_t n = strlen(skip[i]);
        if (strncmp(rel, skip[i], n) == 0 &&
            (rel[n] == '\0' || rel[n] == '/'))
            return 1;
    }
    return 0;
}

static void tst_file_list_push(tst_file_list_t *l, tst_file_t *f)
{
    if (l->n == l->cap) {
        size_t next = l->cap == 0 ? 32 : l->cap * 2;
        l->items = (tst_file_t *)tst_xrealloc(l->items,
            next * sizeof(tst_file_t));
        l->cap = next;
    }
    l->items[l->n] = *f;
    l->n++;
}

static int tst_is_binary(const char *buf, size_t len)
{
    size_t i;
    size_t n = len < 8192 ? len : 8192;
    for (i = 0; i < n; i++) {
        if (buf[i] == '\0')
            return 1;
    }
    return 0;
}

static void tst_count_lines(tst_file_t *f)
{
    size_t i;
    int blank = 1;
    f->lines = 0;
    f->nblank = 0;
    for (i = 0; i < f->len; i++) {
        if (f->text[i] == '\n') {
            f->lines++;
            if (!blank)
                f->nblank++;
            blank = 1;
        } else if (!isspace((unsigned char)f->text[i])) {
            blank = 0;
        }
    }
    if (f->len > 0 && f->text[f->len - 1] != '\n') {
        f->lines++;
        if (!blank)
            f->nblank++;
    }
}

static void tst_load_file(const char *path, const char *rel,
    tst_file_list_t *out)
{
    FILE *fp = fopen(path, "rb");
    long size;
    char *buf;
    tst_file_t f;
    size_t got;
    if (fp == NULL)
        return;
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return;
    }
    size = ftell(fp);
    if (size < 0) {
        fclose(fp);
        return;
    }
    rewind(fp);
    buf = (char *)tst_xmalloc((size_t)size + 1);
    got = fread(buf, 1, (size_t)size, fp);
    fclose(fp);
    buf[got] = '\0';
    if (tst_is_binary(buf, got)) {
        free(buf);
        return;
    }
    f.path = tst_dup(path);
    f.rel = tst_dup(rel);
    f.text = buf;
    f.len = got;
    f.lines = 0;
    f.nblank = 0;
    {
        const char *ext = tst_file_ext(path);
        char low[32];
        size_t n = strlen(ext);
        f.is_code = 0;
        f.is_doc = 0;
        if (n > 0 && n < sizeof(low)) {
            tst_lower_copy(low, ext, n);
            if (strcmp(low, "md") == 0)
                f.is_doc = 1;
            else if (strcmp(low, "json") != 0 &&
                strcmp(low, "txt") != 0)
                f.is_code = 1;
        }
    }
    tst_count_lines(&f);
    tst_file_list_push(out, &f);
}

static void tst_walk_into(const char *dir, const char *relbase,
    const char **exts, size_t nexts, const char **skip, size_t nskip,
    const char *skip_file, tst_file_list_t *out)
{
    DIR *d = opendir(dir);
    struct dirent *ent;
    if (d == NULL)
        return;
    while ((ent = readdir(d)) != NULL) {
        tst_buf_t full;
        tst_buf_t rel;
        if (ent->d_name[0] == '.')
            continue;
        tst_buf_init(&full);
        tst_buf_init(&rel);
        tst_buf_pushf(&full, "%s/%s", dir, ent->d_name);
        if (relbase[0] == '\0')
            tst_buf_pushf(&rel, "%s", ent->d_name);
        else
            tst_buf_pushf(&rel, "%s/%s", relbase, ent->d_name);
        if (!tst_rel_skipped(rel.data, skip, nskip)) {
            DIR *probe = opendir(full.data);
            if (probe != NULL) {
                closedir(probe);
                tst_walk_into(full.data, rel.data, exts, nexts, skip,
                    nskip, skip_file, out);
            } else if (skip_file == NULL ||
                strcmp(full.data, skip_file) != 0) {
                if (tst_ext_wanted(tst_file_ext(full.data), exts, nexts))
                    tst_load_file(full.data, rel.data, out);
            }
        }
        tst_buf_free(&full);
        tst_buf_free(&rel);
    }
    closedir(d);
}

static int tst_file_cmp(const void *a, const void *b)
{
    const tst_file_t *fa = (const tst_file_t *)a;
    const tst_file_t *fb = (const tst_file_t *)b;
    return strcmp(fa->rel, fb->rel);
}

int tst_walk(const char *root, const char **exts, size_t nexts,
    const char **skip, size_t nskip, const char *skip_file,
    tst_file_list_t *out)
{
    DIR *probe = opendir(root);
    if (probe == NULL)
        return -1;
    closedir(probe);
    out->items = NULL;
    out->n = 0;
    out->cap = 0;
    tst_walk_into(root, "", exts, nexts, skip, nskip, skip_file, out);
    if (out->n > 1)
        qsort(out->items, out->n, sizeof(tst_file_t), tst_file_cmp);
    return 0;
}

/* Comment and string families by extension. */
#define TST_FAM_PLAIN 0
#define TST_FAM_C 1
#define TST_FAM_HASH 2
#define TST_FAM_JSON 3
#define TST_FAM_HTML 4

static int tst_family(const char *ext)
{
    char low[32];
    size_t n = strlen(ext);
    if (n == 0 || n >= sizeof(low))
        return TST_FAM_PLAIN;
    tst_lower_copy(low, ext, n);
    if (strcmp(low, "c") == 0 || strcmp(low, "h") == 0 ||
        strcmp(low, "cc") == 0 || strcmp(low, "cpp") == 0 ||
        strcmp(low, "hpp") == 0 || strcmp(low, "js") == 0 ||
        strcmp(low, "mjs") == 0 || strcmp(low, "css") == 0)
        return TST_FAM_C;
    if (strcmp(low, "py") == 0 || strcmp(low, "sh") == 0)
        return TST_FAM_HASH;
    if (strcmp(low, "json") == 0)
        return TST_FAM_JSON;
    if (strcmp(low, "html") == 0 || strcmp(low, "htm") == 0)
        return TST_FAM_HTML;
    return TST_FAM_PLAIN;
}

static void tst_blank_span(char *out, size_t from, size_t to)
{
    size_t i;
    for (i = from; i < to; i++) {
        if (out[i] != '\n')
            out[i] = ' ';
    }
}

static size_t tst_skip_line(const char *text, size_t len, size_t i)
{
    while (i < len && text[i] != '\n')
        i++;
    return i;
}

/* Index just past the next occurrence of end, or len if it never comes. */
static size_t tst_skip_to(const char *text, size_t len, size_t i,
    const char *end, size_t endlen)
{
    size_t j = i;
    while (j + endlen <= len) {
        size_t k;
        int match = 1;
        for (k = 0; k < endlen; k++) {
            if (text[j + k] != end[k]) {
                match = 0;
                break;
            }
        }
        if (match)
            return j + endlen;
        j++;
    }
    return len;
}

/*
 * One pass over the text. Comments are always blanked. Quoted strings are
 * blanked only when drop_strings is set, so callers can choose whether
 * quoted words count as code or as data.
 */
static void tst_strip_core(const char *text, size_t len, const char *ext,
    char *out, int drop_strings)
{
    int fam = tst_family(ext);
    size_t i = 0;
    memcpy(out, text, len);
    out[len] = '\0';
    while (i < len) {
        char c = text[i];
        if (fam == TST_FAM_C && c == '/' && i + 1 < len &&
            text[i + 1] == '/') {
            size_t j = tst_skip_line(text, len, i);
            tst_blank_span(out, i, j);
            i = j;
            continue;
        }
        if (fam == TST_FAM_C && c == '/' && i + 1 < len &&
            text[i + 1] == '*') {
            size_t j = tst_skip_to(text, len, i + 2, "*/", 2);
            tst_blank_span(out, i, j);
            i = j;
            continue;
        }
        if (fam == TST_FAM_HASH && c == '#') {
            size_t j = tst_skip_line(text, len, i);
            tst_blank_span(out, i, j);
            i = j;
            continue;
        }
        if (fam == TST_FAM_HTML && c == '<' && i + 3 < len &&
            text[i + 1] == '!' && text[i + 2] == '-' &&
            text[i + 3] == '-') {
            size_t j = tst_skip_to(text, len, i + 4, "-->", 3);
            tst_blank_span(out, i, j);
            i = j;
            continue;
        }
        if (drop_strings && (fam == TST_FAM_C || fam == TST_FAM_HASH ||
            fam == TST_FAM_JSON) && (c == '"' || c == '\'')) {
            int triple = 0;
            char q = c;
            size_t j;
            if (fam == TST_FAM_HASH && i + 2 < len &&
                text[i + 1] == q && text[i + 2] == q)
                triple = 1;
            if (triple) {
                j = i + 3;
                while (j + 2 < len && !(text[j] == q &&
                    text[j + 1] == q && text[j + 2] == q))
                    j++;
                if (j + 2 < len)
                    j += 3;
                else
                    j = len;
            } else {
                j = i + 1;
                while (j < len && text[j] != q && text[j] != '\n') {
                    if (text[j] == '\\' && j + 1 < len)
                        j++;
                    j++;
                }
                if (j < len && text[j] == q)
                    j++;
            }
            tst_blank_span(out, i, j);
            i = j;
            continue;
        }
        i++;
    }
}

void tst_strip_noise(const char *text, size_t len, const char *ext, char *out)
{
    tst_strip_core(text, len, ext, out, 1);
}

void tst_strip_comments(const char *text, size_t len, const char *ext,
    char *out)
{
    tst_strip_core(text, len, ext, out, 0);
}

void tst_collect_idents(const char *clean, size_t len, tst_ident_list_t *out)
{
    size_t i = 0;
    int line = 1;
    out->items = NULL;
    out->n = 0;
    out->cap = 0;
    while (i < len) {
        char c = clean[i];
        if (c == '\n') {
            line++;
            i++;
            continue;
        }
        if (!tst_is_ident_start((unsigned char)c)) {
            i++;
            continue;
        }
        {
            size_t j = i;
            tst_ident_t id;
            while (j < len && tst_is_ident_char((unsigned char)clean[j]))
                j++;
            id.name = tst_dup_range(clean + i, j - i);
            id.line = line;
            if (out->n == out->cap) {
                size_t next = out->cap == 0 ? 64 : out->cap * 2;
                out->items = (tst_ident_t *)tst_xrealloc(out->items,
                    next * sizeof(tst_ident_t));
                out->cap = next;
            }
            out->items[out->n] = id;
            out->n++;
            i = j;
        }
    }
}

void tst_collect_normlines(const char *text, size_t len, const char *ext,
    tst_normline_list_t *out)
{
    char *clean = (char *)tst_xmalloc(len + 1);
    size_t i = 0;
    int line = 1;
    tst_strip_comments(text, len, ext, clean);
    out->items = NULL;
    out->n = 0;
    out->cap = 0;
    while (i < len) {
        size_t j = i;
        tst_buf_t nb;
        int spaced = 0;
        size_t k;
        while (j < len && clean[j] != '\n')
            j++;
        tst_buf_init(&nb);
        for (k = i; k < j; k++) {
            char c = clean[k];
            if (c == ' ' || c == '\t' || c == '\r') {
                if (nb.len > 0)
                    spaced = 1;
            } else {
                if (spaced) {
                    tst_buf_pushc(&nb, ' ');
                    spaced = 0;
                }
                tst_buf_pushc(&nb, c);
            }
        }
        if (nb.len > 0) {
            tst_normline_t nl;
            nl.norm = tst_dup(nb.data);
            nl.line = line;
            if (out->n == out->cap) {
                size_t next = out->cap == 0 ? 64 : out->cap * 2;
                out->items = (tst_normline_t *)tst_xrealloc(
                    out->items, next * sizeof(tst_normline_t));
                out->cap = next;
            }
            out->items[out->n] = nl;
            out->n++;
        }
        tst_buf_free(&nb);
        if (j < len && clean[j] == '\n') {
            line++;
            j++;
        }
        i = j;
    }
    free(clean);
}
