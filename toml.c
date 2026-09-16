/*
 * toml.c -- implementation of the read-only TOML 1.1 parser declared in
 * toml.h.  See toml.h for the user-facing documentation.
 */

#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "toml.h"

#define TOML_MAX_NESTING 256

/* Node flags used while building and validating the tree. */
enum {
  F_IMPLICIT = 1u << 0, /* table created implicitly by a [a.b.c] header */
  F_DEFINED = 1u << 1,  /* table explicitly defined by a header, inline
                           table, or array-of-tables element */
  F_DOTTED = 1u << 2,   /* table created by a dotted key */
  F_SEALED = 1u << 3,   /* inline table: closed to all further additions */
  F_AOT = 1u << 4       /* array created by [[a.b]] */
};

struct toml_t {
  toml_type_t type;
  char *key;          /* key inside the parent table; NULL for the root and
                         for array elements */
  size_t key_length;
  toml_t **items;     /* table entries or array elements */
  size_t count;
  size_t capacity;
  char *string;       /* decoded value for TOML_STRING */
  size_t string_length;
  long long integer;  /* value for TOML_INTEGER */
  double floating;    /* value for TOML_FLOAT */
  int boolean;        /* value for TOML_BOOLEAN */
  toml_datetime_t datetime;
  unsigned flags;
};

typedef struct {
  const char *text;
  size_t length;
  size_t position;
  int line;
  int column;
  int depth;
  int failed;
  toml_error_t *error;
} toml_parser_t;

typedef struct {
  char *data;
  size_t length;
  size_t capacity;
} toml_buffer_t;

/* A byte string that may contain NUL bytes (TOML strings and keys can). */
typedef struct {
  char *text;
  size_t length;
} toml_text_t;

typedef struct {
  toml_text_t *keys;
  size_t count;
  size_t capacity;
} toml_keypath_t;

/* ------------------------------------------------------------------ */
/* Errors and allocation                                              */
/* ------------------------------------------------------------------ */

static void set_error(toml_error_t *error, int line, int column, const char *format, ...) {
  va_list args;
  if (!error) return;
  error->line = line;
  error->column = column;
  va_start(args, format);
  vsnprintf(error->message, sizeof(error->message), format, args);
  va_end(args);
}

static void parse_error(toml_parser_t *parser, const char *format, ...) {
  va_list args;
  if (parser->failed) return;
  parser->failed = 1;
  if (!parser->error) return;
  parser->error->line = parser->line;
  parser->error->column = parser->column;
  va_start(args, format);
  vsnprintf(parser->error->message, sizeof(parser->error->message), format, args);
  va_end(args);
}

static void parse_oom(toml_parser_t *parser) {
  parse_error(parser, "out of memory");
}

/* ------------------------------------------------------------------ */
/* Character helpers                                                  */
/* ------------------------------------------------------------------ */

static int is_digit(int c) { return c >= '0' && c <= '9'; }

static int hex_value(int c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

static int is_bare_key_char(int c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || is_digit(c) ||
         c == '_' || c == '-';
}

static int is_bare_value_char(int c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || is_digit(c) ||
         c == '_' || c == '+' || c == '-' || c == '.' || c == ':';
}

/* ------------------------------------------------------------------ */
/* Parser primitives                                                  */
/* ------------------------------------------------------------------ */

static int peek(const toml_parser_t *parser) {
  if (parser->position >= parser->length) return -1;
  return (unsigned char) parser->text[parser->position];
}

static int peek_at(const toml_parser_t *parser, size_t offset) {
  if (parser->position + offset >= parser->length) return -1;
  return (unsigned char) parser->text[parser->position + offset];
}

static int advance(toml_parser_t *parser) {
  int c;
  if (parser->position >= parser->length) return -1;
  c = (unsigned char) parser->text[parser->position++];
  if (c == '\n') {
    parser->line++;
    parser->column = 1;
  } else {
    parser->column++;
  }
  return c;
}

static void skip_ws(toml_parser_t *parser) {
  while (peek(parser) == ' ' || peek(parser) == '\t') advance(parser);
}

static int skip_newline(toml_parser_t *parser) {
  if (peek(parser) == '\n') {
    advance(parser);
    return 0;
  }
  if (peek(parser) == '\r') {
    if (peek_at(parser, 1) != '\n') {
      parse_error(parser, "carriage return without line feed");
      return -1;
    }
    advance(parser);
    advance(parser);
    return 0;
  }
  return -1;
}

/* Consume a comment; leaves the newline (or EOF) for the caller. */
static void skip_comment(toml_parser_t *parser) {
  advance(parser); /* '#' */
  for (;;) {
    int c = peek(parser);
    if (c == -1 || c == '\n') return;
    if (c == '\r') {
      if (peek_at(parser, 1) != '\n') {
        parse_error(parser, "carriage return without line feed");
      }
      return;
    }
    if (c != '\t' && (c < 0x20 || c == 0x7f)) {
      parse_error(parser, "control character in comment");
      return;
    }
    advance(parser);
  }
}

/* Skip whitespace, comments and newlines, as allowed inside arrays and
 * inline tables. */
static int skip_ws_comments_newlines(toml_parser_t *parser) {
  for (;;) {
    int c = peek(parser);
    if (c == ' ' || c == '\t') {
      advance(parser);
      continue;
    }
    if (c == '#') {
      skip_comment(parser);
      if (parser->failed) return -1;
      continue;
    }
    if (c == '\n' || c == '\r') {
      if (skip_newline(parser)) return -1;
      continue;
    }
    return 0;
  }
}

/* ------------------------------------------------------------------ */
/* UTF-8 validation                                                   */
/* ------------------------------------------------------------------ */

/* Returns the offset of the first invalid byte, or length when valid. */
static size_t utf8_invalid_offset(const char *text, size_t length) {
  size_t i = 0;
  while (i < length) {
    unsigned char c = (unsigned char) text[i];
    size_t extra;
    unsigned char lo = 0x80, hi = 0xbf;
    if (c < 0x80) {
      i++;
      continue;
    } else if (c >= 0xc2 && c <= 0xdf) {
      extra = 1;
    } else if (c == 0xe0) {
      extra = 2;
      lo = 0xa0;
    } else if (c >= 0xe1 && c <= 0xec) {
      extra = 2;
    } else if (c == 0xed) {
      extra = 2;
      hi = 0x9f;
    } else if (c >= 0xee && c <= 0xef) {
      extra = 2;
    } else if (c == 0xf0) {
      extra = 3;
      lo = 0x90;
    } else if (c >= 0xf1 && c <= 0xf3) {
      extra = 3;
    } else if (c == 0xf4) {
      extra = 3;
      hi = 0x8f;
    } else {
      return i;
    }
    if (i + extra >= length) return i;
    if ((unsigned char) text[i + 1] < lo || (unsigned char) text[i + 1] > hi) return i;
    {
      size_t k;
      for (k = 2; k <= extra; k++) {
        unsigned char b = (unsigned char) text[i + k];
        if (b < 0x80 || b > 0xbf) return i;
      }
    }
    i += extra + 1;
  }
  return length;
}

/* ------------------------------------------------------------------ */
/* Growable string buffer                                             */
/* ------------------------------------------------------------------ */

static int buffer_reserve(toml_parser_t *parser, toml_buffer_t *buffer, size_t extra) {
  size_t needed = buffer->length + extra + 1;
  size_t capacity;
  char *data;
  if (needed <= buffer->capacity) return 0;
  capacity = buffer->capacity ? buffer->capacity : 32;
  while (capacity < needed) capacity *= 2;
  data = (char *) realloc(buffer->data, capacity);
  if (!data) {
    parse_oom(parser);
    return -1;
  }
  buffer->data = data;
  buffer->capacity = capacity;
  return 0;
}

static int buffer_put(toml_parser_t *parser, toml_buffer_t *buffer, const char *bytes, size_t n) {
  if (buffer_reserve(parser, buffer, n)) return -1;
  memcpy(buffer->data + buffer->length, bytes, n);
  buffer->length += n;
  buffer->data[buffer->length] = '\0';
  return 0;
}

static int buffer_put_char(toml_parser_t *parser, toml_buffer_t *buffer, int c) {
  char byte = (char) c;
  return buffer_put(parser, buffer, &byte, 1);
}

static int buffer_put_codepoint(toml_parser_t *parser, toml_buffer_t *buffer,
                                unsigned long codepoint) {
  unsigned char bytes[4];
  size_t n;
  if (codepoint <= 0x7f) {
    bytes[0] = (unsigned char) codepoint;
    n = 1;
  } else if (codepoint <= 0x7ff) {
    bytes[0] = (unsigned char) (0xc0 | (codepoint >> 6));
    bytes[1] = (unsigned char) (0x80 | (codepoint & 0x3f));
    n = 2;
  } else if (codepoint <= 0xffff) {
    bytes[0] = (unsigned char) (0xe0 | (codepoint >> 12));
    bytes[1] = (unsigned char) (0x80 | ((codepoint >> 6) & 0x3f));
    bytes[2] = (unsigned char) (0x80 | (codepoint & 0x3f));
    n = 3;
  } else {
    bytes[0] = (unsigned char) (0xf0 | (codepoint >> 18));
    bytes[1] = (unsigned char) (0x80 | ((codepoint >> 12) & 0x3f));
    bytes[2] = (unsigned char) (0x80 | ((codepoint >> 6) & 0x3f));
    bytes[3] = (unsigned char) (0x80 | (codepoint & 0x3f));
    n = 4;
  }
  return buffer_put(parser, buffer, (const char *) bytes, n);
}

/* Return the finished text, or an empty text when the parser has failed. */
static toml_text_t buffer_finish(toml_parser_t *parser, toml_buffer_t *buffer) {
  toml_text_t text;
  text.text = NULL;
  text.length = 0;
  if (buffer_reserve(parser, buffer, 0)) {
    free(buffer->data);
    return text;
  }
  buffer->data[buffer->length] = '\0';
  text.text = buffer->data;
  text.length = buffer->length;
  return text;
}

/* ------------------------------------------------------------------ */
/* Tree construction                                                  */
/* ------------------------------------------------------------------ */

static toml_t *node_new(toml_parser_t *parser, toml_type_t type) {
  toml_t *node = (toml_t *) calloc(1, sizeof(*node));
  if (!node) {
    parse_oom(parser);
    return NULL;
  }
  node->type = type;
  return node;
}

static int node_append(toml_parser_t *parser, toml_t *parent, toml_t *child) {
  if (parent->count == parent->capacity) {
    size_t capacity = parent->capacity ? parent->capacity * 2 : 4;
    toml_t **items = (toml_t **) realloc(parent->items, capacity * sizeof(*items));
    if (!items) {
      parse_oom(parser);
      return -1;
    }
    parent->items = items;
    parent->capacity = capacity;
  }
  parent->items[parent->count++] = child;
  return 0;
}

static toml_t *node_find_n(const toml_t *table, const char *key, size_t length) {
  size_t i;
  if (table->type != TOML_TABLE) return NULL;
  for (i = 0; i < table->count; i++) {
    toml_t *item = table->items[i];
    if (item->key && item->key_length == length && memcmp(item->key, key, length) == 0) {
      return item;
    }
  }
  return NULL;
}

static toml_t *node_find(const toml_t *table, const char *key) {
  return node_find_n(table, key, strlen(key));
}

void toml_free(toml_t *toml) {
  size_t i;
  if (!toml) return;
  for (i = 0; i < toml->count; i++) toml_free(toml->items[i]);
  free(toml->items);
  free(toml->key);
  free(toml->string);
  free(toml);
}

static toml_t *create_child_table(toml_parser_t *parser, toml_t *parent,
                                  toml_text_t key, unsigned flags) {
  toml_t *table = node_new(parser, TOML_TABLE);
  if (!table) {
    free(key.text);
    return NULL;
  }
  table->key = key.text;
  table->key_length = key.length;
  table->flags = flags;
  if (node_append(parser, parent, table)) {
    toml_free(table);
    return NULL;
  }
  return table;
}

/* ------------------------------------------------------------------ */
/* Strings                                                            */
/* ------------------------------------------------------------------ */

static int read_hex_escape(toml_parser_t *parser, toml_buffer_t *buffer, int digits) {
  unsigned long codepoint = 0;
  int i;
  for (i = 0; i < digits; i++) {
    int value = hex_value(advance(parser));
    if (value < 0) {
      parse_error(parser, "invalid hexadecimal escape");
      return -1;
    }
    codepoint = codepoint * 16 + (unsigned long) value;
  }
  if (codepoint > 0x10ffff || (codepoint >= 0xd800 && codepoint <= 0xdfff)) {
    parse_error(parser, "escape is not a Unicode scalar value");
    return -1;
  }
  return buffer_put_codepoint(parser, buffer, codepoint);
}

/* Backslash already consumed; decode one escape sequence. */
static int read_escape(toml_parser_t *parser, toml_buffer_t *buffer) {
  switch (advance(parser)) {
    case 'b': return buffer_put_char(parser, buffer, '\b');
    case 't': return buffer_put_char(parser, buffer, '\t');
    case 'n': return buffer_put_char(parser, buffer, '\n');
    case 'f': return buffer_put_char(parser, buffer, '\f');
    case 'r': return buffer_put_char(parser, buffer, '\r');
    case 'e': return buffer_put_char(parser, buffer, 0x1b);
    case '"': return buffer_put_char(parser, buffer, '"');
    case '\\': return buffer_put_char(parser, buffer, '\\');
    case 'x': return read_hex_escape(parser, buffer, 2);
    case 'u': return read_hex_escape(parser, buffer, 4);
    case 'U': return read_hex_escape(parser, buffer, 8);
    default:
      parse_error(parser, "invalid escape sequence");
      return -1;
  }
}

static toml_text_t parse_basic_string(toml_parser_t *parser) {
  toml_buffer_t buffer = {0};
  toml_text_t text = {NULL, 0};
  advance(parser); /* opening quote */
  for (;;) {
    int c = peek(parser);
    if (c == '"') {
      advance(parser);
      return buffer_finish(parser, &buffer);
    }
    if (c == -1 || c == '\n' || c == '\r') {
      parse_error(parser, "unterminated string");
      break;
    }
    if (c == '\\') {
      advance(parser);
      if (read_escape(parser, &buffer)) break;
      continue;
    }
    if (c != '\t' && (c < 0x20 || c == 0x7f)) {
      parse_error(parser, "control character in string");
      break;
    }
    if (buffer_put_char(parser, &buffer, c)) break;
    advance(parser);
  }
  free(buffer.data);
  return text;
}

static toml_text_t parse_multiline_basic_string(toml_parser_t *parser) {
  toml_buffer_t buffer = {0};
  toml_text_t text = {NULL, 0};
  advance(parser);
  advance(parser);
  advance(parser); /* """ */
  if (peek(parser) == '\r' && peek_at(parser, 1) == '\n') {
    advance(parser);
    advance(parser);
  } else if (peek(parser) == '\n') {
    advance(parser);
  }
  for (;;) {
    int c = peek(parser);
    if (c == -1) {
      parse_error(parser, "unterminated multi-line string");
      break;
    }
    if (c == '"') {
      size_t run = 0;
      while (peek_at(parser, run) == '"') run++;
      if (run >= 3) {
        size_t i;
        if (run - 3 > 2) {
          parse_error(parser, "too many quotation marks at end of multi-line string");
          break;
        }
        for (i = 0; i < run - 3; i++) {
          if (buffer_put_char(parser, &buffer, '"')) goto fail;
        }
        for (i = 0; i < run; i++) advance(parser);
        return buffer_finish(parser, &buffer);
      }
      while (run--) {
        if (buffer_put_char(parser, &buffer, '"')) goto fail;
        advance(parser);
      }
      continue;
    }
    if (c == '\\') {
      int following = peek_at(parser, 1);
      if (following == ' ' || following == '\t' || following == '\n' || following == '\r') {
        /* Line-ending backslash: trim through the next non-whitespace. */
        advance(parser);
        while (peek(parser) == ' ' || peek(parser) == '\t') advance(parser);
        if (peek(parser) == '\r') {
          if (peek_at(parser, 1) != '\n') {
            parse_error(parser, "carriage return without line feed");
            break;
          }
          advance(parser);
        }
        if (peek(parser) != '\n') {
          parse_error(parser, "invalid escape sequence");
          break;
        }
        advance(parser);
        for (;;) {
          int w = peek(parser);
          if (w == ' ' || w == '\t' || w == '\n') {
            advance(parser);
            continue;
          }
          if (w == '\r' && peek_at(parser, 1) == '\n') {
            advance(parser);
            advance(parser);
            continue;
          }
          break;
        }
        continue;
      }
      advance(parser);
      if (read_escape(parser, &buffer)) break;
      continue;
    }
    if (c == '\r') {
      if (peek_at(parser, 1) != '\n') {
        parse_error(parser, "carriage return without line feed");
        break;
      }
      advance(parser);
      advance(parser);
      if (buffer_put_char(parser, &buffer, '\n')) break;
      continue;
    }
    if (c == '\n' || c == '\t' || (c >= 0x20 && c != 0x7f)) {
      if (buffer_put_char(parser, &buffer, c)) break;
      advance(parser);
      continue;
    }
    parse_error(parser, "control character in string");
    break;
  }
fail:
  free(buffer.data);
  return text;
}

static toml_text_t parse_literal_string(toml_parser_t *parser) {
  toml_buffer_t buffer = {0};
  toml_text_t text = {NULL, 0};
  advance(parser); /* opening apostrophe */
  for (;;) {
    int c = peek(parser);
    if (c == '\'') {
      advance(parser);
      return buffer_finish(parser, &buffer);
    }
    if (c == -1 || c == '\n' || c == '\r') {
      parse_error(parser, "unterminated literal string");
      break;
    }
    if (c != '\t' && (c < 0x20 || c == 0x7f)) {
      parse_error(parser, "control character in string");
      break;
    }
    if (buffer_put_char(parser, &buffer, c)) break;
    advance(parser);
  }
  free(buffer.data);
  return text;
}

static toml_text_t parse_multiline_literal_string(toml_parser_t *parser) {
  toml_buffer_t buffer = {0};
  toml_text_t text = {NULL, 0};
  advance(parser);
  advance(parser);
  advance(parser); /* ''' */
  if (peek(parser) == '\r' && peek_at(parser, 1) == '\n') {
    advance(parser);
    advance(parser);
  } else if (peek(parser) == '\n') {
    advance(parser);
  }
  for (;;) {
    int c = peek(parser);
    if (c == -1) {
      parse_error(parser, "unterminated multi-line literal string");
      break;
    }
    if (c == '\'') {
      size_t run = 0;
      while (peek_at(parser, run) == '\'') run++;
      if (run >= 3) {
        size_t i;
        if (run - 3 > 2) {
          parse_error(parser, "too many apostrophes at end of multi-line literal string");
          break;
        }
        for (i = 0; i < run - 3; i++) {
          if (buffer_put_char(parser, &buffer, '\'')) goto fail;
        }
        for (i = 0; i < run; i++) advance(parser);
        return buffer_finish(parser, &buffer);
      }
      while (run--) {
        if (buffer_put_char(parser, &buffer, '\'')) goto fail;
        advance(parser);
      }
      continue;
    }
    if (c == '\r') {
      if (peek_at(parser, 1) != '\n') {
        parse_error(parser, "carriage return without line feed");
        break;
      }
      advance(parser);
      advance(parser);
      if (buffer_put_char(parser, &buffer, '\n')) break;
      continue;
    }
    if (c == '\n' || c == '\t' || (c >= 0x20 && c != 0x7f)) {
      if (buffer_put_char(parser, &buffer, c)) break;
      advance(parser);
      continue;
    }
    parse_error(parser, "control character in string");
    break;
  }
fail:
  free(buffer.data);
  return text;
}

/* ------------------------------------------------------------------ */
/* Numbers                                                            */
/* ------------------------------------------------------------------ */

/* Validate a bare token as an integer and store its value. */
static int parse_integer_token(const char *text, size_t length, long long *out) {
  size_t i = 0;
  int negative = 0;
  int base = 10;
  unsigned long long limit;
  unsigned long long value = 0;

  if (i < length && (text[i] == '+' || text[i] == '-')) {
    negative = text[i] == '-';
    i++;
  }
  if (i + 1 < length && text[i] == '0' &&
      (text[i + 1] == 'x' || text[i + 1] == 'o' || text[i + 1] == 'b')) {
    int j;
    if (negative || (i > 0 && text[0] == '+')) return 0;
    switch (text[i + 1]) {
      case 'x': base = 16; break;
      case 'o': base = 8; break;
      default: base = 2; break;
    }
    i += 2;
    if (i >= length) return 0;
    for (j = 0; i < length; i++, j++) {
      int digit;
      if (text[i] == '_') {
        if (j == 0 || i + 1 >= length) return 0;
        i++;
      }
      if (base == 16) {
        digit = hex_value((unsigned char) text[i]);
      } else {
        digit = (text[i] >= '0' && text[i] < '0' + base) ? text[i] - '0' : -1;
      }
      if (digit < 0) return 0;
      if (value > (9223372036854775807ull - (unsigned long long) digit) / (unsigned long long) base) {
        return 0;
      }
      value = value * (unsigned long long) base + (unsigned long long) digit;
    }
    *out = (long long) value;
    return 1;
  }

  if (i >= length || !is_digit((unsigned char) text[i])) return 0;
  if (text[i] == '0' && i + 1 < length) return 0;
  for (; i < length; i++) {
    int digit;
    if (text[i] == '_') {
      if (i + 1 >= length || !is_digit((unsigned char) text[i + 1])) return 0;
      i++;
    }
    if (!is_digit((unsigned char) text[i])) return 0;
    digit = text[i] - '0';
    limit = negative ? 9223372036854775808ull : 9223372036854775807ull;
    if (value > (limit - (unsigned long long) digit) / 10ull) return 0;
    value = value * 10ull + (unsigned long long) digit;
  }
  if (negative) {
    *out = value == 9223372036854775808ull ? LLONG_MIN : -(long long) value;
  } else {
    *out = (long long) value;
  }
  return 1;
}

static int parse_special_float(const char *text, size_t length, double *out) {
  size_t i = 0;
  int negative = 0;
  if (i < length && (text[i] == '+' || text[i] == '-')) {
    negative = text[i] == '-';
    i++;
  }
  if (length - i == 3 && memcmp(text + i, "inf", 3) == 0) {
    *out = negative ? -strtod("inf", NULL) : strtod("inf", NULL);
    return 1;
  }
  if (length - i == 3 && memcmp(text + i, "nan", 3) == 0) {
    *out = strtod("nan", NULL);
    return 1;
  }
  return 0;
}

/* Validate a bare token as a float and store its value. */
static int parse_float_token(const char *text, size_t length, double *out) {
  char *clean;
  size_t i = 0;
  size_t j = 0;
  int has_fraction = 0;
  int has_exponent = 0;

  if (parse_special_float(text, length, out)) return 1;

  clean = (char *) malloc(length + 1);
  if (!clean) return -1;
  if (i < length && (text[i] == '+' || text[i] == '-')) clean[j++] = text[i++];
  if (i >= length || !is_digit((unsigned char) text[i])) goto fail;
  if (text[i] == '0' && i + 1 < length &&
      text[i + 1] != '.' && text[i + 1] != 'e' && text[i + 1] != 'E') {
    goto fail;
  }
  while (i < length && is_digit((unsigned char) text[i])) clean[j++] = text[i++];
  if (i < length && text[i] == '_') {
    /* Underscores only between digits of the integer part. */
    while (i < length && text[i] == '_') {
      if (!is_digit((unsigned char) text[i + 1])) goto fail;
      i++;
      while (i < length && is_digit((unsigned char) text[i])) {
        clean[j++] = text[i++];
      }
    }
  }
  if (i < length && text[i] == '.') {
    has_fraction = 1;
    clean[j++] = '.';
    i++;
    if (i >= length || !is_digit((unsigned char) text[i])) goto fail;
    while (i < length && text[i] != 'e' && text[i] != 'E') {
      if (text[i] == '_') {
        if (!is_digit((unsigned char) text[i + 1])) goto fail;
        i++;
        continue;
      }
      if (!is_digit((unsigned char) text[i])) goto fail;
      clean[j++] = text[i++];
    }
  }
  if (i < length && (text[i] == 'e' || text[i] == 'E')) {
    has_exponent = 1;
    clean[j++] = text[i++];
    if (i < length && (text[i] == '+' || text[i] == '-')) clean[j++] = text[i++];
    if (i >= length || !is_digit((unsigned char) text[i])) goto fail;
    while (i < length) {
      if (text[i] == '_') {
        if (!is_digit((unsigned char) text[i + 1])) goto fail;
        i++;
      }
      if (i >= length || !is_digit((unsigned char) text[i])) goto fail;
      clean[j++] = text[i++];
    }
  }
  if (!has_fraction && !has_exponent) goto fail;
  if (i != length) goto fail;
  clean[j] = '\0';
  *out = strtod(clean, NULL);
  free(clean);
  return 1;
fail:
  free(clean);
  return 0;
}

/* ------------------------------------------------------------------ */
/* Date and time                                                      */
/* ------------------------------------------------------------------ */

static int is_leap_year(int year) {
  return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

static int days_in_month(int year, int month) {
  static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month == 2 && is_leap_year(year)) return 29;
  return days[month - 1];
}

static int read_padded_int(const char *text, size_t length, size_t *i, int digits, int *out) {
  int value = 0;
  int k;
  if (*i + (size_t) digits > length) return 0;
  for (k = 0; k < digits; k++) {
    if (!is_digit((unsigned char) text[*i + k])) return 0;
    value = value * 10 + (text[*i + k] - '0');
  }
  *i += (size_t) digits;
  *out = value;
  return 1;
}

/* partial-time = time-hour ":" time-minute [ ":" time-second [ time-secfrac ] ] */
static int parse_partial_time(const char *text, size_t length, size_t *i,
                              toml_datetime_t *out) {
  if (!read_padded_int(text, length, i, 2, &out->hour)) return 0;
  if (out->hour > 23) return 0;
  if (*i >= length || text[(*i)++] != ':') return 0;
  if (!read_padded_int(text, length, i, 2, &out->minute)) return 0;
  if (out->minute > 59) return 0;
  if (*i < length && text[*i] == ':') {
    (*i)++;
    if (!read_padded_int(text, length, i, 2, &out->second)) return 0;
    if (out->second > 60) return 0;
    if (*i < length && text[*i] == '.') {
      int digits = 0;
      int scale = 100000;
      (*i)++;
      if (*i >= length || !is_digit((unsigned char) text[*i])) return 0;
      while (*i < length && is_digit((unsigned char) text[*i])) {
        if (digits < 6) {
          out->microsecond += (text[*i] - '0') * scale;
          scale /= 10;
        }
        digits++;
        (*i)++;
      }
    }
  }
  return 1;
}

static int parse_datetime_token(const char *text, size_t length, toml_datetime_t *out) {
  size_t i = 0;
  int have_date = 0;
  int have_offset = 0;

  memset(out, 0, sizeof(*out));

  /* full-date */
  if (length >= 10 && text[4] == '-') {
    if (!read_padded_int(text, length, &i, 4, &out->year)) return 0;
    if (i >= length || text[i++] != '-') return 0;
    if (!read_padded_int(text, length, &i, 2, &out->month)) return 0;
    if (out->month < 1 || out->month > 12) return 0;
    if (i >= length || text[i++] != '-') return 0;
    if (!read_padded_int(text, length, &i, 2, &out->day)) return 0;
    if (out->day < 1 || out->day > days_in_month(out->year, out->month)) return 0;
    have_date = 1;
    if (i == length) {
      out->kind = TOML_LOCAL_DATE;
      return 1;
    }
    if (text[i] != 'T' && text[i] != 't' && text[i] != ' ') return 0;
    i++;
  }

  /* partial-time (required after a date separator, and on its own) */
  if (!parse_partial_time(text, length, &i, out)) return 0;

  /* offset, only valid as part of an offset date-time */
  if (i < length) {
    if (!have_date) return 0;
    if (text[i] == 'Z' || text[i] == 'z') {
      i++;
    } else if (text[i] == '+' || text[i] == '-') {
      int sign = text[i] == '-' ? -1 : 1;
      int offset_hour = 0;
      int offset_minute = 0;
      i++;
      if (!read_padded_int(text, length, &i, 2, &offset_hour)) return 0;
      if (i >= length || text[i++] != ':') return 0;
      if (!read_padded_int(text, length, &i, 2, &offset_minute)) return 0;
      if (offset_hour > 23 || offset_minute > 59) return 0;
      out->offset_minutes = sign * (offset_hour * 60 + offset_minute);
    } else {
      return 0;
    }
    have_offset = 1;
  }

  if (i != length) return 0;
  if (have_date) {
    out->kind = have_offset ? TOML_OFFSET_DATETIME : TOML_LOCAL_DATETIME;
  } else {
    out->kind = TOML_LOCAL_TIME;
  }
  return 1;
}

/* ------------------------------------------------------------------ */
/* Keys                                                               */
/* ------------------------------------------------------------------ */

static void keypath_free(toml_keypath_t *path) {
  size_t i;
  for (i = 0; i < path->count; i++) free(path->keys[i].text);
  free(path->keys);
  path->keys = NULL;
  path->count = 0;
  path->capacity = 0;
}

static toml_text_t parse_simple_key(toml_parser_t *parser) {
  toml_text_t text = {NULL, 0};
  int c = peek(parser);
  if (c == '"' || c == '\'') {
    if (peek_at(parser, 1) == c && peek_at(parser, 2) == c) {
      parse_error(parser, "multi-line strings cannot be used as keys");
      return text;
    }
    return c == '"' ? parse_basic_string(parser) : parse_literal_string(parser);
  }
  if (is_bare_key_char(c)) {
    toml_buffer_t buffer = {0};
    while (is_bare_key_char(peek(parser))) {
      if (buffer_put_char(parser, &buffer, advance(parser))) {
        free(buffer.data);
        return text;
      }
    }
    return buffer_finish(parser, &buffer);
  }
  parse_error(parser, "expected a key");
  return text;
}

static int parse_key_path(toml_parser_t *parser, toml_keypath_t *path) {
  memset(path, 0, sizeof(*path));
  for (;;) {
    toml_text_t key = parse_simple_key(parser);
    if (!key.text) goto fail;
    if (path->count >= TOML_MAX_NESTING) {
      free(key.text);
      parse_error(parser, "dotted key is nested too deeply");
      goto fail;
    }
    if (path->count == path->capacity) {
      size_t capacity = path->capacity ? path->capacity * 2 : 4;
      toml_text_t *keys = (toml_text_t *) realloc(path->keys, capacity * sizeof(*keys));
      if (!keys) {
        free(key.text);
        parse_oom(parser);
        goto fail;
      }
      path->keys = keys;
      path->capacity = capacity;
    }
    path->keys[path->count++] = key;
    skip_ws(parser);
    if (peek(parser) != '.') return 0;
    advance(parser);
    skip_ws(parser);
  }
fail:
  keypath_free(path);
  return -1;
}

/* ------------------------------------------------------------------ */
/* Value construction                                                 */
/* ------------------------------------------------------------------ */

static toml_t *parse_value(toml_parser_t *parser);
static toml_t *parse_array(toml_parser_t *parser);
static toml_t *parse_inline_table(toml_parser_t *parser);
static int assign_key_value(toml_parser_t *parser, toml_t *table, toml_keypath_t *path,
                            toml_t *value);

static toml_t *value_from_string(toml_parser_t *parser, toml_text_t string) {
  toml_t *node = node_new(parser, TOML_STRING);
  if (!node) {
    free(string.text);
    return NULL;
  }
  node->string = string.text;
  node->string_length = string.length;
  return node;
}

static toml_t *parse_bare_value(toml_parser_t *parser) {
  size_t start = parser->position;
  size_t length;
  const char *text;
  toml_t *node;

  while (is_bare_value_char(peek(parser))) advance(parser);
  /* A date-time may use a single space between the date and the time. */
  length = parser->position - start;
  if (length == 10 && parser->text[start + 4] == '-' &&
      peek(parser) == ' ' && is_digit(peek_at(parser, 1))) {
    advance(parser);
    while (is_bare_value_char(peek(parser))) advance(parser);
    length = parser->position - start;
  }
  text = parser->text + start;
  if (length == 0) {
    parse_error(parser, "expected a value");
    return NULL;
  }

  if (length == 4 && memcmp(text, "true", 4) == 0) {
    node = node_new(parser, TOML_BOOLEAN);
    if (node) node->boolean = 1;
    return node;
  }
  if (length == 5 && memcmp(text, "false", 5) == 0) {
    return node_new(parser, TOML_BOOLEAN);
  }
  {
    double floating;
    int float_result = parse_float_token(text, length, &floating);
    if (float_result < 0) {
      parse_oom(parser);
      return NULL;
    }
    if (float_result > 0) {
      node = node_new(parser, TOML_FLOAT);
      if (node) node->floating = floating;
      return node;
    }
  }
  if (length >= 5 && (text[4] == '-' || text[2] == ':')) {
    toml_datetime_t datetime;
    if (!parse_datetime_token(text, length, &datetime)) {
      parse_error(parser, "invalid date-time value");
      return NULL;
    }
    node = node_new(parser, TOML_DATETIME);
    if (node) node->datetime = datetime;
    return node;
  }
  {
    long long integer;
    if (!parse_integer_token(text, length, &integer)) {
      parse_error(parser, "invalid value");
      return NULL;
    }
    node = node_new(parser, TOML_INTEGER);
    if (node) node->integer = integer;
    return node;
  }
}

static toml_t *parse_value_inner(toml_parser_t *parser) {
  int c = peek(parser);
  toml_text_t string;
  if (c == '"') {
    if (peek_at(parser, 1) == '"' && peek_at(parser, 2) == '"') {
      string = parse_multiline_basic_string(parser);
    } else {
      string = parse_basic_string(parser);
    }
    return string.text ? value_from_string(parser, string) : NULL;
  }
  if (c == '\'') {
    if (peek_at(parser, 1) == '\'' && peek_at(parser, 2) == '\'') {
      string = parse_multiline_literal_string(parser);
    } else {
      string = parse_literal_string(parser);
    }
    return string.text ? value_from_string(parser, string) : NULL;
  }
  if (c == '[') return parse_array(parser);
  if (c == '{') return parse_inline_table(parser);
  return parse_bare_value(parser);
}

static toml_t *parse_value(toml_parser_t *parser) {
  toml_t *value;
  if (parser->depth >= TOML_MAX_NESTING) {
    parse_error(parser, "value is nested too deeply");
    return NULL;
  }
  parser->depth++;
  value = parse_value_inner(parser);
  parser->depth--;
  return value;
}

static toml_t *parse_array(toml_parser_t *parser) {
  toml_t *array = node_new(parser, TOML_ARRAY);
  if (!array) return NULL;
  advance(parser); /* '[' */
  if (skip_ws_comments_newlines(parser)) goto fail;
  if (peek(parser) == ']') {
    advance(parser);
    return array;
  }
  for (;;) {
    toml_t *value = parse_value(parser);
    int c;
    if (!value) goto fail;
    if (node_append(parser, array, value)) {
      toml_free(value);
      goto fail;
    }
    if (skip_ws_comments_newlines(parser)) goto fail;
    c = peek(parser);
    if (c == ',') {
      advance(parser);
      if (skip_ws_comments_newlines(parser)) goto fail;
      if (peek(parser) == ']') {
        advance(parser);
        return array;
      }
      continue;
    }
    if (c == ']') {
      advance(parser);
      return array;
    }
    parse_error(parser, "expected ',' or ']' in array");
    goto fail;
  }
fail:
  toml_free(array);
  return NULL;
}

static toml_t *parse_inline_table(toml_parser_t *parser) {
  toml_t *table = node_new(parser, TOML_TABLE);
  if (!table) return NULL;
  advance(parser); /* '{' */
  if (skip_ws_comments_newlines(parser)) goto fail;
  if (peek(parser) == '}') {
    advance(parser);
    table->flags |= F_SEALED;
    return table;
  }
  for (;;) {
    toml_keypath_t path;
    toml_t *value;
    int c;
    if (parse_key_path(parser, &path)) goto fail;
    skip_ws(parser);
    if (peek(parser) != '=') {
      parse_error(parser, "expected '=' after key");
      keypath_free(&path);
      goto fail;
    }
    advance(parser);
    skip_ws(parser);
    value = parse_value(parser);
    if (!value) {
      keypath_free(&path);
      goto fail;
    }
    if (assign_key_value(parser, table, &path, value)) {
      toml_free(value);
      keypath_free(&path);
      goto fail;
    }
    keypath_free(&path);
    if (skip_ws_comments_newlines(parser)) goto fail;
    c = peek(parser);
    if (c == ',') {
      advance(parser);
      if (skip_ws_comments_newlines(parser)) goto fail;
      if (peek(parser) == '}') {
        advance(parser);
        table->flags |= F_SEALED;
        return table;
      }
      continue;
    }
    if (c == '}') {
      advance(parser);
      table->flags |= F_SEALED;
      return table;
    }
    parse_error(parser, "expected ',' or '}' in inline table");
    goto fail;
  }
fail:
  toml_free(table);
  return NULL;
}

/* ------------------------------------------------------------------ */
/* Assigning keys                                                     */
/* ------------------------------------------------------------------ */

/* Place value under table, creating intermediate tables for a dotted key.
 * Intermediate tables must have been created by dotted keys themselves;
 * headers, inline tables and other values are closed to this form. */
static int assign_key_value(toml_parser_t *parser, toml_t *table, toml_keypath_t *path,
                            toml_t *value) {
  toml_t *current = table;
  size_t i;
  for (i = 0; i + 1 < path->count; i++) {
    toml_text_t key = path->keys[i];
    toml_t *child = node_find_n(current, key.text, key.length);
    if (!child) {
      path->keys[i].text = NULL; /* ownership passes to create_child_table */
      path->keys[i].length = 0;
      child = create_child_table(parser, current, key, F_DOTTED);
      if (!child) return -1;
      current = child;
      continue;
    }
    path->keys[i].text = NULL;
    path->keys[i].length = 0;
    if (child->type != TOML_TABLE) {
      parse_error(parser, "key '%.*s' is not a table", (int) key.length, key.text);
      free(key.text);
      return -1;
    }
    if (child->flags & F_SEALED) {
      parse_error(parser, "cannot extend inline table '%.*s'", (int) key.length, key.text);
      free(key.text);
      return -1;
    }
    if (!(child->flags & F_DOTTED)) {
      parse_error(parser, "cannot use a dotted key to extend table '%.*s'", (int) key.length,
                  key.text);
      free(key.text);
      return -1;
    }
    free(key.text);
    current = child;
  }
  {
    toml_text_t key = path->keys[path->count - 1];
    if (node_find_n(current, key.text, key.length)) {
      parse_error(parser, "duplicate key '%.*s'", (int) key.length, key.text);
      return -1;
    }
    value->key = key.text;
    value->key_length = key.length;
    path->keys[path->count - 1].text = NULL;
    path->keys[path->count - 1].length = 0;
    if (node_append(parser, current, value)) return -1;
  }
  return 0;
}

/* Walk a table header path, creating implicit tables as needed, and return
 * the table the header defines (or whose element it appends to). */
static toml_t *walk_table_header(toml_parser_t *parser, toml_t *root, toml_keypath_t *path,
                                 int is_array) {
  toml_t *current = root;
  toml_t *child;
  size_t i;
  for (i = 0; i + 1 < path->count; i++) {
    toml_text_t key = path->keys[i];
    child = node_find_n(current, key.text, key.length);
    if (!child) {
      path->keys[i].text = NULL; /* ownership passes to create_child_table */
      path->keys[i].length = 0;
      child = create_child_table(parser, current, key, F_IMPLICIT);
      if (!child) return NULL;
      current = child;
      continue;
    }
    free(key.text);
    path->keys[i].text = NULL;
    path->keys[i].length = 0;
    if (child->type == TOML_TABLE) {
      if (child->flags & F_SEALED) {
        parse_error(parser, "cannot extend inline table");
        return NULL;
      }
      current = child;
      continue;
    }
    if (child->type == TOML_ARRAY && (child->flags & F_AOT) && child->count > 0) {
      current = child->items[child->count - 1];
      continue;
    }
    parse_error(parser, "key is not a table");
    return NULL;
  }
  {
    toml_text_t key = path->keys[path->count - 1];
    child = node_find_n(current, key.text, key.length);
    path->keys[path->count - 1].text = NULL;
    path->keys[path->count - 1].length = 0;
    if (!child) {
      if (is_array) {
        toml_t *array = node_new(parser, TOML_ARRAY);
        toml_t *element;
        if (!array) {
          free(key.text);
          return NULL;
        }
        array->key = key.text;
        array->key_length = key.length;
        array->flags = F_AOT;
        if (node_append(parser, current, array)) {
          toml_free(array);
          return NULL;
        }
        element = create_child_table(parser, array, (toml_text_t) {NULL, 0}, F_DEFINED);
        if (!element) return NULL;
        return element;
      }
      return create_child_table(parser, current, key, F_DEFINED);
    }
    if (!is_array) {
      if (child->type == TOML_TABLE && (child->flags & F_IMPLICIT) &&
          !(child->flags & F_DEFINED)) {
        child->flags |= F_DEFINED;
        free(key.text);
        return child;
      }
      if (child->type == TOML_TABLE) {
        parse_error(parser, "table '%.*s' is already defined", (int) key.length, key.text);
      } else if (child->type == TOML_ARRAY) {
        parse_error(parser, "cannot define table '%.*s' over an array", (int) key.length, key.text);
      } else {
        parse_error(parser, "cannot redefine key '%.*s' as a table", (int) key.length, key.text);
      }
      free(key.text);
      return NULL;
    }
    if (child->type == TOML_ARRAY && (child->flags & F_AOT)) {
      toml_t *element = create_child_table(parser, child, (toml_text_t) {NULL, 0}, F_DEFINED);
      free(key.text);
      return element;
    }
    parse_error(parser, "cannot append to array '%.*s' with a table header", (int) key.length,
                key.text);
    free(key.text);
    return NULL;
  }
}

/* ------------------------------------------------------------------ */
/* Document                                                           */
/* ------------------------------------------------------------------ */

static toml_t *parse_table_header(toml_parser_t *parser, toml_t *root) {
  toml_keypath_t path;
  toml_t *table;
  int is_array = 0;
  advance(parser); /* '[' */
  if (peek(parser) == '[') {
    advance(parser);
    is_array = 1;
  }
  skip_ws(parser);
  if (parse_key_path(parser, &path)) return NULL;
  skip_ws(parser);
  if (peek(parser) != ']') {
    parse_error(parser, "expected ']' to close a table header");
    keypath_free(&path);
    return NULL;
  }
  advance(parser);
  if (is_array) {
    if (peek(parser) != ']') {
      parse_error(parser, "expected ']]' to close an array-of-tables header");
      keypath_free(&path);
      return NULL;
    }
    advance(parser);
  }
  table = walk_table_header(parser, root, &path, is_array);
  keypath_free(&path);
  return table;
}

static toml_t *parse_document(toml_parser_t *parser) {
  toml_t *root = node_new(parser, TOML_TABLE);
  toml_t *table = root;
  if (!root) return NULL;
  root->flags = F_DEFINED;
  for (;;) {
    int c;
    skip_ws(parser);
    c = peek(parser);
    if (c == -1) break;
    if (c == '\n' || c == '\r') {
      if (skip_newline(parser)) break;
      continue;
    }
    if (c == '#') {
      skip_comment(parser);
      if (parser->failed) break;
      continue;
    }
    if (c == '[') {
      toml_t *next = parse_table_header(parser, root);
      if (!next) break;
      table = next;
    } else {
      toml_keypath_t path;
      toml_t *value;
      if (parse_key_path(parser, &path)) break;
      skip_ws(parser);
      if (peek(parser) != '=') {
        parse_error(parser, "expected '=' after key");
        keypath_free(&path);
        break;
      }
      advance(parser);
      skip_ws(parser);
      value = parse_value(parser);
      if (!value) {
        keypath_free(&path);
        break;
      }
      if (assign_key_value(parser, table, &path, value)) {
        toml_free(value);
        keypath_free(&path);
        break;
      }
      keypath_free(&path);
    }
    skip_ws(parser);
    if (peek(parser) == '#') {
      skip_comment(parser);
      if (parser->failed) break;
    }
    c = peek(parser);
    if (c == -1) break;
    if (c == '\n' || c == '\r') {
      if (skip_newline(parser)) break;
      continue;
    }
    parse_error(parser, "expected a newline or comment after the statement");
    break;
  }
  if (parser->failed) {
    toml_free(root);
    return NULL;
  }
  return root;
}

/* ------------------------------------------------------------------ */
/* Public API                                                         */
/* ------------------------------------------------------------------ */

toml_t *toml_parse(const char *text, size_t length, toml_error_t *error) {
  toml_error_t local_error;
  toml_parser_t parser;
  toml_t *root;
  char *copy;
  size_t invalid;

  if (!error) error = &local_error;
  memset(error, 0, sizeof(*error));
  if (!text) {
    set_error(error, 0, 0, "no input text");
    return NULL;
  }

  copy = (char *) malloc(length + 1);
  if (!copy) {
    set_error(error, 0, 0, "out of memory");
    return NULL;
  }
  memcpy(copy, text, length);
  copy[length] = '\0';

  invalid = utf8_invalid_offset(copy, length);
  if (invalid < length) {
    size_t i;
    int line = 1;
    int column = 1;
    for (i = 0; i < invalid; i++) {
      if (copy[i] == '\n') {
        line++;
        column = 1;
      } else {
        column++;
      }
    }
    set_error(error, line, column, "invalid UTF-8 byte sequence");
    free(copy);
    return NULL;
  }

  memset(&parser, 0, sizeof(parser));
  parser.text = copy;
  parser.length = length;
  parser.line = 1;
  parser.column = 1;
  parser.error = error;
  if (length >= 3 && (unsigned char) copy[0] == 0xef &&
      (unsigned char) copy[1] == 0xbb && (unsigned char) copy[2] == 0xbf) {
    parser.position = 3;
    parser.column = 4;
  }

  root = parse_document(&parser);
  free(copy);
  return root;
}

toml_t *toml_load(const char *path, toml_error_t *error) {
  toml_error_t local_error;
  FILE *file;
  long size;
  char *buffer;
  size_t read_count;
  toml_t *root;

  if (!error) error = &local_error;
  memset(error, 0, sizeof(*error));
  if (!path) {
    set_error(error, 0, 0, "no file path");
    return NULL;
  }

  file = fopen(path, "rb");
  if (!file) {
    set_error(error, 0, 0, "cannot open '%s'", path);
    return NULL;
  }
  if (fseek(file, 0, SEEK_END) != 0 || (size = ftell(file)) < 0) {
    fclose(file);
    set_error(error, 0, 0, "cannot determine the size of '%s'", path);
    return NULL;
  }
  rewind(file);
  buffer = (char *) malloc((size_t) size + 1);
  if (!buffer) {
    fclose(file);
    set_error(error, 0, 0, "out of memory");
    return NULL;
  }
  read_count = fread(buffer, 1, (size_t) size, file);
  fclose(file);
  if (read_count != (size_t) size) {
    free(buffer);
    set_error(error, 0, 0, "cannot read '%s'", path);
    return NULL;
  }

  root = toml_parse(buffer, (size_t) size, error);
  free(buffer);
  return root;
}

toml_type_t toml_type(const toml_t *value) {
  return value ? value->type : TOML_INVALID;
}

const char *toml_type_name(toml_type_t type) {
  switch (type) {
    case TOML_TABLE: return "table";
    case TOML_ARRAY: return "array";
    case TOML_STRING: return "string";
    case TOML_INTEGER: return "integer";
    case TOML_FLOAT: return "float";
    case TOML_BOOLEAN: return "boolean";
    case TOML_DATETIME: return "datetime";
    default: return "invalid";
  }
}

const toml_t *toml_get(const toml_t *value, const char *dotted_path) {
  const toml_t *current = value;
  const char *segment = dotted_path;
  if (!value || !dotted_path) return NULL;
  for (;;) {
    const char *dot;
    size_t length;
    if (*segment == '\0') return current; /* empty path returns the value */
    dot = strchr(segment, '.');
    length = dot ? (size_t) (dot - segment) : strlen(segment);
    if (length == 0) return NULL;
    current = node_find_n(current, segment, length);
    if (!current) return NULL;
    if (!dot) return current;
    if (dot[1] == '\0') return NULL; /* trailing dot */
    segment = dot + 1;
  }
}

const toml_t *toml_child(const toml_t *table, const char *key) {
  if (!table || !key) return NULL;
  return node_find(table, key);
}

size_t toml_count(const toml_t *value) {
  if (!value) return 0;
  if (value->type != TOML_TABLE && value->type != TOML_ARRAY) return 0;
  return value->count;
}

const toml_t *toml_at(const toml_t *value, size_t index) {
  if (!value) return NULL;
  if (value->type != TOML_TABLE && value->type != TOML_ARRAY) return NULL;
  if (index >= value->count) return NULL;
  return value->items[index];
}

const char *toml_key(const toml_t *table, size_t index) {
  if (!table || table->type != TOML_TABLE) return NULL;
  if (index >= table->count) return NULL;
  return table->items[index]->key;
}

size_t toml_key_length(const toml_t *table, size_t index) {
  if (!table || table->type != TOML_TABLE) return 0;
  if (index >= table->count) return 0;
  return table->items[index]->key_length;
}

const char *toml_string(const toml_t *value) {
  if (!value || value->type != TOML_STRING) return NULL;
  return value->string;
}

size_t toml_string_length(const toml_t *value) {
  if (!value || value->type != TOML_STRING) return 0;
  return value->string_length;
}

long long toml_integer(const toml_t *value, long long fallback) {
  if (!value || value->type != TOML_INTEGER) return fallback;
  return value->integer;
}

double toml_float(const toml_t *value, double fallback) {
  if (!value || value->type != TOML_FLOAT) return fallback;
  return value->floating;
}

int toml_boolean(const toml_t *value, int fallback) {
  if (!value || value->type != TOML_BOOLEAN) return fallback;
  return value->boolean;
}

const toml_datetime_t *toml_datetime(const toml_t *value) {
  if (!value || value->type != TOML_DATETIME) return NULL;
  return &value->datetime;
}

const char *toml_get_string(const toml_t *toml, const char *path, const char *fallback) {
  const char *value = toml_string(toml_get(toml, path));
  return value ? value : fallback;
}

long long toml_get_integer(const toml_t *toml, const char *path, long long fallback) {
  return toml_integer(toml_get(toml, path), fallback);
}

double toml_get_float(const toml_t *toml, const char *path, double fallback) {
  return toml_float(toml_get(toml, path), fallback);
}

int toml_get_boolean(const toml_t *toml, const char *path, int fallback) {
  return toml_boolean(toml_get(toml, path), fallback);
}

const toml_datetime_t *toml_get_datetime(const toml_t *toml, const char *path) {
  return toml_datetime(toml_get(toml, path));
}
