/* Touchstone: authenticity auditor. Audits code for original work. */
#include "tst_check.h"
#include "tst_scan.h"
#include "tst_util.h"
#include "touchstone_selfscore.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TOUCHSTONE_VERSION "0.1.0"
#define TOUCHSTONE_LIMITS "Evidence-based signals only. This tool reports what it can check: names, markers, duplication, working code, and doc agreement. It cannot prove no copying ever happened anywhere. A 100% score means every check passed on the evidence available, not that the work is proven original against the whole world."

static const char *tst_default_exts[] = {
    "c", "h", "cc", "cpp", "hpp", "js", "mjs", "css", "py", "sh",
    "md", "json", "txt", "html"
};
#define TST_DEFAULT_EXT_COUNT \
    (sizeof(tst_default_exts) / sizeof(const char *))

typedef struct {
    const char *root;
    const char *json_out;
    const char *allow_path;
    const char *owner;
    const char *build_cmd;
    const char *test_cmd;
    const char **exts;
    size_t nexts;
    const char **excludes;
    size_t nexcludes;
    int run_build;
    int self_check;
    int quiet;
} tst_options_t;

static void tst_usage(void)
{
    printf("Touchstone %s - authenticity auditor\n", TOUCHSTONE_VERSION);
    printf("Usage: touchstone <dir> [options]\n");
    printf("Options:\n");
    printf("  --json <file>       write machine-readable report\n");
    printf("  --allowlist <file>  names the code must speak, with reasons\n");
    printf("  --owner <name>      project owner for copyright checks\n");
    printf("  --build-cmd <cmd>   build command for the working-code check\n");
    printf("  --test-cmd <cmd>    test command for the working-code check\n");
    printf("  --ext <a,b,c>       file extensions to audit\n");
    printf("  --exclude <dir>     skip a path under the audit root\n");
    printf("  --no-build          skip the build and test run\n");
    printf("  --self-check        require the baked self-score to be 100\n");
    printf("  --quiet             summary plus files below 100 only\n");
    printf("  --help              show this text\n");
    printf("  --version           show the version\n");
    printf("Exit code is 0 only when every audited file scores 100.\n");
}

static void tst_version(void)
{
    printf("touchstone %s\n", TOUCHSTONE_VERSION);
}

static char **tst_split_list(const char *s, size_t *count)
{
    char **out = NULL;
    size_t n = 0;
    size_t cap = 0;
    const char *p = s;
    *count = 0;
    while (*p != '\0') {
        const char *comma = strchr(p, ',');
        size_t len = comma != NULL ? (size_t)(comma - p) : strlen(p);
        while (len > 0 && (p[0] == ' ' || p[0] == '\t')) {
            p++;
            len--;
        }
        while (len > 0 && (p[len - 1] == ' ' || p[len - 1] == '\t'))
            len--;
        if (len > 0) {
            if (n == cap) {
                size_t next = cap == 0 ? 8 : cap * 2;
                out = (char **)tst_xrealloc(out, next * sizeof(char *));
                cap = next;
            }
            out[n] = tst_dup_range(p, len);
            n++;
        }
        if (comma == NULL)
            break;
        p = comma + 1;
    }
    *count = n;
    return out;
}

static void tst_free_list(char **list, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++)
        free(list[i]);
    free(list);
}

/* Load an allowlist: first word is the name, the rest is the reason. */
static char **tst_load_allowlist(const char *path, size_t *count,
    const char *root, char **resolved)
{
    FILE *fp;
    char line[512];
    char **names = NULL;
    size_t n = 0;
    size_t cap = 0;
    *count = 0;
    *resolved = NULL;
    if (path == NULL) {
        tst_buf_t def;
        tst_buf_init(&def);
        tst_buf_pushf(&def, "%s/touchstone-allowlist.txt", root);
        fp = fopen(def.data, "rb");
        if (fp == NULL) {
            tst_buf_free(&def);
            return NULL;
        }
        *resolved = tst_dup(def.data);
        tst_buf_free(&def);
    } else {
        fp = fopen(path, "rb");
        if (fp == NULL) {
            fprintf(stderr, "touchstone: cannot open allowlist %s\n",
                path);
            return NULL;
        }
        *resolved = tst_dup(path);
    }
    while (fgets(line, sizeof(line), fp) != NULL) {
        char *p = line;
        char *name;
        size_t nlen;
        while (*p == ' ' || *p == '\t')
            p++;
        if (*p == '\0' || *p == '\n' || *p == '#')
            continue;
        name = p;
        while (*p != '\0' && *p != ' ' && *p != '\t' && *p != '\n')
            p++;
        nlen = (size_t)(p - name);
        while (*p == ' ' || *p == '\t')
            p++;
        if (*p == '\0' || *p == '\n') {
            fprintf(stderr,
                "touchstone: allowlist entry \"%.*s\" has no reason; ignored\n",
                (int)nlen, name);
            continue;
        }
        if (n == cap) {
            size_t next = cap == 0 ? 16 : cap * 2;
            names = (char **)tst_xrealloc(names, next * sizeof(char *));
            cap = next;
        }
        names[n] = tst_dup_range(name, nlen);
        n++;
    }
    fclose(fp);
    *count = n;
    return names;
}

static void tst_json_escape(FILE *fp, const char *s)
{
    size_t i;
    for (i = 0; s[i] != '\0'; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '"')
            fputs("\\\"", fp);
        else if (c == '\\')
            fputs("\\\\", fp);
        else if (c == '\n')
            fputs("\\n", fp);
        else if (c == '\t')
            fputs("\\t", fp);
        else if (c < 0x20)
            fprintf(fp, "\\u%04x", c);
        else
            fputc(c, fp);
    }
}

static void tst_report_check_text(FILE *fp, const char *label, int weight,
    tst_check_t *c)
{
    size_t i;
    fprintf(fp, "  %s %d (weight %d)", label, c->score, weight);
    if (c->n == 0) {
        fputc('\n', fp);
        return;
    }
    fputc('\n', fp);
    for (i = 0; i < c->n; i++)
        fprintf(fp, "    line %d: %s\n", c->items[i].line,
            c->items[i].text);
}

static void tst_report_text(FILE *fp, const char *root,
    tst_file_list_t *files, tst_file_result_t *results,
    tst_project_result_t *proj, int quiet, const char **excludes,
    size_t nexcludes)
{
    size_t i;
    fprintf(fp, "Touchstone authenticity audit\n");
    fprintf(fp, "Project: %s\n", root);
    fprintf(fp, "Files audited: %lu (%ld lines)\n",
        (unsigned long)proj->files, proj->total_lines);
    if (proj->score < 0.0)
        fprintf(fp, "Project score: n/a (no files)\n");
    else
        fprintf(fp, "Project score: %.1f%%\n", proj->score);
    fprintf(fp, "Files at 100%%: %lu of %lu\n",
        (unsigned long)proj->perfect, (unsigned long)proj->files);
    fprintf(fp, "Build: %s\n", proj->build_ran ?
        (proj->build_ok ? "passed" : "failed") : "not run");
    fprintf(fp, "Tests: %s\n", proj->test_ran ?
        (proj->test_ok ? "passed" : "failed") : "not run");
    if (nexcludes > 0) {
        fprintf(fp, "Excluded:");
        for (i = 0; i < nexcludes; i++)
            fprintf(fp, " %s", excludes[i]);
        fputc('\n', fp);
    }
    fputc('\n', fp);
    for (i = 0; i < files->n; i++) {
        if (quiet && results[i].score >= 100.0)
            continue;
        fprintf(fp, "%s: %.1f%%\n", files->items[i].rel,
            results[i].score);
        if (quiet || results[i].score >= 100.0)
            continue;
        tst_report_check_text(fp, "names", 25, &results[i].names);
        tst_report_check_text(fp, "third-party", 25, &results[i].third);
        tst_report_check_text(fp, "duplication", 20, &results[i].dup);
        tst_report_check_text(fp, "real-work", 20, &results[i].real);
        tst_report_check_text(fp, "docs", 10, &results[i].docs);
    }
    fprintf(fp, "\nTouchstone self-integrity: %s%% (verified at build, %s, source %s)\n",
        TOUCHSTONE_SELF_SCORE, TOUCHSTONE_SELF_BUILT,
        TOUCHSTONE_SELF_FINGERPRINT);
    fprintf(fp, "%s\n", TOUCHSTONE_LIMITS);
}

static void tst_report_check_json(FILE *fp, const char *label, int weight,
    tst_check_t *c)
{
    size_t i;
    fprintf(fp, "        \"%s\": {\"score\": %d, \"weight\": %d, \"findings\": [",
        label, c->score, weight);
    for (i = 0; i < c->n; i++) {
        if (i > 0)
            fputs(", ", fp);
        fprintf(fp, "{\"line\": %d, \"text\": \"", c->items[i].line);
        tst_json_escape(fp, c->items[i].text);
        fputs("\"}", fp);
    }
    fputs("]}", fp);
}

static void tst_report_json(FILE *fp, const char *root,
    tst_file_list_t *files, tst_file_result_t *results,
    tst_project_result_t *proj, const char **excludes, size_t nexcludes)
{
    size_t i;
    fputs("{\n", fp);
    fprintf(fp, "  \"tool\": \"touchstone\",\n");
    fprintf(fp, "  \"version\": \"%s\",\n", TOUCHSTONE_VERSION);
    fputs("  \"project\": \"", fp);
    tst_json_escape(fp, root);
    fputs("\",\n", fp);
    fputs("  \"excluded\": [", fp);
    for (i = 0; i < nexcludes; i++) {
        if (i > 0)
            fputs(", ", fp);
        fputc('"', fp);
        tst_json_escape(fp, excludes[i]);
        fputc('"', fp);
    }
    fputs("],\n", fp);
    fprintf(fp, "  \"files_audited\": %lu,\n",
        (unsigned long)proj->files);
    fprintf(fp, "  \"total_lines\": %ld,\n", proj->total_lines);
    if (proj->score < 0.0)
        fputs("  \"project_score\": null,\n", fp);
    else
        fprintf(fp, "  \"project_score\": %.1f,\n", proj->score);
    fprintf(fp, "  \"files_perfect\": %lu,\n",
        (unsigned long)proj->perfect);
    fprintf(fp, "  \"build\": {\"ran\": %s, \"ok\": %s},\n",
        proj->build_ran ? "true" : "false",
        proj->build_ok ? "true" : "false");
    fprintf(fp, "  \"tests\": {\"ran\": %s, \"ok\": %s},\n",
        proj->test_ran ? "true" : "false",
        proj->test_ok ? "true" : "false");
    fputs("  \"files\": [", fp);
    for (i = 0; i < files->n; i++) {
        if (i > 0)
            fputs(",", fp);
        fputs("\n    {\"path\": \"", fp);
        tst_json_escape(fp, files->items[i].rel);
        fprintf(fp, "\", \"lines\": %ld, \"score\": %.1f,\n",
            files->items[i].nblank, results[i].score);
        fputs("     \"checks\": {\n", fp);
        tst_report_check_json(fp, "names", 25, &results[i].names);
        fputs(",\n", fp);
        tst_report_check_json(fp, "third-party", 25, &results[i].third);
        fputs(",\n", fp);
        tst_report_check_json(fp, "duplication", 20, &results[i].dup);
        fputs(",\n", fp);
        tst_report_check_json(fp, "real-work", 20, &results[i].real);
        fputs(",\n", fp);
        tst_report_check_json(fp, "docs", 10, &results[i].docs);
        fputs("\n     }}", fp);
    }
    if (files->n > 0)
        fputs("\n  ", fp);
    fputs("],\n", fp);
    fprintf(fp, "  \"auditor_self_score\": \"%s\",\n",
        TOUCHSTONE_SELF_SCORE);
    fprintf(fp, "  \"auditor_self_built\": \"%s\",\n",
        TOUCHSTONE_SELF_BUILT);
    fprintf(fp, "  \"auditor_self_fingerprint\": \"%s\",\n",
        TOUCHSTONE_SELF_FINGERPRINT);
    fputs("  \"limits\": \"", fp);
    tst_json_escape(fp, TOUCHSTONE_LIMITS);
    fputs("\"\n}\n", fp);
}

static int tst_parse_args(int argc, char **argv, tst_options_t *o,
    char ***ext_list, size_t *ext_count,
    char ***excl_list, size_t *excl_count)
{
    int i;
    o->root = NULL;
    o->json_out = NULL;
    o->allow_path = NULL;
    o->owner = "James Marcus Anderson";
    o->build_cmd = NULL;
    o->test_cmd = NULL;
    o->exts = tst_default_exts;
    o->nexts = TST_DEFAULT_EXT_COUNT;
    o->excludes = NULL;
    o->nexcludes = 0;
    o->run_build = 1;
    o->self_check = 0;
    o->quiet = 0;
    *ext_list = NULL;
    *ext_count = 0;
    *excl_list = NULL;
    *excl_count = 0;
    for (i = 1; i < argc; i++) {
        char *a = argv[i];
        if (strcmp(a, "--help") == 0) {
            tst_usage();
            exit(0);
        }
        if (strcmp(a, "--version") == 0) {
            tst_version();
            exit(0);
        }
        if (strcmp(a, "--quiet") == 0) {
            o->quiet = 1;
            continue;
        }
        if (strcmp(a, "--no-build") == 0) {
            o->run_build = 0;
            continue;
        }
        if (strcmp(a, "--self-check") == 0) {
            o->self_check = 1;
            continue;
        }
        if (strcmp(a, "--json") == 0 && i + 1 < argc) {
            o->json_out = argv[++i];
            continue;
        }
        if (strcmp(a, "--allowlist") == 0 && i + 1 < argc) {
            o->allow_path = argv[++i];
            continue;
        }
        if (strcmp(a, "--owner") == 0 && i + 1 < argc) {
            o->owner = argv[++i];
            continue;
        }
        if (strcmp(a, "--build-cmd") == 0 && i + 1 < argc) {
            o->build_cmd = argv[++i];
            continue;
        }
        if (strcmp(a, "--test-cmd") == 0 && i + 1 < argc) {
            o->test_cmd = argv[++i];
            continue;
        }
        if (strcmp(a, "--ext") == 0 && i + 1 < argc) {
            *ext_list = tst_split_list(argv[++i], ext_count);
            o->exts = (const char **)*ext_list;
            o->nexts = *ext_count;
            continue;
        }
        if (strcmp(a, "--exclude") == 0 && i + 1 < argc) {
            char **one = tst_split_list(argv[++i], excl_count);
            *excl_list = one;
            o->excludes = (const char **)one;
            o->nexcludes = *excl_count;
            continue;
        }
        if (a[0] == '-') {
            fprintf(stderr, "touchstone: unknown option %s\n", a);
            return -1;
        }
        if (o->root != NULL) {
            fprintf(stderr, "touchstone: only one directory allowed\n");
            return -1;
        }
        o->root = a;
    }
    if (o->root == NULL) {
        tst_usage();
        return -1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    tst_options_t o;
    char **ext_list = NULL;
    size_t ext_count = 0;
    char **excl_list = NULL;
    size_t excl_count = 0;
    char **allow = NULL;
    size_t nallow = 0;
    char *allow_resolved = NULL;
    tst_file_list_t files;
    tst_file_result_t *results = NULL;
    tst_project_result_t proj;
    size_t i;
    int rc = 0;

    if (tst_parse_args(argc, argv, &o, &ext_list, &ext_count,
        &excl_list, &excl_count) != 0)
        return 2;
    allow = tst_load_allowlist(o.allow_path, &nallow, o.root,
        &allow_resolved);
    if (tst_walk(o.root, o.exts, o.nexts, o.excludes, o.nexcludes,
        allow_resolved, &files) != 0) {
        fprintf(stderr, "touchstone: cannot open directory %s\n", o.root);
        return 2;
    }
    if (files.n == 0) {
        printf("Touchstone authenticity audit\n");
        printf("Project: %s\n", o.root);
        printf("Files audited: 0\n");
        printf("Project score: n/a (no files)\n");
        printf("\nTouchstone self-integrity: %s%% (verified at build, %s, source %s)\n",
            TOUCHSTONE_SELF_SCORE, TOUCHSTONE_SELF_BUILT,
            TOUCHSTONE_SELF_FINGERPRINT);
        printf("%s\n", TOUCHSTONE_LIMITS);
        tst_file_list_free(&files);
        return 1;
    }
    results = (tst_file_result_t *)tst_xcalloc(files.n,
        sizeof(tst_file_result_t));
    tst_audit(&files, o.root, o.owner, (const char **)allow, nallow,
        o.build_cmd, o.test_cmd, o.run_build, results, &proj);
    tst_report_text(stdout, o.root, &files, results, &proj, o.quiet,
        o.excludes, o.nexcludes);
    if (o.json_out != NULL) {
        FILE *fp = fopen(o.json_out, "wb");
        if (fp == NULL) {
            fprintf(stderr, "touchstone: cannot write %s\n", o.json_out);
            rc = 2;
        } else {
            tst_report_json(fp, o.root, &files, results, &proj,
                o.excludes, o.nexcludes);
            fclose(fp);
        }
    }
    if (proj.perfect != proj.files)
        rc = 1;
    if (o.self_check && strcmp(TOUCHSTONE_SELF_SCORE, "100.0") != 0) {
        fprintf(stderr,
            "touchstone: self-check failed: baked self-score is %s, not 100.0\n",
            TOUCHSTONE_SELF_SCORE);
        rc = 1;
    }
    for (i = 0; i < files.n; i++)
        tst_file_result_free(&results[i]);
    free(results);
    tst_file_list_free(&files);
    tst_free_list(allow, nallow);
    free(allow_resolved);
    tst_free_list(ext_list, ext_count);
    tst_free_list(excl_list, excl_count);
    return rc;
}
