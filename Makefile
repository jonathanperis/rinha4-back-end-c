CC ?= cc
BUILD_DIR := build
COMMON_SRC := src/common/distance.c src/common/fdpass.c src/common/http.c src/common/index.c src/common/net.c src/common/search.c src/common/vectorize.c
CFLAGS_WARN := -Wall -Wextra -Wshadow -Werror -I src
CFLAGS_ARCH ?=
CFLAGS_COMMON := -std=c11 -O3 -DNDEBUG $(CFLAGS_ARCH) $(CFLAGS_WARN)
CFLAGS_TEST := -std=c11 -O0 -g $(CFLAGS_WARN)
LDFLAGS_COMMON :=

.PHONY: all api preprocess test test-search-stats search-stats-replay check-correctness test-api-fdpass-immediate test-assume-passed-fd-flags clean docker run

all: api preprocess

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

api: $(BUILD_DIR)/api

preprocess: $(BUILD_DIR)/build-index

$(BUILD_DIR)/api: src/api/main.c $(COMMON_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS_COMMON) $^ -o $@ $(LDFLAGS_COMMON)

$(BUILD_DIR)/build-index: src/preprocess/build_index.c src/common/distance.c | $(BUILD_DIR)
	$(CC) $(CFLAGS_COMMON) $^ -o $@ -lz

$(BUILD_DIR)/test_http: tests/test_http.c src/common/http.c | $(BUILD_DIR)
	$(CC) $(CFLAGS_TEST) $^ -o $@ $(LDFLAGS_COMMON)

$(BUILD_DIR)/test_vectorize: tests/test_vectorize.c src/common/vectorize.c src/common/distance.c | $(BUILD_DIR)
	$(CC) $(CFLAGS_TEST) $^ -o $@ $(LDFLAGS_COMMON)

$(BUILD_DIR)/test_search: tests/test_search.c src/common/search.c src/common/index.c src/common/distance.c | $(BUILD_DIR)
	$(CC) $(CFLAGS_TEST) $^ -o $@ $(LDFLAGS_COMMON)

$(BUILD_DIR)/test_search_stats: tests/test_search.c src/common/search.c src/common/index.c src/common/distance.c | $(BUILD_DIR)
	$(CC) $(CFLAGS_TEST) -DRINHA_SEARCH_STATS $^ -o $@ $(LDFLAGS_COMMON)

$(BUILD_DIR)/search-stats-replay: tools/search_stats_replay.c $(COMMON_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS_COMMON) -DRINHA_SEARCH_STATS $^ -o $@ -lz $(LDFLAGS_COMMON)

$(BUILD_DIR)/check-correctness: tools/check_correctness.c $(COMMON_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS_COMMON) $^ -o $@ -lz $(LDFLAGS_COMMON)

$(BUILD_DIR)/test_fdpass: tests/test_fdpass.c src/common/fdpass.c src/common/net.c | $(BUILD_DIR)
	$(CC) $(CFLAGS_TEST) $^ -o $@ $(LDFLAGS_COMMON)

# test_api_fdpass_immediate includes src/api/main.c after renaming main; keep
# src/api/main.c as a dependency even though the compile command names only the test.
$(BUILD_DIR)/test_api_fdpass_immediate: tests/test_api_fdpass_immediate.c src/api/main.c $(COMMON_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS_TEST) tests/test_api_fdpass_immediate.c $(COMMON_SRC) -o $@ $(LDFLAGS_COMMON)

$(BUILD_DIR)/test_api_fdpass_assume_flags: tests/test_api_fdpass_immediate.c src/api/main.c $(COMMON_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS_TEST) -DRINHA_ASSUME_PASSED_FD_FLAGS tests/test_api_fdpass_immediate.c $(COMMON_SRC) -o $@ $(LDFLAGS_COMMON)

test: $(BUILD_DIR)/test_http $(BUILD_DIR)/test_vectorize $(BUILD_DIR)/test_search $(BUILD_DIR)/test_fdpass $(BUILD_DIR)/test_api_fdpass_immediate
	./$(BUILD_DIR)/test_http
	./$(BUILD_DIR)/test_vectorize
	./$(BUILD_DIR)/test_search
	./$(BUILD_DIR)/test_fdpass
	./$(BUILD_DIR)/test_api_fdpass_immediate

test-search-stats: $(BUILD_DIR)/test_search_stats
	RINHA_SEARCH_STATS=1 ./$(BUILD_DIR)/test_search_stats

search-stats-replay: $(BUILD_DIR)/search-stats-replay

check-correctness: $(BUILD_DIR)/check-correctness

test-api-fdpass-immediate: $(BUILD_DIR)/test_api_fdpass_immediate
	./$(BUILD_DIR)/test_api_fdpass_immediate

test-assume-passed-fd-flags: $(BUILD_DIR)/test_api_fdpass_assume_flags
	./$(BUILD_DIR)/test_api_fdpass_assume_flags

docker:
	docker build -t rinha4-back-end-c:local .

run:
	docker compose up --build

clean:
	rm -rf $(BUILD_DIR)
