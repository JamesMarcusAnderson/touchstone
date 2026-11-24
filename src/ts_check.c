/* TrueSmith checks: five evidence checks plus weighted scoring. */
#include "ts_check.h"
#include "ts_util.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Weights: names 25, third-party 25, duplication 20, real work 20, docs 10. */
#define TS_W_NAMES 25
#define TS_W_THIRD 25
#define TS_W_DUP 20
#define TS_W_REAL 20
#define TS_W_DOCS 10
#define TS_DUP_WINDOW 6

void ts_check_free(ts_check_t *c)
{
    size_t i;
    for (i = 0; i < c->n; i++)
        free(c->items[i].text);
    free(c->items);
    c->items = NULL;
    c->n = 0;
    c->cap = 0;
}

void ts_file_result_free(ts_file_result_t *r)
{
    ts_check_free(&r->names);
    ts_check_free(&r->third);
    ts_check_free(&r->dup);
    ts_check_free(&r->real);
    ts_check_free(&r->docs);
}

static void ts_check_init(ts_check_t *c)
{
    c->score = 100;
    c->items = NULL;
    c->n = 0;
    c->cap = 0;
}

static void ts_check_push(ts_check_t *c, int line, const char *text)
{
    ts_finding_t f;
    if (c->n == c->cap) {
        size_t next = c->cap == 0 ? 8 : c->cap * 2;
        c->items = (ts_finding_t *)ts_xrealloc(c->items,
            next * sizeof(ts_finding_t));
        c->cap = next;
    }
    f.line = line;
    f.text = ts_dup(text);
    c->items[c->n] = f;
    c->n++;
}

static void ts_check_deduct(ts_check_t *c, int amount)
{
    c->score -= amount;
    if (c->score < 0)
        c->score = 0;
}

/*
 * Detection list: names of other projects, products, companies, and
 * programming languages used as product names. Whole-token matches only.
 * This list is data, not code; it is finite by nature, and the README says
 * so plainly. Generic formats, protocols, and character sets are out, and
 * so are words too common to be a reliable signal: meta, edge, obs,
 * scheme, atom, and nano all name real projects, but they also name
 * ordinary things in code, so they stay out rather than cry wolf.
 */
static const char *ts_borrowed_names[] = {
    "vulkan", "cuda", "hip", "metal", "gguf", "ggml", "opencl", "opengl",
    "directx", "spirv", "webgpu", "sdl", "glfw", "glut", "boost", "qt",
    "gtk", "wxwidgets", "llvm", "clang", "gcc", "mingw", "msbuild",
    "xcode", "cmake", "meson", "ninja", "gradle", "maven", "ant", "bazel",
    "android", "ios", "osx", "linux", "windows", "macos", "apple",
    "nvidia", "amd", "intel", "arm", "qualcomm", "broadcom", "microchip",
    "atmel", "espressif", "stm32", "arduino", "raspberry", "freertos",
    "zephyr", "mbed", "opencv", "eigen", "protobuf", "grpc", "openssl",
    "curl", "zlib", "sqlite", "postgres", "mysql", "redis", "mongo",
    "kafka", "docker", "kubernetes", "react", "angular", "vue", "nodejs",
    "django", "flask", "rails", "laravel", "wordpress", "jquery",
    "bootstrap", "tailwind", "electron", "flutter", "kotlin", "swift",
    "scala", "haskell", "erlang", "elixir", "julia", "matlab", "fortran",
    "cobol", "pascal", "delphi", "ada", "lisp", "clojure",
    "groovy", "dart", "java", "javascript", "typescript", "python",
    "ruby", "perl", "lua", "rust", "golang", "csharp", "php", "unity",
    "unreal", "godot", "blender", "steam", "github", "gitlab", "openai",
    "anthropic", "google", "microsoft", "facebook", "amazon",
    "oracle", "ibm", "cisco", "samsung", "sony", "nintendo",
    "playstation", "xbox", "tensorflow", "pytorch", "numpy", "scipy",
    "pandas", "keras", "sklearn", "huggingface", "langchain", "ollama",
    "llama", "mistral", "gemini", "copilot", "chatgpt", "chromium",
    "firefox", "safari", "chrome", "opera", "brave", "vscode",
    "eclipse", "intellij", "pycharm", "sublime", "vim", "emacs",
    "git", "svn", "mercurial", "perforce", "jenkins", "travis",
    "circleci", "appveyor", "codecov", "coveralls", "sonarqube",
    "splint", "valgrind", "gdb", "lldb", "strace", "wireshark", "nmap",
    "metasploit", "burp", "john", "hashcat", "aircrack", "ettercap",
    "tcpdump", "netcat", "socat", "putty", "winscp", "filezilla",
    "wget", "aria2", "ffmpeg", "imagemagick", "gimp", "inkscape",
    "audacity", "vlc", "handbrake", "posix", "ansi", "win32",
    "win64"
};
#define TS_BORROWED_COUNT (sizeof(ts_borrowed_names) / sizeof(const char *))

/*
 * License markers. "spdx" on its own is a format name, so only the
 * license-header token counts. "copyright" counts only with notice
 * context on the same line; see ts_notice_context.
 */
static const char *ts_license_markers[] = {
    "copyright", "licensed under", "spdx-license-identifier",
    "all rights reserved"
};
#define TS_LICENSE_COUNT (sizeof(ts_license_markers) / sizeof(const char *))

static const char *ts_credit_phrases[] = {
    "based on", "adapted from", "inspired by", "thanks to", "credit to",
    "ported from", "taken from", "borrowed from", "copied from",
    "derived from"
};
#define TS_CREDIT_COUNT (sizeof(ts_credit_phrases) / sizeof(const char *))

static const char *ts_unfinished_markers[] = {
    "todo", "fixme", "xxx", "placeholder", "not implemented", "stub"
};
#define TS_UNFINISHED_COUNT \
    (sizeof(ts_unfinished_markers) / sizeof(const char *))

static const char *ts_control_words[] = {
    "if", "for", "while", "switch", "return", "sizeof", "else", "do",
    "catch", "typeof"
};
#define TS_CONTROL_COUNT (sizeof(ts_control_words) / sizeof(const char *))

static const char *ts_common_files[] = {
    "LICENSE", "README", "Makefile", "CHANGELOG", "VERSION", "COPYING",
    "NOTICE", "AUTHORS"
};
#define TS_COMMON_COUNT (sizeof(ts_common_files) / sizeof(const char *))

static int ts_in_list(const char *word, const char **list, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        if (ts_lower_eq(word, list[i]))
            return 1;
    }
    return 0;
}

static int ts_name_allowed(const char *word, const char **allow, size_t n)
{
    return ts_in_list(word, allow, n);
}

/* Split an identifier into pieces on underscores and lower-to-upper edges. */
static int ts_piece_borrowed(const char *ident, const char **allow,
    size_t nallow, char *hit, size_t hitcap)
{
    size_t len = strlen(ident);
    size_t i = 0;
    while (i < len) {
        size_t j = i;
        size_t k;
        char piece[64];
        size_t plen;
        while (j < len && ident[j] != '_') {
            if (j > i && islower((unsigned char)ident[j - 1]) &&
                isupper((unsigned char)ident[j]))
                break;
            j++;
        }
        plen = j - i;
        if (plen > 0 && plen < sizeof(piece)) {
            ts_lower_copy(piece, ident + i, plen);
            if (ts_in_list(piece, ts_borrowed_names, TS_BORROWED_COUNT) &&
                !ts_name_allowed(piece, allow, nallow)) {
                size_t m;
                size_t copy = plen < hitcap - 1 ? plen : hitcap - 1;
                for (m = 0; m < copy; m++)
                    hit[m] = piece[m];
                hit[copy] = '\0';
                return 1;
            }
        }
        k = j;
        while (k < len && ident[k] == '_')
            k++;
        i = k;
    }
    return 0;
}

/* Rebuild comment-only text: code chars become spaces, comments stay. */
static char *ts_comment_text(const char *text, size_t len, const char *ext)
{
    char *stripped = (char *)ts_xmalloc(len + 1);
    char *out = (char *)ts_xmalloc(len + 1);
    size_t i;
    ts_strip_comments(text, len, ext, stripped);
    for (i = 0; i < len; i++) {
        if (text[i] == '\n') {
            out[i] = '\n';
        } else if (stripped[i] == ' ' && text[i] != ' ') {
            out[i] = text[i];
        } else if (stripped[i] == text[i]) {
            out[i] = ' ';
        } else {
            out[i] = ' ';
        }
    }
    out[len] = '\0';
    free(stripped);
    return out;
}

typedef struct {
    int marker;
    int line;
} ts_hit_t;

typedef struct {
    ts_hit_t *items;
    size_t n;
    size_t cap;
} ts_hit_list_t;

static void ts_hit_push(ts_hit_list_t *l, int marker, int line)
{
    size_t i;
    for (i = 0; i < l->n; i++) {
        if (l->items[i].marker == marker)
            return;
    }
    if (l->n == l->cap) {
        size_t next = l->cap == 0 ? 8 : l->cap * 2;
        l->items = (ts_hit_t *)ts_xrealloc(l->items,
            next * sizeof(ts_hit_t));
        l->cap = next;
    }
    l->items[l->n].marker = marker;
    l->items[l->n].line = line;
    l->n++;
}

/*
 * Case-insensitive marker scan, one distinct hit per marker, first line.
 * A marker counts only as a whole word: the characters on either side
 * must not be identifier characters, so a longer word never trips a
 * short marker hidden inside it.
 */
static int ts_marker_in_line(const char *low, const char *marker)
{
    size_t mlen = strlen(marker);
    const char *at = low;
    while ((at = strstr(at, marker)) != NULL) {
        const char *after = at + mlen;
        int before_ok = at == low ||
            !ts_is_ident_char((unsigned char)at[-1]);
        int after_ok = *after == '\0' ||
            !ts_is_ident_char((unsigned char)*after);
        if (before_ok && after_ok)
            return 1;
        at++;
    }
    return 0;
}

/*
 * A bare mention of copyright is prose, not a notice. A real notice
 * carries (c), the copyright sign, or a year right next to the word,
 * or the reserved-rights phrase on the same line.
 */
static int ts_notice_context(const char *low)
{
    const char *hit = strstr(low, "copyright");
    size_t i;
    if (hit == NULL)
        return 0;
    if (strstr(low, "all rights reserved") != NULL)
        return 1;
    for (i = 0; i < 16 && hit[i] != '\0'; i++) {
        if (hit[i + 1] != '\0' && hit[i + 2] != '\0' &&
            hit[i] == '(' && hit[i + 1] == 'c' && hit[i + 2] == ')')
            return 1;
        if (hit[i + 1] != '\0' &&
            (unsigned char)hit[i] == 0xc2 &&
            (unsigned char)hit[i + 1] == 0xa9)
            return 1;
        if (hit[i + 1] != '\0' && hit[i + 2] != '\0' &&
            hit[i + 3] != '\0' &&
            ((hit[i] == '1' && hit[i + 1] == '9') ||
             (hit[i] == '2' && hit[i + 1] == '0')) &&
            hit[i + 2] >= '0' && hit[i + 2] <= '9' &&
            hit[i + 3] >= '0' && hit[i + 3] <= '9')
            return 1;
    }
    return 0;
}

static void ts_scan_markers(const char *text, const char **markers,
    size_t nmarkers, ts_hit_list_t *hits)
{
    const char *p = text;
    int line = 1;
    hits->items = NULL;
    hits->n = 0;
    hits->cap = 0;
    while (*p != '\0') {
        const char *eol = strchr(p, '\n');
        size_t llen = eol != NULL ? (size_t)(eol - p) : strlen(p);
        size_t m;
        char *low = (char *)ts_xmalloc(llen + 1);
        size_t k;
        for (k = 0; k < llen; k++)
            low[k] = (char)tolower((unsigned char)p[k]);
        low[llen] = '\0';
        for (m = 0; m < nmarkers; m++) {
            if (ts_marker_in_line(low, markers[m])) {
                if (strcmp(markers[m], "copyright") == 0 &&
                    !ts_notice_context(low))
                    continue;
                ts_hit_push(hits, (int)m, line);
            }
        }
        free(low);
        if (eol == NULL)
            break;
        line++;
        p = eol + 1;
    }
}

static void ts_hit_list_free(ts_hit_list_t *l)
{
    free(l->items);
    l->items = NULL;
    l->n = 0;
    l->cap = 0;
}

/* ---- check 1: original names ---- */

static void ts_names_note(ts_check_t *out, char seen[][64], size_t *nseen,
    int line, const char *msg, const char *hit)
{
    size_t s;
    for (s = 0; s < *nseen; s++) {
        if (strcmp(seen[s], hit) == 0)
            return;
    }
    ts_check_push(out, line, msg);
    ts_check_deduct(out, 20);
    if (*nseen < 16) {
        strcpy(seen[*nseen], hit);
        (*nseen)++;
    }
}

/*
 * Names the code does not choose: identifiers starting with an
 * underscore live in the implementation's namespace (compiler macros
 * such as the platform tests, CRT helpers), and posix_ names are the
 * platform API spelled as written. Neither is our naming.
 */
static int ts_reserved_name(const char *name)
{
    if (name[0] == '_')
        return 1;
    if (strncmp(name, "posix_", 6) == 0)
        return 1;
    return 0;
}

static void ts_check_names(ts_file_t *f, ts_ident_list_t *idents,
    const char **allow, size_t nallow, ts_check_t *out)
{
    char seen[16][64];
    size_t nseen = 0;
    size_t i;
    ts_check_init(out);
    {
        const char *base = strrchr(f->rel, '/');
        char hit[64];
        ts_buf_t msg;
        base = base != NULL ? base + 1 : f->rel;
        if (ts_piece_borrowed(base, allow, nallow, hit, sizeof(hit))) {
            ts_buf_init(&msg);
            ts_buf_pushf(&msg,
                "file name carries other-project name \"%s\"", hit);
            ts_names_note(out, seen, &nseen, 1, msg.data, hit);
            ts_buf_free(&msg);
        }
    }
    if (!f->is_code)
        return;
    for (i = 0; i < idents->n; i++) {
        char hit[64];
        ts_buf_t msg;
        if (ts_reserved_name(idents->items[i].name))
            continue;
        if (!ts_piece_borrowed(idents->items[i].name, allow, nallow,
            hit, sizeof(hit)))
            continue;
        ts_buf_init(&msg);
        ts_buf_pushf(&msg,
            "identifier \"%s\" carries other-project name \"%s\"",
            idents->items[i].name, hit);
        ts_names_note(out, seen, &nseen, idents->items[i].line,
            msg.data, hit);
        ts_buf_free(&msg);
    }
}

/* ---- check 2: no third-party code ---- */

static void ts_third_apply(ts_check_t *out, const char *scantext,
    const char **markers, size_t nmarkers, const char *label)
{
    ts_hit_list_t hits;
    size_t i;
    ts_scan_markers(scantext, markers, nmarkers, &hits);
    for (i = 0; i < hits.n; i++) {
        ts_buf_t msg;
        ts_buf_init(&msg);
        ts_buf_pushf(&msg, "%s \"%s\"", label,
            markers[hits.items[i].marker]);
        ts_check_push(out, hits.items[i].line, msg.data);
        ts_check_deduct(out, 25);
        ts_buf_free(&msg);
    }
    ts_hit_list_free(&hits);
}

static void ts_check_third(ts_file_t *f, const char *owner, ts_check_t *out)
{
    char *scantext;
    ts_check_init(out);
    if (owner != NULL && owner[0] != '\0' &&
        ts_lower_contains(f->text, owner))
        return;
    if (f->is_code)
        scantext = ts_comment_text(f->text, f->len, ts_file_ext(f->path));
    else
        scantext = ts_dup(f->text);
    ts_third_apply(out, scantext, ts_license_markers, TS_LICENSE_COUNT,
        "third-party marker");
    ts_third_apply(out, scantext, ts_credit_phrases, TS_CREDIT_COUNT,
        "credit phrase");
    free(scantext);
}

/* ---- project-wide context ---- */

typedef struct {
    char *name;
    size_t file;
    int line;
} ts_symbol_t;

typedef struct {
    ts_symbol_t *items;
    size_t n;
    size_t cap;
} ts_symbol_list_t;

static void ts_symbol_push(ts_symbol_list_t *l, const char *name,
    size_t file, int line)
{
    ts_symbol_t s;
    if (l->n == l->cap) {
        size_t next = l->cap == 0 ? 64 : l->cap * 2;
        l->items = (ts_symbol_t *)ts_xrealloc(l->items,
            next * sizeof(ts_symbol_t));
        l->cap = next;
    }
    s.name = ts_dup(name);
    s.file = file;
    s.line = line;
    l->items[l->n] = s;
    l->n++;
}

static void ts_symbol_list_free(ts_symbol_list_t *l)
{
    size_t i;
    for (i = 0; i < l->n; i++)
        free(l->items[i].name);
    free(l->items);
    l->items = NULL;
    l->n = 0;
    l->cap = 0;
}

typedef struct {
    size_t file;
    int line;
    int empty_body;
    int const_body;
    int is_static;
    char *name;
} ts_defn_t;

typedef struct {
    ts_defn_t *items;
    size_t n;
    size_t cap;
} ts_defn_list_t;

static void ts_defn_push(ts_defn_list_t *l, ts_defn_t *d)
{
    if (l->n == l->cap) {
        size_t next = l->cap == 0 ? 64 : l->cap * 2;
        l->items = (ts_defn_t *)ts_xrealloc(l->items,
            next * sizeof(ts_defn_t));
        l->cap = next;
    }
    l->items[l->n] = *d;
    l->n++;
}

static void ts_defn_list_free(ts_defn_list_t *l)
{
    while (l->n > 0) {
        l->n--;
        free(l->items[l->n].name);
    }
    free(l->items);
    l->items = NULL;
    l->cap = 0;
}

/* Identifier immediately before position p (p points at '('). */
static int ts_ident_before(const char *text, size_t p, char *name,
    size_t cap, size_t *start)
{
    size_t e;
    size_t s;
    if (p == 0)
        return 0;
    e = p;
    if (!ts_is_ident_char((unsigned char)text[e - 1]))
        return 0;
    s = e;
    while (s > 0 && ts_is_ident_char((unsigned char)text[s - 1]))
        s--;
    if (!ts_is_ident_start((unsigned char)text[s]))
        return 0;
    if (e - s >= cap)
        return 0;
    memcpy(name, text + s, e - s);
    name[e - s] = '\0';
    if (start != NULL)
        *start = s;
    return 1;
}

static size_t ts_match_pair(const char *text, size_t len, size_t open,
    char open_ch, char close_ch)
{
    int depth = 0;
    size_t i;
    for (i = open; i < len; i++) {
        if (text[i] == open_ch)
            depth++;
        else if (text[i] == close_ch) {
            depth--;
            if (depth == 0)
                return i;
        }
    }
    return len;
}

static int ts_line_of(const char *text, size_t pos)
{
    int line = 1;
    size_t i;
    for (i = 0; i < pos; i++) {
        if (text[i] == '\n')
            line++;
    }
    return line;
}

/* Body between braces, whitespace removed, for the empty-body test. */
static void ts_body_shape(const char *text, size_t open, size_t close,
    char *shape, size_t cap)
{
    size_t i;
    size_t o = 0;
    for (i = open + 1; i < close && o + 1 < cap; i++) {
        char c = text[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
            continue;
        shape[o++] = c;
    }
    shape[o] = '\0';
}

static int ts_is_const_return(const char *shape)
{
    return strcmp(shape, "return0;") == 0 ||
        strcmp(shape, "return1;") == 0 ||
        strcmp(shape, "return-1;") == 0 ||
        strcmp(shape, "returnNULL;") == 0 ||
        strcmp(shape, "return;") == 0;
}

static int ts_line_has_static(const char *text, size_t pos)
{
    size_t s = pos;
    while (s > 0 && text[s - 1] != '\n')
        s--;
    {
        size_t n = pos - s;
        char *line = ts_dup_range(text + s, n);
        int found = strstr(line, "static") != NULL;
        free(line);
        return found;
    }
}

/*
 * Step through a cleaned file to the next call-shaped name: an identifier
 * directly before an opening paren, not a control word, with a matching
 * close paren. Reports the name, the paren position, and the close paren.
 */
static int ts_next_call(const char *clean, size_t len, size_t *at,
    char *name, size_t cap, size_t *open_at, size_t *close_at)
{
    size_t i = *at;
    while (i < len) {
        size_t close_paren;
        if (clean[i] != '(') {
            i++;
            continue;
        }
        if (!ts_ident_before(clean, i, name, cap, NULL)) {
            i++;
            continue;
        }
        if (ts_in_list(name, ts_control_words, TS_CONTROL_COUNT)) {
            i++;
            continue;
        }
        close_paren = ts_match_pair(clean, len, i, '(', ')');
        if (close_paren >= len) {
            i++;
            continue;
        }
        *open_at = i;
        *close_at = close_paren;
        *at = i + 1;
        return 1;
    }
    return 0;
}

/*
 * Collect definitions (when defs is set) or declarations (when decls is
 * set) from one cleaned file. One walk serves both shapes. A declaration
 * only counts at file scope: a call inside a function body is not a
 * prototype, and counting it would flag every library call in a header.
 */
static void ts_collect_calls(size_t fidx, const char *clean,
    ts_defn_list_t *defs, ts_symbol_list_t *decls)
{
    size_t len = strlen(clean);
    size_t at = 0;
    size_t open_at = 0;
    size_t close_at = 0;
    size_t scanned = 0;
    int depth = 0;
    char name[128];
    while (ts_next_call(clean, len, &at, name, sizeof(name), &open_at,
        &close_at)) {
        size_t j = close_at + 1;
        size_t s;
        for (s = scanned; s < open_at; s++) {
            if (clean[s] == '{')
                depth++;
            else if (clean[s] == '}')
                depth--;
        }
        if (depth < 0)
            depth = 0;
        scanned = open_at;
        if (defs != NULL) {
            while (j < len && (clean[j] == ' ' || clean[j] == '\t' ||
                clean[j] == '\n' || clean[j] == '\r'))
                j++;
            if (j < len && clean[j] == '{') {
                size_t close_brace = ts_match_pair(clean, len, j,
                    '{', '}');
                char shape[256];
                ts_defn_t d;
                ts_body_shape(clean, j, close_brace, shape,
                    sizeof(shape));
                d.file = fidx;
                d.line = ts_line_of(clean, open_at);
                d.empty_body = shape[0] == '\0';
                d.const_body = ts_is_const_return(shape);
                d.is_static = ts_line_has_static(clean, open_at);
                d.name = ts_dup(name);
                ts_defn_push(defs, &d);
                at = close_brace + 1;
            }
        } else {
            while (j < len && (clean[j] == ' ' || clean[j] == '\t'))
                j++;
            if (depth == 0 && j < len && clean[j] == ';') {
                size_t k = open_at;
                int preproc = 0;
                while (k > 0 && clean[k - 1] != '\n')
                    k--;
                if (clean[k] == '#')
                    preproc = 1;
                if (!preproc)
                    ts_symbol_push(decls, name, fidx,
                        ts_line_of(clean, open_at));
            }
        }
    }
}

/* ---- check 3: duplication window table ---- */

typedef struct {
    unsigned long long hash;
    size_t file;
    size_t start;
    int used;
} ts_window_slot_t;

typedef struct {
    ts_window_slot_t *slots;
    size_t cap;
    size_t n;
} ts_window_table_t;

static void ts_window_table_init(ts_window_table_t *t, size_t cap)
{
    t->slots = (ts_window_slot_t *)ts_xcalloc(cap,
        sizeof(ts_window_slot_t));
    t->cap = cap;
    t->n = 0;
}

static void ts_window_table_free(ts_window_table_t *t)
{
    free(t->slots);
    t->slots = NULL;
    t->cap = 0;
    t->n = 0;
}

static void ts_window_table_grow(ts_window_table_t *t,
    ts_normline_list_t *norms)
{
    ts_window_table_t next;
    size_t i;
    (void)norms;
    ts_window_table_init(&next, t->cap * 2);
    for (i = 0; i < t->cap; i++) {
        size_t at;
        if (!t->slots[i].used)
            continue;
        at = (size_t)(t->slots[i].hash % next.cap);
        while (next.slots[at].used)
            at = (at + 1) % next.cap;
        next.slots[at] = t->slots[i];
        next.n++;
    }
    free(t->slots);
    *t = next;
}

static int ts_window_lines_equal(ts_normline_list_t *norms, size_t f1,
    size_t s1, size_t f2, size_t s2)
{
    int k;
    for (k = 0; k < TS_DUP_WINDOW; k++) {
        if (strcmp(norms[f1].items[s1 + k].norm,
            norms[f2].items[s2 + k].norm) != 0)
            return 0;
    }
    return 1;
}

/*
 * Look up a window. Returns 1 and fills first-file/start when a verified
 * repeat is found; otherwise records the window and returns 0.
 */
static int ts_window_check(ts_window_table_t *t, unsigned long long hash,
    ts_normline_list_t *norms, size_t file, size_t start, size_t *first_file,
    size_t *first_start)
{
    size_t at;
    if ((t->n + 1) * 10 > t->cap * 7)
        ts_window_table_grow(t, norms);
    at = (size_t)(hash % t->cap);
    while (t->slots[at].used) {
        if (t->slots[at].hash == hash &&
            ts_window_lines_equal(norms, t->slots[at].file,
                t->slots[at].start, file, start)) {
            *first_file = t->slots[at].file;
            *first_start = t->slots[at].start;
            return 1;
        }
        at = (at + 1) % t->cap;
    }
    t->slots[at].hash = hash;
    t->slots[at].file = file;
    t->slots[at].start = start;
    t->slots[at].used = 1;
    t->n++;
    return 0;
}

/* ---- build and test ---- */

static void ts_shell_escape(const char *s, ts_buf_t *out)
{
    size_t i;
    ts_buf_pushc(out, '\'');
    for (i = 0; s[i] != '\0'; i++) {
        if (s[i] == '\'')
            ts_buf_push(out, "'\\''", 4);
        else
            ts_buf_pushc(out, s[i]);
    }
    ts_buf_pushc(out, '\'');
}

static int ts_run_logged(const char *root, const char *cmd,
    const char *logpath)
{
    ts_buf_t full;
    int rc;
    ts_buf_init(&full);
    ts_buf_push(&full, "cd ", 3);
    ts_shell_escape(root, &full);
    ts_buf_push(&full, " && ", 4);
    ts_buf_push(&full, cmd, strlen(cmd));
    ts_buf_push(&full, " > ", 3);
    ts_shell_escape(logpath, &full);
    ts_buf_push(&full, " 2>&1", 5);
    rc = system(full.data);
    ts_buf_free(&full);
    return rc == 0;
}

static int ts_path_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

/* Mark files whose basename appears on an error line in the build log. */
static void ts_mark_compile_failures(const char *logpath,
    ts_file_list_t *files, int *failed)
{
    FILE *fp = fopen(logpath, "rb");
    char line[1024];
    if (fp == NULL)
        return;
    while (fgets(line, sizeof(line), fp) != NULL) {
        char *err = strstr(line, "error");
        char *colon;
        char *base;
        size_t i;
        if (err == NULL)
            continue;
        colon = strchr(line, ':');
        if (colon == NULL || colon >= err)
            continue;
        *colon = '\0';
        base = strrchr(line, '/');
        base = base != NULL ? base + 1 : line;
        for (i = 0; i < files->n; i++) {
            const char *relbase = strrchr(files->items[i].rel, '/');
            relbase = relbase != NULL ? relbase + 1 : files->items[i].rel;
            if (strcmp(relbase, base) == 0)
                failed[i] = 1;
        }
    }
    fclose(fp);
}

/* ---- doc spans ---- */

static int ts_span_is_ident(const char *s, size_t n)
{
    size_t i;
    if (n == 0 || !ts_is_ident_start((unsigned char)s[0]))
        return 0;
    for (i = 1; i < n; i++) {
        if (!ts_is_ident_char((unsigned char)s[i]))
            return 0;
    }
    return 1;
}

/* ---- the audit ---- */

int ts_audit(ts_file_list_t *files, const char *root, const char *owner,
    const char **allow, size_t nallow, const char *build_cmd,
    const char *test_cmd, int run_build, ts_file_result_t *results,
    ts_project_result_t *proj)
{
    size_t n = files->n;
    size_t i;
    size_t j;
    ts_ident_list_t *all_idents;
    ts_normline_list_t *all_norms;
    ts_symbol_list_t decls;
    ts_defn_list_t defs;
    ts_window_table_t wtab;
    int *failed = NULL;
    char *root_base;
    double weighted = 0.0;
    long weight_total = 0;
    const char *slash = strrchr(root, '/');

    decls.items = NULL;
    decls.n = 0;
    decls.cap = 0;
    defs.items = NULL;
    defs.n = 0;
    defs.cap = 0;
    root_base = ts_dup(slash != NULL ? slash + 1 : root);
    if (root_base[0] == '\0') {
        free(root_base);
        root_base = ts_dup(root);
    }

    all_idents = (ts_ident_list_t *)ts_xcalloc(n == 0 ? 1 : n,
        sizeof(ts_ident_list_t));
    all_norms = (ts_normline_list_t *)ts_xcalloc(n == 0 ? 1 : n,
        sizeof(ts_normline_list_t));
    for (i = 0; i < n; i++) {
        char *clean = (char *)malloc(files->items[i].len + 1);
        const char *ext = ts_file_ext(files->items[i].path);
        char low[32];
        size_t en = strlen(ext);
        if (clean == NULL) {
            fprintf(stderr, "truesmith: out of memory\n");
            exit(2);
        }
        ts_strip_noise(files->items[i].text, files->items[i].len, ext,
            clean);
        ts_collect_idents(clean, files->items[i].len, &all_idents[i]);
        ts_collect_normlines(files->items[i].text, files->items[i].len,
            ext, &all_norms[i]);
        if (en > 0 && en < sizeof(low)) {
            ts_lower_copy(low, ext, en);
            if (strcmp(low, "h") == 0 || strcmp(low, "hpp") == 0)
                ts_collect_calls(i, clean, NULL, &decls);
            if (strcmp(low, "c") == 0 || strcmp(low, "cc") == 0 ||
                strcmp(low, "cpp") == 0 || strcmp(low, "js") == 0 ||
                strcmp(low, "mjs") == 0)
                ts_collect_calls(i, clean, &defs, NULL);
        }
        free(clean);
    }

    proj->score = 0.0;
    proj->files = n;
    proj->perfect = 0;
    proj->total_lines = 0;
    proj->build_ran = 0;
    proj->build_ok = 0;
    proj->test_ran = 0;
    proj->test_ok = 0;
    for (i = 0; i < n; i++)
        proj->total_lines += files->items[i].nblank;

    failed = (int *)ts_xcalloc(n == 0 ? 1 : n, sizeof(int));
    if (run_build) {
        ts_buf_t mk;
        ts_buf_init(&mk);
        ts_buf_pushf(&mk, "%s/Makefile", root);
        if (ts_path_exists(mk.data) || (build_cmd != NULL &&
            build_cmd[0] != '\0')) {
            char blog[256];
            char tlog[256];
            const char *bcmd = build_cmd;
            const char *tcmd = test_cmd;
            if (bcmd == NULL)
                bcmd = "make";
            if (tcmd == NULL)
                tcmd = "make test";
            snprintf(blog, sizeof(blog), "/tmp/truesmith-build-%ld.log",
                (long)getpid());
            snprintf(tlog, sizeof(tlog), "/tmp/truesmith-test-%ld.log",
                (long)getpid());
            proj->build_ran = 1;
            proj->build_ok = ts_run_logged(root, bcmd, blog);
            if (!proj->build_ok)
                ts_mark_compile_failures(blog, files, failed);
            proj->test_ran = 1;
            proj->test_ok = ts_run_logged(root, tcmd, tlog);
            remove(blog);
            remove(tlog);
        }
        ts_buf_free(&mk);
    }

    ts_window_table_init(&wtab, 1024);
    for (i = 0; i < n; i++) {
        ts_check_init(&results[i].names);
        ts_check_init(&results[i].third);
        ts_check_init(&results[i].dup);
        ts_check_init(&results[i].real);
        ts_check_init(&results[i].docs);
        results[i].score = 0.0;
    }

    for (i = 0; i < n; i++) {
        ts_file_t *f = &files->items[i];
        ts_check_names(f, &all_idents[i], allow, nallow,
            &results[i].names);
        ts_check_third(f, owner, &results[i].third);
    }

    for (i = 0; i < n; i++) {
        ts_normline_list_t *nl = &all_norms[i];
        size_t s;
        if (nl->n < TS_DUP_WINDOW)
            continue;
        for (s = 0; s + TS_DUP_WINDOW <= nl->n; s++) {
            unsigned long long h = 0x243f6a8885a308d3ULL;
            size_t ff = 0;
            size_t fs = 0;
            int k;
            for (k = 0; k < TS_DUP_WINDOW; k++) {
                const char *line = nl->items[s + k].norm;
                h = ts_mix_step(h, ts_mix_hash(line, strlen(line)));
            }
            if (ts_window_check(&wtab, h, all_norms, i, s, &ff, &fs)) {
                ts_buf_t msg;
                ts_buf_init(&msg);
                ts_buf_pushf(&msg,
                    "repeated block of %d lines, first seen in %s:%d",
                    TS_DUP_WINDOW, files->items[ff].rel,
                    all_norms[ff].items[fs].line);
                ts_check_push(&results[i].dup, nl->items[s].line,
                    msg.data);
                ts_check_deduct(&results[i].dup, 10);
                ts_buf_free(&msg);
            }
        }
    }
    ts_window_table_free(&wtab);

    for (i = 0; i < n; i++) {
        ts_file_t *f = &files->items[i];
        char *scantext;
        ts_hit_list_t hits;
        ts_check_t *real = &results[i].real;
        if (f->is_code)
            scantext = ts_comment_text(f->text, f->len,
                ts_file_ext(f->path));
        else
            scantext = ts_dup(f->text);
        ts_scan_markers(scantext, ts_unfinished_markers,
            TS_UNFINISHED_COUNT, &hits);
        for (j = 0; j < hits.n; j++) {
            ts_buf_t msg;
            ts_buf_init(&msg);
            ts_buf_pushf(&msg, "unfinished-work marker \"%s\"",
                ts_unfinished_markers[hits.items[j].marker]);
            ts_check_push(real, hits.items[j].line, msg.data);
            ts_check_deduct(real, 15);
            ts_buf_free(&msg);
        }
        ts_hit_list_free(&hits);
        free(scantext);
        if (failed[i]) {
            ts_check_push(real, 1, "file fails to compile");
            real->score = 0;
        }
    }

    for (i = 0; i < defs.n; i++) {
        ts_defn_t *d = &defs.items[i];
        ts_check_t *real = &results[d->file].real;
        if (d->empty_body || d->const_body) {
            ts_buf_t msg;
            ts_buf_init(&msg);
            ts_buf_pushf(&msg, "function \"%s\" has no working body",
                d->name);
            ts_check_push(real, d->line, msg.data);
            ts_check_deduct(real, 15);
            ts_buf_free(&msg);
        }
        if (d->is_static) {
            size_t refs = 0;
            size_t fi;
            size_t ii;
            for (fi = 0; fi < n && refs < 2; fi++) {
                for (ii = 0; ii < all_idents[fi].n; ii++) {
                    if (strcmp(all_idents[fi].items[ii].name,
                        d->name) == 0)
                        refs++;
                    if (refs >= 2)
                        break;
                }
            }
            if (refs < 2) {
                ts_buf_t msg;
                ts_buf_init(&msg);
                ts_buf_pushf(&msg,
                    "file-local function \"%s\" is never called",
                    d->name);
                ts_check_push(real, d->line, msg.data);
                ts_check_deduct(real, 15);
                ts_buf_free(&msg);
            }
        }
    }

    for (i = 0; i < decls.n; i++) {
        int found = 0;
        for (j = 0; j < defs.n; j++) {
            if (strcmp(defs.items[j].name, decls.items[i].name) == 0) {
                found = 1;
                break;
            }
        }
        if (!found) {
            ts_buf_t msg;
            ts_check_t *docs = &results[decls.items[i].file].docs;
            ts_buf_init(&msg);
            ts_buf_pushf(&msg,
                "declared \"%s\" has no definition anywhere",
                decls.items[i].name);
            ts_check_push(docs, decls.items[i].line, msg.data);
            ts_check_deduct(docs, 20);
            ts_buf_free(&msg);
        }
    }

    for (i = 0; i < n; i++) {
        ts_file_t *f = &files->items[i];
        const char *p;
        if (!f->is_doc)
            continue;
        p = f->text;
        while (*p != '\0') {
            const char *open = strchr(p, '`');
            const char *close;
            size_t slen;
            if (open == NULL)
                break;
            close = strchr(open + 1, '`');
            if (close == NULL)
                break;
            slen = (size_t)(close - (open + 1));
            if (ts_span_is_ident(open + 1, slen)) {
                char *span = ts_dup_range(open + 1, slen);
                int known = 0;
                size_t fi;
                size_t ii;
                if (ts_lower_eq(span, root_base))
                    known = 1;
                if (!known && ts_in_list(span, ts_common_files,
                    TS_COMMON_COUNT))
                    known = 1;
                for (fi = 0; fi < n && !known; fi++) {
                    const char *rel = files->items[fi].rel;
                    const char *seg = rel;
                    for (ii = 0; ii < all_idents[fi].n; ii++) {
                        if (strcmp(all_idents[fi].items[ii].name,
                            span) == 0) {
                            known = 1;
                            break;
                        }
                    }
                    while (!known && *seg != '\0') {
                        const char *slash = strchr(seg, '/');
                        size_t seglen = slash != NULL ?
                            (size_t)(slash - seg) : strlen(seg);
                        char *part = ts_dup_range(seg, seglen);
                        if (strcmp(part, span) == 0)
                            known = 1;
                        free(part);
                        if (slash == NULL)
                            break;
                        seg = slash + 1;
                    }
                }
                if (!known) {
                    ts_buf_t msg;
                    ts_check_t *docs = &results[i].docs;
                    ts_buf_init(&msg);
                    ts_buf_pushf(&msg,
                        "document names \"%s\", which exists nowhere",
                        span);
                    ts_check_push(docs, ts_line_of(f->text,
                        (size_t)(open - f->text)), msg.data);
                    ts_check_deduct(docs, 20);
                    ts_buf_free(&msg);
                }
                free(span);
            }
            p = close + 1;
        }
    }

    for (i = 0; i < n; i++) {
        double total = (double)(TS_W_NAMES + TS_W_THIRD + TS_W_DUP +
            TS_W_REAL + TS_W_DOCS);
        double sum = (double)results[i].names.score * TS_W_NAMES +
            (double)results[i].third.score * TS_W_THIRD +
            (double)results[i].dup.score * TS_W_DUP +
            (double)results[i].real.score * TS_W_REAL +
            (double)results[i].docs.score * TS_W_DOCS;
        results[i].score = sum / total;
        if (results[i].score >= 100.0)
            proj->perfect++;
        weighted += results[i].score * (double)files->items[i].nblank;
        weight_total += files->items[i].nblank;
    }
    if (weight_total > 0)
        proj->score = weighted / (double)weight_total;
    else if (n > 0)
        proj->score = 0.0;
    else
        proj->score = -1.0;

    for (i = 0; i < n; i++) {
        ts_ident_list_free(&all_idents[i]);
        ts_normline_list_free(&all_norms[i]);
    }
    free(all_idents);
    free(all_norms);
    free(failed);
    free(root_base);
    ts_symbol_list_free(&decls);
    ts_defn_list_free(&defs);
    return 0;
}
