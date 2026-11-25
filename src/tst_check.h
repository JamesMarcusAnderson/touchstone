/* Touchstone checks: five evidence checks plus weighted scoring. */
#ifndef TOUCHSTONE_CHECK_H
#define TOUCHSTONE_CHECK_H

#include "tst_scan.h"

typedef struct {
    int line;
    char *text;
} tst_finding_t;

typedef struct {
    int score;
    tst_finding_t *items;
    size_t n;
    size_t cap;
} tst_check_t;

typedef struct {
    tst_check_t names;
    tst_check_t third;
    tst_check_t dup;
    tst_check_t real;
    tst_check_t docs;
    double score;
} tst_file_result_t;

typedef struct {
    double score;
    size_t files;
    size_t perfect;
    long total_lines;
    int build_ran;
    int build_ok;
    int test_ran;
    int test_ok;
} tst_project_result_t;

void tst_check_free(tst_check_t *c);
void tst_file_result_free(tst_file_result_t *r);

int tst_audit(tst_file_list_t *files, const char *root, const char *owner,
    const char **allow, size_t nallow, const char *build_cmd,
    const char *test_cmd, int run_build, tst_file_result_t *results,
    tst_project_result_t *proj);

#endif
