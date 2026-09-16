/*
 * config.c -- read settings from an embedded TOML document.
 *
 * Demonstrates the look-up helpers: each call names a dotted path and a
 * fallback that is used when the key is missing or holds a different type, so
 * defaults live next to the code that uses them.
 */

#include <stdio.h>
#include <string.h>

#include "toml.h"

static const char *CONFIG =
    "title = \"widgets\"\n"
    "debug = true\n"
    "created = 2024-03-01T10:30:00Z\n"
    "\n"
    "[server]\n"
    "host = \"example.com\"\n"
    "port = 8443\n"
    "tags = [ \"web\", \"prod\" ]\n"
    "timeouts = [ 1.5, 5.0 ]\n"
    "\n"
    "[limits]\n"
    "max_connections = 1_000\n";

int main(void) {
  toml_error_t error;
  toml_t *doc = toml_parse(CONFIG, strlen(CONFIG), &error);
  if (!doc) {
    fprintf(stderr, "%d:%d: %s\n", error.line, error.column, error.message);
    return 1;
  }

  printf("title: %s\n", toml_get_string(doc, "title", "untitled"));
  printf("debug: %s\n", toml_get_boolean(doc, "debug", 0) ? "yes" : "no");
  printf("server: %s:%lld\n", toml_get_string(doc, "server.host", "localhost"),
         toml_get_integer(doc, "server.port", 80));
  printf("max connections: %lld\n", toml_get_integer(doc, "limits.max_connections", 100));
  printf("missing key falls back: %s\n", toml_get_string(doc, "nope", "default"));

  {
    const toml_datetime_t *created = toml_get_datetime(doc, "created");
    if (created) {
      printf("created: %04d-%02d-%02d %02d:%02d:%02dZ\n", created->year, created->month,
             created->day, created->hour, created->minute, created->second);
    }
  }

  {
    const toml_t *tags = toml_get(doc, "server.tags");
    size_t i;
    printf("tags:");
    for (i = 0; i < toml_count(tags); i++) {
      printf(" %s", toml_string(toml_at(tags, i)));
    }
    printf("\n");
  }

  toml_free(doc);
  return 0;
}
