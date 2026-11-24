# Test fixtures

These files exist only so the test suite can check the auditor against
known-good and known-bad input. They are not part of the tool, and the
self-audit skips this directory.

- clean.c: a clean file; every check passes.
- borrowed.c: a file with a name that belongs to another project.
- credited.c: a file whose comments name another project as their source.
- doubled.c: a file with the same block written twice.
- stubbed.c: a file with unfinished work inside.
- page.html: a plain page; every check passes.
- formats.c: mentions a format name and copyright in prose; both pass.
- licensed.c: carries a real third-party license header; it fails.
- calls.h: an inline helper calls a library function; the call is not
  a declaration, and the prototype it declares is defined in licensed.c.
- platform.c: platform macros and a platform API call; none are ours.
