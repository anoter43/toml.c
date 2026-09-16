/*
 * toml.h -- a small, read-only TOML 1.1 parser.
 *
 * This is a single-file library: drop toml.h and toml.c into your project and
 * compile them together.  It uses only the C99 standard library (plus stdio in
 * toml.c for reading files) and has no configuration or build-system
 * requirements.  The parser accepts every construct in TOML v1.1.0, including
 * multi-line strings, dotted keys, arrays of tables, inline tables that span
 * multiple lines, and the full set of date-time types.  It is read-only: there
 * is no writer and no serializer.
 *
 * Basic usage
 * -----------
 *
 *     #include "toml.h"
 *
 *     toml_error_t error;
 *     toml_t *doc = toml_load("config.toml", &error);
 *     if (!doc) {
 *         fprintf(stderr, "config.toml:%d:%d: %s\n",
 *                 error.line, error.column, error.message);
 *         return 1;
 *     }
 *
 *     const char *host = toml_get_string(doc, "server.host", "localhost");
 *     long long   port = toml_get_integer(doc, "server.port", 8080);
 *     int         tls  = toml_get_boolean(doc, "server.tls", 0);
 *
 *     printf("connecting to %s:%lld (tls=%s)\n", host, port, tls ? "yes" : "no");
 *     toml_free(doc);
 *
 * toml_load() reads a file; toml_parse() parses text that you already have in
 * memory.  Both return an opaque document handle or NULL on failure.  The
 * look-up helpers above take a dotted path ("server.host" is the key `host`
 * inside the table `server`) and a fallback value that is returned when the
 * key is missing or holds a different type.
 *
 * Walking the document
 * --------------------
 *
 * A document is a tree of values.  The handle itself is the root table, and
 * every node is a toml_t.  Tables and arrays can be walked in insertion order:
 *
 *     const toml_t *doc = toml_parse(text, strlen(text), NULL);
 *     const toml_t *servers = toml_get(doc, "servers");
 *
 *     for (size_t i = 0; i < toml_count(servers); i++) {
 *         const toml_t *server = toml_at(servers, i);
 *         printf("%s: %lld\n",
 *                toml_string(toml_child(server, "name")),
 *                toml_integer(toml_child(server, "port"), 0));
 *     }
 *
 * toml_get() only follows plain table keys.  For keys that contain dots, or
 * when you want to look up a single key at a time, use toml_child().  Keys of
 * table entries are available through toml_key() (array elements have no key,
 * so it returns NULL for arrays).
 *
 * Value types
 * -----------
 *
 * toml_type() reports what a node holds: TOML_TABLE, TOML_ARRAY, TOML_STRING,
 * TOML_INTEGER, TOML_FLOAT, TOML_BOOLEAN or TOML_DATETIME.  The typed
 * accessors return safe defaults instead of crashing when the type does not
 * match:
 *
 *     toml_string(value)              -> const char*, or NULL
 *     toml_integer(value, fallback)   -> long long
 *     toml_float(value, fallback)     -> double
 *     toml_boolean(value, fallback)   -> int (0 or 1)
 *     toml_datetime(value)            -> const toml_datetime_t*, or NULL
 *
 * Integers are 64-bit signed: documents containing a value that does not fit
 * are rejected rather than silently truncated.  Floats are doubles.  Date and
 * time values keep their calendar fields in a toml_datetime_t along with a
 * "kind" field distinguishing an offset date-time from a local date-time,
 * local date, or local time.  Fractional seconds are kept to microsecond
 * precision; extra digits are truncated, never rounded.  For offset
 * date-times, offset_minutes is the offset east of UTC (for example -420 for
 * -07:00), and for the local kinds it is 0.
 *
 * Strings returned by the accessors are owned by the document and remain
 * valid until toml_free() is called.  They are NUL-terminated byte strings in
 * UTF-8 and their escape sequences have already been decoded.  TOML allows
 * strings and keys to contain NUL bytes (written as \u0000), which a
 * NUL-terminated C string cannot express; toml_string_length() and
 * toml_key_length() give the true byte count for those rare cases, while
 * strlen() and the look-up functions see the part before the first NUL.
 *
 * Errors
 * ------
 *
 * toml_parse() and toml_load() fill in the toml_error_t you pass (which may
 * be NULL) with a 1-based line and column and a short human-readable message.
 * A document that is rejected produces no partial result: the function
 * returns NULL and every allocation made while parsing is released.
 * toml_error_t.message is always NUL-terminated.
 *
 * Notes
 * -----
 *
 * - TOML documents must be valid UTF-8; invalid byte sequences are rejected.
 * - A UTF-8 byte order mark at the very start of a document is accepted and
 *   skipped, as are CRLF line endings anywhere.
 * - The parser imposes a nesting limit (256 levels for arrays, inline tables
 *   and dotted paths) so that malicious input cannot exhaust the C stack.
 * - Tables use linear search by key.  Documents with very large tables pay a
 *   small lookup cost, which is a deliberate trade for simplicity.
 * - All functions are thread-safe as long as you do not share a single
 *   document between threads while mutating it, which this API never does;
 *   after parsing, documents are effectively immutable.
 */

#ifndef TOML_H
#define TOML_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TOML_VERSION "1.1.0" /* the TOML specification this parser implements */

typedef struct toml_t toml_t;

typedef enum {
  TOML_INVALID = 0,
  TOML_TABLE,
  TOML_ARRAY,
  TOML_STRING,
  TOML_INTEGER,
  TOML_FLOAT,
  TOML_BOOLEAN,
  TOML_DATETIME
} toml_type_t;

typedef enum {
  TOML_OFFSET_DATETIME,
  TOML_LOCAL_DATETIME,
  TOML_LOCAL_DATE,
  TOML_LOCAL_TIME
} toml_datetime_kind_t;

typedef struct {
  toml_datetime_kind_t kind; /* which parts of the value are present */
  int year;                  /* 0 when the value has no date */
  int month;                 /* 1-12 */
  int day;                   /* 1-31 */
  int hour;                  /* 0-23 */
  int minute;                /* 0-59 */
  int second;                /* 0-60 */
  int microsecond;           /* 0-999999 */
  int offset_minutes;        /* minutes east of UTC; 0 for local kinds */
} toml_datetime_t;

#define TOML_ERROR_MESSAGE_SIZE 160

typedef struct {
  int line;   /* 1-based; 0 when not applicable */
  int column; /* 1-based; 0 when not applicable */
  char message[TOML_ERROR_MESSAGE_SIZE];
} toml_error_t;

/* Parse a TOML document from memory.  length is the number of bytes to read;
 * the text does not need to be NUL-terminated and is not modified.  Returns
 * the root table, or NULL on error (with *error filled in when error is not
 * NULL). */
toml_t *toml_parse(const char *text, size_t length, toml_error_t *error);

/* Read a file and parse it.  Returns NULL on error, including when the file
 * cannot be opened or read. */
toml_t *toml_load(const char *path, toml_error_t *error);

/* Release a document and everything in it.  NULL is accepted. */
void toml_free(toml_t *toml);

/* What a value holds.  TOML_INVALID is returned for a NULL value. */
toml_type_t toml_type(const toml_t *value);

/* "table", "array", "string", "integer", "float", "boolean", "datetime", or
 * "invalid" for TOML_INVALID. */
const char *toml_type_name(toml_type_t type);

/* Look up a dotted path ("a.b.c") starting from value, which is usually the
 * document.  Only table keys are traversed; arrays are not.  An empty path
 * returns value itself.  Returns NULL when the path does not exist. */
const toml_t *toml_get(const toml_t *value, const char *dotted_path);

/* Look up one key directly inside a table.  Returns NULL when value is not a
 * table or the key is absent. */
const toml_t *toml_child(const toml_t *table, const char *key);

/* Number of entries in a table or elements in an array; 0 for anything
 * else. */
size_t toml_count(const toml_t *table_or_array);

/* Entry/element number index (0-based) of a table or array; NULL when out of
 * range or when value holds something else. */
const toml_t *toml_at(const toml_t *table_or_array, size_t index);

/* Key of entry number index in a table; NULL for arrays and scalars or when
 * index is out of range. */
const char *toml_key(const toml_t *table, size_t index);

/* Byte length of the key returned by toml_key().  TOML keys may contain NUL
 * bytes (written as \u0000), which the NUL-terminated key cannot express; use
 * this length to get the full key. */
size_t toml_key_length(const toml_t *table, size_t index);

/* Typed accessors.  Each returns its fallback (or NULL) unless the value has
 * exactly the requested type. */
const char *toml_string(const toml_t *value);
long long toml_integer(const toml_t *value, long long fallback);
double toml_float(const toml_t *value, double fallback);
int toml_boolean(const toml_t *value, int fallback);
const toml_datetime_t *toml_datetime(const toml_t *value);

/* Byte length of the string returned by toml_string().  Like keys, TOML
 * strings may contain NUL bytes (from \u0000), so prefer this length over
 * strlen() unless you know the data has none. */
size_t toml_string_length(const toml_t *value);

/* Shorthand for a look-up and typed access in one call.  The fallback is
 * returned when the path is missing or holds another type. */
const char *toml_get_string(const toml_t *toml, const char *path, const char *fallback);
long long toml_get_integer(const toml_t *toml, const char *path, long long fallback);
double toml_get_float(const toml_t *toml, const char *path, double fallback);
int toml_get_boolean(const toml_t *toml, const char *path, int fallback);
const toml_datetime_t *toml_get_datetime(const toml_t *toml, const char *path);

#ifdef __cplusplus
}
#endif

#endif /* TOML_H */
