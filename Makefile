# TrueSmith build. Pure C11, no dependencies.
CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra
SRC = src/main.c src/ts_util.c src/ts_scan.c src/ts_check.c
BIN = truesmith
HDR = src/truesmith_selfscore.h

.PHONY: all clean test selfcheck selfscore

all: $(BIN)

$(BIN): $(SRC) src/ts_util.h src/ts_scan.h src/ts_check.h $(HDR)
	$(CC) $(CFLAGS) -o $(BIN) $(SRC)

# One-command self-audit of this repo. Fails unless every file is 100.
selfcheck: $(BIN)
	./$(BIN) . --exclude tests/corpus --no-build --quiet

# Build, prove the current source is 100, stamp the score into the
# binary, rebuild, and confirm the shipped binary still passes itself.
selfscore: $(BIN)
	./$(BIN) . --exclude tests/corpus --no-build --quiet
	@FPRINT=`(sha256sum src/*.c src/*.h 2>/dev/null | sha256sum | cut -d' ' -f1) || echo unknown`; \
	STAMP=`date -u +%Y-%m-%dT%H:%M:%SZ`; \
	printf '/* TrueSmith self-score, written by the Makefile at build time. */\n#ifndef TRUESMITH_SELFSCORE_H\n#define TRUESMITH_SELFSCORE_H\n\n#define TRUESMITH_SELF_SCORE "100.0"\n#define TRUESMITH_SELF_BUILT "%s"\n#define TRUESMITH_SELF_FINGERPRINT "%s"\n\n#endif\n' "$$STAMP" "$$FPRINT" > $(HDR)
	$(CC) $(CFLAGS) -o $(BIN) $(SRC)
	./$(BIN) . --exclude tests/corpus --no-build --quiet --self-check

test: selfscore
	@./$(BIN) tests/corpus --no-build --quiet --json /tmp/truesmith-corpus.json; test $$? -eq 1
	@grep -q '"path": "clean.c", "lines": 8, "score": 100.0' /tmp/truesmith-corpus.json
	@grep -q '"path": "borrowed.c", "lines": 8, "score": 95.0' /tmp/truesmith-corpus.json
	@grep -q '"path": "credited.c", "lines": 10, "score": 87.5' /tmp/truesmith-corpus.json
	@grep -q '"path": "doubled.c", "lines": 16, "score": 94.0' /tmp/truesmith-corpus.json
	@grep -q '"path": "stubbed.c", "lines": 8, "score": 94.0' /tmp/truesmith-corpus.json
	@grep -q '"path": "page.html", "lines": 10, "score": 100.0' /tmp/truesmith-corpus.json
	@grep -q '"path": "formats.c", "lines": 10, "score": 100.0' /tmp/truesmith-corpus.json
	@grep -q '"path": "licensed.c", "lines": 7, "score": 87.5' /tmp/truesmith-corpus.json
	@grep -q '"path": "calls.h", "lines": 11, "score": 100.0' /tmp/truesmith-corpus.json
	@grep -q '"path": "platform.c", "lines": 17, "score": 100.0' /tmp/truesmith-corpus.json
	@rm -f /tmp/truesmith-corpus.json
	@echo "tests passed"

clean:
	rm -f $(BIN)
