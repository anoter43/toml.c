/*
 * toml_test_decoder.c -- adapter for the toml-test conformance suite
 * (https://github.com/toml-lang/toml-test).
 *
 * Reads TOML on stdin, writes the tagged JSON representation on stdout and
 * exits non-zero when the document is invalid.  This is only used by
 * `make toml-test`; it is not part of the library.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "toml.h"

static void print_json_string_n(const char *text, size_t length) {
  const unsigned char *p = (const unsigned char *) text;
  size_t i;
  putchar('"');
  for (i = 0; i < length; i++) {
    switch (p[i]) {
      case '"': fputs("\\\"", stdout); break;
      case '\\': fputs("\\\\", stdout); break;
      case '\b': fputs("\\b", stdout); break;
      case '\f': fputs("\\f", stdout); break;
      case '\n': fputs("\\n", stdout); break;
      case '\r': fputs("\\r", stdout); break;
      case '\t': fputs("\\t", stdout); break;
      default:
        if (p[i] < 0x20) {
          printf("\\u%04x", p[i]);
        } else {
          putchar(p[i]);
        }
        break;
    }
  }
  putchar('"');
}

static void print_datetime(const toml_datetime_t *dt) {
  const char *type = "datetime";
  if (dt->kind == TOML_LOCAL_DATETIME) type = "datetime-local";
  if (dt->kind == TOML_LOCAL_DATE) type = "date-local";
  if (dt->kind == TOML_LOCAL_TIME) type = "time-local";
  printf("{\"type\": \"%s\", \"value\": \"", type);
  if (dt->kind != TOML_LOCAL_TIME) {
    printf("%04d-%02d-%02d", dt->year, dt->month, dt->day);
  }
  if (dt->kind != TOML_LOCAL_DATE) {
    if (dt->kind != TOML_LOCAL_TIME) putchar('T');
    printf("%02d:%02d:%02d", dt->hour, dt->minute, dt->second);
    if (dt->microsecond != 0) {
      if (dt->microsecond % 1000 == 0) {
        printf(".%03d", dt->microsecond / 1000);
      } else {
        printf(".%06d", dt->microsecond);
      }
    }
    if (dt->kind == TOML_OFFSET_DATETIME) {
      int offset = dt->offset_minutes;
      if (offset == 0) {
        putchar('Z');
      } else {
        printf("%c%02d:%02d", offset < 0 ? '-' : '+', abs(offset) / 60, abs(offset) % 60);
      }
    }
  }
  printf("\"}");
}

static void print_value(const toml_t *value) {
  size_t i;
  switch (toml_type(value)) {
    case TOML_TABLE:
      putchar('{');
      for (i = 0; i < toml_count(value); i++) {
        if (i) putchar(',');
        print_json_string_n(toml_key(value, i), toml_key_length(value, i));
        putchar(':');
        print_value(toml_at(value, i));
      }
      putchar('}');
      break;
    case TOML_ARRAY:
      putchar('[');
      for (i = 0; i < toml_count(value); i++) {
        if (i) putchar(',');
        print_value(toml_at(value, i));
      }
      putchar(']');
      break;
    case TOML_STRING:
      fputs("{\"type\": \"string\", \"value\": ", stdout);
      print_json_string_n(toml_string(value), toml_string_length(value));
      putchar('}');
      break;
    case TOML_INTEGER:
      printf("{\"type\": \"integer\", \"value\": \"%lld\"}", toml_integer(value, 0));
      break;
    case TOML_FLOAT: {
      double number = toml_float(value, 0);
      fputs("{\"type\": \"float\", \"value\": \"", stdout);
      if (isnan(number)) {
        fputs("nan", stdout);
      } else if (isinf(number)) {
        fputs(number < 0 ? "-inf" : "inf", stdout);
      } else {
        printf("%.17g", number);
      }
      fputs("\"}", stdout);
      break;
    }
    case TOML_BOOLEAN:
      printf("{\"type\": \"bool\", \"value\": \"%s\"}", toml_boolean(value, 0) ? "true" : "false");
      break;
    case TOML_DATETIME:
      print_datetime(toml_datetime(value));
      break;
    default:
      fputs("{\"type\": \"invalid\", \"value\": \"\"}", stdout);
      break;
  }
}

int main(void) {
  size_t length = 0;
  size_t capacity = 65536;
  char *text = (char *) malloc(capacity);
  toml_error_t error;
  toml_t *doc;

  if (!text) return 1;
  for (;;) {
    size_t n = fread(text + length, 1, capacity - length, stdin);
    length += n;
    if (length < capacity) break;
    capacity *= 2;
    text = (char *) realloc(text, capacity);
    if (!text) return 1;
  }

  doc = toml_parse(text, length, &error);
  free(text);
  if (!doc) {
    fprintf(stderr, "%d:%d: %s\n", error.line, error.column, error.message);
    return 1;
  }
  print_value(doc);
  putchar('\n');
  toml_free(doc);
  return 0;
}
