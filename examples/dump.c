/*
 * dump.c -- print every value in a TOML file.
 *
 * Usage: dump [file.toml]
 *
 * Demonstrates walking a document: toml_count()/toml_at()/toml_key() iterate
 * tables and arrays, toml_type() reports what each node holds, and the typed
 * accessors read the values.
 */

#include <stdio.h>

#include "toml.h"

static void print_datetime(const toml_datetime_t *dt) {
  if (dt->kind == TOML_LOCAL_TIME) {
    printf("%02d:%02d:%02d", dt->hour, dt->minute, dt->second);
  } else {
    printf("%04d-%02d-%02d", dt->year, dt->month, dt->day);
    if (dt->kind != TOML_LOCAL_DATE) {
      printf("T%02d:%02d:%02d", dt->hour, dt->minute, dt->second);
      if (dt->offset_minutes != 0) {
        int offset = dt->offset_minutes;
        printf("%c%02d:%02d", offset < 0 ? '-' : '+', (offset < 0 ? -offset : offset) / 60,
               (offset < 0 ? -offset : offset) % 60);
      } else if (dt->kind == TOML_OFFSET_DATETIME) {
        printf("Z");
      }
    }
  }
  if (dt->microsecond) printf(".%06d", dt->microsecond);
}

static void dump_value(const toml_t *value, const char *key, int indent);

static void dump_children(const toml_t *value, int indent) {
  size_t i;
  printf("%s\n", toml_type(value) == TOML_ARRAY ? "[" : "{");
  for (i = 0; i < toml_count(value); i++) {
    dump_value(toml_at(value, i), toml_key(value, i), indent + 1);
  }
  printf("%*s%s", indent * 4, "", toml_type(value) == TOML_ARRAY ? "]" : "}");
}

static void dump_value(const toml_t *value, const char *key, int indent) {
  printf("%*s", indent * 4, "");
  if (key) printf("%s = ", key);
  switch (toml_type(value)) {
    case TOML_TABLE:
    case TOML_ARRAY:
      dump_children(value, indent);
      break;
    case TOML_STRING:
      printf("\"%s\"", toml_string(value));
      break;
    case TOML_INTEGER:
      printf("%lld", toml_integer(value, 0));
      break;
    case TOML_FLOAT:
      printf("%g", toml_float(value, 0));
      break;
    case TOML_BOOLEAN:
      printf("%s", toml_boolean(value, 0) ? "true" : "false");
      break;
    case TOML_DATETIME:
      print_datetime(toml_datetime(value));
      break;
    default:
      printf("<invalid>");
      break;
  }
  printf("\n");
}

int main(int argc, char **argv) {
  const char *path = argc > 1 ? argv[1] : "examples/sample.toml";
  toml_error_t error;
  toml_t *doc = toml_load(path, &error);
  if (!doc) {
    fprintf(stderr, "%s:%d:%d: %s\n", path, error.line, error.column, error.message);
    return 1;
  }
  dump_children(doc, 0);
  toml_free(doc);
  return 0;
}
