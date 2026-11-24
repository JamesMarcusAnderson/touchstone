/* TrueSmith checks: five evidence checks plus weighted scoring. */
#ifndef TRUESMITH_CHECK_H
#define TRUESMITH_CHECK_H

#include "ts_scan.h"

typedef struct {
    int line;
    char *text;
} ts_finding_t;

typedef struct {
    int score;
    ts_finding_t *items;
    size_t n;
    size_t cap;
} ts_check_t;

typedef struct {
    ts_check_t names;
    ts_check_t third;
    ts_check_t dup;
    ts_check_t real;
    ts_check_t docs;
    double score;
} ts_file_result_t;

typedef struct {
    double score;
    size_t files;
    size_t perfect;
    long total_lines;
    int build_ran;
    int build_ok;
    int test_ran;
    int test_ok;
} ts_project_result_t;

void ts_check_free(ts_check_t *c);
void ts_file_result_free(ts_file_result_t *r);

int ts_audit(ts_file_list_t *files, const char *root, const char *owner,
    const char **allow, size_t nallow, const char *build_cmd,
    const char *test_cmd, int run_build, ts_file_result_t *results,
    ts_project_result_t *proj);

#endif
