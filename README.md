# TrueSmith

TrueSmith audits a codebase file by file and reports, for each file and
for the whole project, a percentage of true original creative and real
work. Every percentage comes with its per-check breakdown, so the number
is auditable, not magic.

## What it checks

Each file is scored on five checks:

1. Original names (weight 25). File names and identifiers must be the
   project's own. Names that belong to another project or technology are
   flagged. The only exceptions are literal interface names the code must
   speak to reach a driver or read a format, and each one must be listed
   in the allowlist with its reason. Words too common to be a reliable
   signal stay out of the name list rather than cry wolf. Names the
   code does not choose never count: identifiers starting with an
   underscore (the implementation's namespace) and posix_ API names.
2. No third-party code (weight 25). License headers, ownership notices
   that name someone other than the project owner, and credit language
   that names another project as the source are flagged. A format name
   on its own is not a license claim, and a bare mention of copyright
   in prose is not a notice: the word only counts with (c), the
   copyright sign, the reserved-rights phrase, or a year right
   next to it.
3. No internal duplication (weight 20). Blocks of six or more lines that
   appear more than once in the project are flagged, with the location of
   each repeat.
4. Real work (weight 20). Unfinished-work markers, empty function
   bodies, file-local functions that are never called, and files that
   fail to compile are flagged. Where the project has a build, the tool
   runs it and records the result.
5. Docs match code (weight 10). Functions declared in a header must have
   a definition somewhere, and code names mentioned in docs must exist.
   Only file-scope prototypes count as declarations: a call inside a
   function body is not a prototype.

## Scoring

A file score is the weighted sum of its five check scores. A project
score is the lines-weighted mean of its file scores. A check starts at
100 and loses points per finding: 20 per borrowed name, 25 per
third-party marker or credit phrase, 10 per repeated block, 15 per
unfinished marker or dead function, 20 per doc mismatch. A file that
fails to compile scores 0 on the real-work check.

## Use

Build with make, then run:

    truesmith <dir> [options]

Options:

- --json <file>: also write a machine-readable report.
- --allowlist <file>: interface names the code must speak, with reasons.
- --owner <name>: project owner for the ownership check.
- --build-cmd <cmd> / --test-cmd <cmd>: commands for the working-code check.
- --ext <a,b,c>: file extensions to audit.
- --exclude <dir>: skip a path under the audit root.
- --no-build: skip the build and test run.
- --self-check: require the baked self-score to be 100.
- --quiet: summary plus files below 100 only.

The exit code is 0 only when every audited file scores 100. Excluded
paths are printed in every report, so an audit scope is never hidden.

## Allowlist

The allowlist file is named truesmith-allowlist.txt and lives in the
audit root, or is passed with --allowlist. One entry per line: the name,
then the reason it must stay. Entries without a reason are ignored, and
the file itself is never audited.

## Self-integrity

TrueSmith audits itself with the same logic it applies to everything
else. The build runs the tool on its own source and fails unless the
score is 100 on every file. The score, build time, and a fingerprint of
the source are stamped into the binary at build time, and every report
prints them. The continuous-integration job builds the tool, runs the
tests, and runs the self-audit; a version that fails its own audit never
ships.

## Honesty limits

TrueSmith reports evidence, not proof. It cannot prove no copying ever
happened anywhere on the internet. Its name list is finite. Its
duplication check reads code shape, not meaning, so rewritten copies can
pass it. Its build check runs the project's own build and test commands
and reports what they return. A 100% score means every check passed on
the evidence available, not that the work is proven original against the
whole world. Anything the tool cannot check is reported as not checked,
never as passed.

## Building

Pure C11, no dependencies beyond the C library and the directory
interface the platform provides for walking a tree. Build with make.
Run the tests with make test.

## License

TrueSmith License — see `LICENSE`. Anyone may use, copy, modify, and
share this software for any purpose, provided the notice stays with it.
