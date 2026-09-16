# Build the tests and examples.  Requires a C99 compiler and make.

CC      ?= cc
CFLAGS  ?= -std=c99 -O2 -Wall -Wextra -pedantic
CPPFLAGS ?=
BUILD   := build

.PHONY: all test toml-test clean

all: $(BUILD)/test $(BUILD)/dump $(BUILD)/config

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/test: tests/test.c toml.c toml.h | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -I. -DTOML_TEST_EXAMPLE='"examples/sample.toml"' \
		-o $@ tests/test.c toml.c

$(BUILD)/dump: examples/dump.c toml.c toml.h | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -I. -o $@ examples/dump.c toml.c

$(BUILD)/config: examples/config.c toml.c toml.h | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -I. -o $@ examples/config.c toml.c

$(BUILD)/toml-test-decoder: tests/toml_test_decoder.c toml.c toml.h | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -I. -o $@ tests/toml_test_decoder.c toml.c

# Run the unit tests.
test: $(BUILD)/test
	./$(BUILD)/test

# Run the language-agnostic conformance suite.  Needs a checkout of
# https://github.com/toml-lang/toml-test, for example:
#   git clone --depth 1 https://github.com/toml-lang/toml-test
# Pass its location if it is not ./toml-test:
#   make toml-test TOML_TEST_DIR=/path/to/toml-test
TOML_TEST_DIR ?= toml-test

toml-test: $(BUILD)/toml-test-decoder
	python3 tests/toml_test.py $(TOML_TEST_DIR)

clean:
	rm -rf $(BUILD)
