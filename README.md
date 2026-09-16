# toml.c

A small, read-only yet complete [TOML v1.1.0](https://toml.io/en/v1.1.0) parser for C99.

## Highlights

- Complete TOML 1.1 syntax: bare/quoted/dotted keys, basic and literal strings
  (including multi-line forms and line-ending backslashes), integers in
  decimal/hex/octal/binary with underscores, floats including `inf` and `nan`,
  booleans, all four date-time kinds, arrays, inline tables that may span
  lines, and arrays of tables.
- Strict validation: duplicate keys, table redefinitions, dotted-key
  conflicts, sealed inline tables, static arrays, calendar-invalid dates,
  malformed numbers, bad escapes, control characters, and invalid UTF-8 are
  all rejected with a 1-based line and column.
- 64-bit signed integers with overflow detection; floats are `double`.
- No global state, no allocations retained after `toml_free()`, and a nesting
  limit so adversarial input cannot exhaust the C stack.
- Passes the [toml-test](https://github.com/toml-lang/toml-test) conformance
  suite for TOML 1.1 (valid and invalid tests).

## Quick start

Copy `toml.h` and `toml.c` into your project and compile them together:

```sh
cc -std=c99 -O2 my_program.c toml.c
```

```c
#include <stdio.h>
#include "toml.h"

int main(void)
{
    toml_error_t error;
    toml_t *doc = toml_load("config.toml", &error);
    if (!doc) {
        fprintf(stderr, "config.toml:%d:%d: %s\n",
                error.line, error.column, error.message);
        return 1;
    }

    const char *host = toml_get_string(doc, "server.host", "localhost");
    long long port = toml_get_integer(doc, "server.port", 8080);
    int tls = toml_get_boolean(doc, "server.tls", 0);

    printf("connecting to %s:%lld (tls=%s)\n", host, port, tls ? "yes" : "no");

    toml_free(doc);
    return 0;
}
```

`toml_get_string()`, `toml_get_integer()`, `toml_get_float()`,
`toml_get_boolean()` and `toml_get_datetime()` each take a dotted path and a
fallback that is returned when the key is missing or holds a different type,
so defaults live next to the code that uses them.

Walking a document uses one opaque type for every node; the document handle is
the root table:

```c
const toml_t *tags = toml_get(doc, "server.tags");
for (size_t i = 0; i < toml_count(tags); i++) {
    printf("tag: %s\n", toml_string(toml_at(tags, i)));
}

for (size_t i = 0; i < toml_count(doc); i++) {
    printf("key %s is a %s\n", toml_key(doc, i), toml_type_name(toml_type(toml_at(doc, i))));
}
```

## API overview

| Function | Purpose |
| --- | --- |
| `toml_parse(text, length, error)` | Parse a document from memory. |
| `toml_load(path, error)` | Read a file and parse it. |
| `toml_free(toml)` | Release a document (accepts `NULL`). |
| `toml_type(value)` / `toml_type_name(type)` | Value type as enum or string. |
| `toml_get(value, path)` | Look up a dotted path; `NULL` when missing. |
| `toml_child(table, key)` | Look up one key, including keys containing dots. |
| `toml_count(value)` | Entries in a table or elements in an array. |
| `toml_at(value, index)` | Entry/element at an index. |
| `toml_key(table, index)` / `toml_key_length(...)` | Key of a table entry. |
| `toml_string(value)` / `toml_string_length(...)` | String value and byte length. |
| `toml_integer(value, fallback)` | Integer value. |
| `toml_float(value, fallback)` | Float value. |
| `toml_boolean(value, fallback)` | Boolean value as `int`. |
| `toml_datetime(value)` | Date-time fields and kind. |

All accessors are type-strict: they return their fallback (or `NULL`) when the
value is not of the requested type. `toml_get()` traverses table keys only;
use `toml_at()` and `toml_child()` to reach into arrays.

`toml_datetime_t` carries a `kind` (`TOML_OFFSET_DATETIME`,
`TOML_LOCAL_DATETIME`, `TOML_LOCAL_DATE`, `TOML_LOCAL_TIME`), the calendar
fields, `microsecond`, and `offset_minutes` east of UTC. Fractional seconds
beyond microsecond precision are truncated, never rounded.

Keys and strings may legally contain NUL bytes (`\u0000`); the `*_length()`
accessors report the true byte size for those cases, while the plain pointers
remain NUL-terminated.

The header documents every function, the error model, ownership rules, and
thread-safety in a descriptive comment block at the top. Since the parser never
mutates a document after parsing, documents can be shared between threads as
long as the caller does not call `toml_free()` concurrently.

## Building and testing

```sh
make                # build the tests and examples into build/
make test           # run the unit tests
make toml-test      # run the conformance suite (submodule already present)
make clean
```

`make toml-test` builds `tests/toml_test_decoder.c` and runs
`tests/toml_test.py` against the checkout in `toml-test/`. Point it elsewhere
with `make toml-test TOML_TEST_DIR=/path/to/toml-test`.

The unit tests exercise every value type, string escape and quoting form,
date-time shape, table and dotted-key rule, array-of-tables nesting, error
case, and API edge; they compile cleanly with `-std=c99 -Wall -Wextra
-pedantic` and run cleanly under AddressSanitizer and UndefinedBehaviorSanitizer.
