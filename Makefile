# Touchstone build. Pure C11, no dependencies.
CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra
SRC = src/main.c src/tst_util.c src/tst_scan.c src/tst_check.c
BIN = touchstone
HDR = src/touchstone_selfscore.h

.PHONY: all clean test selfcheck selfscore

all: $(BIN)

$(BIN): $(SRC) src/tst_util.h src/tst_scan.h src/tst_check.h $(HDR)
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
	printf '/* Touchstone self-score, written by the Makefile at build time. */\n#ifndef TOUCHSTONE_SELFSCORE_H\n#define TOUCHSTONE_SELFSCORE_H\n\n#define TOUCHSTONE_SELF_SCORE "100.0"\n#define TOUCHSTONE_SELF_BUILT "%s"\n#define TOUCHSTONE_SELF_FINGERPRINT "%s"\n\n#endif\n' "$$STAMP" "$$FPRINT" > $(HDR)
	$(CC) $(CFLAGS) -o $(BIN) $(SRC)
	./$(BIN) . --exclude tests/corpus --no-build --quiet --self-check

test: selfscore
	@./$(BIN) tests/corpus --no-build --quiet --json /tmp/touchstone-corpus.json; test $$? -eq 1
	@grep -q '"path": "clean.c", "lines": 8, "score": 100.0' /tmp/touchstone-corpus.json
	@grep -q '"path": "borrowed.c", "lines": 8, "score": 95.0' /tmp/touchstone-corpus.json
	@grep -q '"path": "credited.c", "lines": 10, "score": 87.5' /tmp/touchstone-corpus.json
	@grep -q '"path": "doubled.c", "lines": 16, "score": 94.0' /tmp/touchstone-corpus.json
	@grep -q '"path": "stubbed.c", "lines": 8, "score": 94.0' /tmp/touchstone-corpus.json
	@grep -q '"path": "page.html", "lines": 10, "score": 100.0' /tmp/touchstone-corpus.json
	@grep -q '"path": "formats.c", "lines": 10, "score": 100.0' /tmp/touchstone-corpus.json
	@grep -q '"path": "licensed.c", "lines": 7, "score": 87.5' /tmp/touchstone-corpus.json
	@grep -q '"path": "calls.h", "lines": 11, "score": 100.0' /tmp/touchstone-corpus.json
	@grep -q '"path": "platform.c", "lines": 17, "score": 100.0' /tmp/touchstone-corpus.json
	@rm -f /tmp/touchstone-corpus.json
	@echo "tests passed"

clean:
	rm -f $(BIN)
