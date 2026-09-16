/*
 * Tests for toml.h.  Build and run with `make test`.
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "toml.h"

static int checks = 0;
static int failures = 0;

#define CHECK(condition)                                                       \
  do {                                                                         \
    checks++;                                                                  \
    if (!(condition)) {                                                        \
      failures++;                                                              \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);              \
    }                                                                          \
  } while (0)

static toml_t *parse_ok(const char *text, int line) {
  toml_error_t error;
  toml_t *doc;
  memset(&error, 0, sizeof(error));
  doc = toml_parse(text, strlen(text), &error);
  checks++;
  if (!doc) {
    failures++;
    printf("FAIL %s:%d: expected success, got error at %d:%d: %s\n", __FILE__,
           line, error.line, error.column, error.message);
  }
  return doc;
}

#define PARSE(text) parse_ok(text, __LINE__)

static void parse_fails(const char *text, int line) {
  toml_error_t error;
  toml_t *doc;
  memset(&error, 0, sizeof(error));
  doc = toml_parse(text, strlen(text), &error);
  checks++;
  if (doc) {
    failures++;
    printf("FAIL %s:%d: expected an error for: %s\n", __FILE__, line, text);
    toml_free(doc);
  } else if (error.message[0] == '\0') {
    failures++;
    printf("FAIL %s:%d: error message was empty\n", __FILE__, line);
  }
}

#define PARSE_FAILS(text) parse_fails(text, __LINE__)

static void test_integers(void) {
  toml_t *doc = PARSE(
      "a = 0\n"
      "b = +99\n"
      "c = -17\n"
      "d = 1_000\n"
      "e = 5_349_221\n"
      "f = 0xDEADBEEF\n"
      "g = 0xdead_beef\n"
      "h = 0o755\n"
      "i = 0b1101_0110\n"
      "j = -0\n"
      "k = 9223372036854775807\n"
      "l = -9223372036854775808\n");
  CHECK(toml_get_integer(doc, "a", -1) == 0);
  CHECK(toml_get_integer(doc, "b", -1) == 99);
  CHECK(toml_get_integer(doc, "c", -1) == -17);
  CHECK(toml_get_integer(doc, "d", -1) == 1000);
  CHECK(toml_get_integer(doc, "e", -1) == 5349221);
  CHECK(toml_get_integer(doc, "f", -1) == 0xdeadbeefLL);
  CHECK(toml_get_integer(doc, "g", -1) == 0xdeadbeefLL);
  CHECK(toml_get_integer(doc, "h", -1) == 0755);
  CHECK(toml_get_integer(doc, "i", -1) == 0xd6);
  CHECK(toml_get_integer(doc, "j", -1) == 0);
  CHECK(toml_get_integer(doc, "k", -1) == 9223372036854775807LL);
  CHECK(toml_integer(toml_get(doc, "l"), 0) == -9223372036854775807LL - 1);
  CHECK(toml_type(toml_get(doc, "a")) == TOML_INTEGER);
  toml_free(doc);

  PARSE_FAILS("a = 01\n");
  PARSE_FAILS("a = 1__2\n");
  PARSE_FAILS("a = 1_\n");
  PARSE_FAILS("a = _1\n");
  PARSE_FAILS("a = 0x\n");
  PARSE_FAILS("a = 0x_1\n");
  PARSE_FAILS("a = 0b2\n");
  PARSE_FAILS("a = 0o8\n");
  PARSE_FAILS("a = +0x1\n");
  PARSE_FAILS("a = -0o1\n");
  PARSE_FAILS("a = 9223372036854775808\n");
  PARSE_FAILS("a = -9223372036854775809\n");
  PARSE_FAILS("a = 1 2\n");
  PARSE_FAILS("a = 1-2\n");
}

static void test_floats(void) {
  toml_t *doc = PARSE(
      "a = +1.0\n"
      "b = 3.1415\n"
      "c = -0.01\n"
      "d = 5e+22\n"
      "e = 1e06\n"
      "f = -2E-2\n"
      "g = 6.626e-34\n"
      "h = 224_617.445_991_228\n"
      "i = inf\n"
      "j = +inf\n"
      "k = -inf\n"
      "l = nan\n"
      "m = -nan\n"
      "n = 0.0\n"
      "o = -0.0\n"
      "p = 1e400\n");
  CHECK(toml_get_float(doc, "a", 0) == 1.0);
  CHECK(toml_get_float(doc, "b", 0) == 3.1415);
  CHECK(toml_get_float(doc, "c", 0) == -0.01);
  CHECK(toml_get_float(doc, "d", 0) == 5e22);
  CHECK(toml_get_float(doc, "e", 0) == 1e6);
  CHECK(toml_get_float(doc, "f", 0) == -0.02);
  CHECK(toml_get_float(doc, "g", 0) == 6.626e-34);
  CHECK(toml_get_float(doc, "h", 0) == 224617.445991228);
  CHECK(isinf(toml_get_float(doc, "i", 0)) && toml_get_float(doc, "i", 0) > 0);
  CHECK(isinf(toml_get_float(doc, "j", 0)) && toml_get_float(doc, "j", 0) > 0);
  CHECK(isinf(toml_get_float(doc, "k", 0)) && toml_get_float(doc, "k", 0) < 0);
  CHECK(isnan(toml_get_float(doc, "l", 0)));
  CHECK(isnan(toml_get_float(doc, "m", 0)));
  CHECK(toml_get_float(doc, "n", 1) == 0.0);
  CHECK(toml_get_float(doc, "o", 1) == 0.0);
  CHECK(isinf(toml_get_float(doc, "p", 0)));
  CHECK(toml_type(toml_get(doc, "e")) == TOML_FLOAT);
  toml_free(doc);

  PARSE_FAILS("a = .7\n");
  PARSE_FAILS("a = 7.\n");
  PARSE_FAILS("a = 3.e+20\n");
  PARSE_FAILS("a = 01.0\n");
  PARSE_FAILS("a = 1_.0\n");
  PARSE_FAILS("a = 1._0\n");
  PARSE_FAILS("a = 1e\n");
  PARSE_FAILS("a = 1e_\n");
  PARSE_FAILS("a = 1e1_\n");
  PARSE_FAILS("a = INF\n");
  PARSE_FAILS("a = NaN\n");
  PARSE_FAILS("a = 1e+_2\n");
}

static void test_booleans(void) {
  toml_t *doc = PARSE("a = true\nb = false\n");
  CHECK(toml_get_boolean(doc, "a", 0) == 1);
  CHECK(toml_get_boolean(doc, "b", 1) == 0);
  CHECK(toml_get_boolean(doc, "missing", 7) == 7);
  CHECK(toml_type(toml_get(doc, "a")) == TOML_BOOLEAN);
  toml_free(doc);

  PARSE_FAILS("a = True\n");
  PARSE_FAILS("a = FALSE\n");
  PARSE_FAILS("a = tru\n");
  PARSE_FAILS("a = truex\n");
}

static void test_strings(void) {
  toml_t *doc = PARSE(
      "basic = \"I'm a string. \\\"You can quote me\\\".\"\n"
      "escapes = \"\\b\\t\\n\\f\\r\\e\\\"\\\\\"\n"
      "hex = \"\\x41\\xE9\"\n"
      "unicode = \"\\u00E9 \\U0001F600\"\n"
      "empty = \"\"\n"
      "literal = 'C:\\Users\\nodejs'\n"
      "litquotes = 'Tom \"Dubs\" Preston-Werner'\n"
      "litempty = ''\n"
      "tab = \"a\\tb\"\n");
  CHECK(strcmp(toml_get_string(doc, "basic", ""), "I'm a string. \"You can quote me\".") == 0);
  CHECK(strcmp(toml_get_string(doc, "escapes", ""), "\b\t\n\f\r\x1b\"\\") == 0);
  CHECK(strcmp(toml_get_string(doc, "hex", ""), "A\xc3\xa9") == 0);
  CHECK(strcmp(toml_get_string(doc, "unicode", ""), "\xc3\xa9 \xf0\x9f\x98\x80") == 0);
  CHECK(strcmp(toml_get_string(doc, "empty", "x"), "") == 0);
  CHECK(strcmp(toml_get_string(doc, "literal", ""), "C:\\Users\\nodejs") == 0);
  CHECK(strcmp(toml_get_string(doc, "litquotes", ""), "Tom \"Dubs\" Preston-Werner") == 0);
  CHECK(strcmp(toml_get_string(doc, "litempty", "x"), "") == 0);
  CHECK(strcmp(toml_get_string(doc, "tab", ""), "a\tb") == 0);
  CHECK(toml_type(toml_get(doc, "basic")) == TOML_STRING);
  toml_free(doc);

  PARSE_FAILS("a = \"unterminated\n");
  PARSE_FAILS("a = 'unterminated\n");
  PARSE_FAILS("a = \"no \\q escape\"\n");
  PARSE_FAILS("a = \"\\xZZ\"\n");
  PARSE_FAILS("a = \"\\u12\"\n");
  PARSE_FAILS("a = \"\\uD800\"\n");
  PARSE_FAILS("a = \"\\U00110000\"\n");
  PARSE_FAILS("a = \"control \x01 here\"\n");
  PARSE_FAILS("a = 'control \x01 here'\n");
  PARSE_FAILS("a = \"del \x7f\"\n");

  {
    toml_t *nul = PARSE("nul = \"x\\u0000y\"\n");
    CHECK(toml_string_length(toml_get(nul, "nul")) == 3);
    CHECK(memcmp(toml_string(toml_get(nul, "nul")), "x\0y", 3) == 0);
    toml_free(nul);
  }
}

static void test_multiline_strings(void) {
  toml_t *doc = PARSE(
      "str1 = \"\"\"\nRoses are red\nViolets are blue\"\"\"\n"
      "str2 = \"\"\"\nThe quick brown \\\n\n\n  fox jumps over \\\n    the lazy dog.\"\"\"\n"
      "str3 = \"\"\"\\\n       The quick brown \\\n       fox jumps over \\\n       the lazy dog.\\\n       \"\"\"\n"
      "quotes2 = \"\"\"Here are two quotation marks: \"\". Simple enough.\"\"\"\n"
      "quotes3 = \"\"\"Here are three quotation marks: \"\"\\\".\"\"\"\n"
      "quotes7 = \"\"\"\"This,\" she said, \"is just a pointless statement.\"\"\"\"\n"
      "lit = '''I [dw]on't need \\d{2} apples'''\n"
      "litlines = '''\nThe first newline is\ntrimmed in literal strings.\n   All other whitespace\n   is preserved.\n'''\n"
      "lit15 = '''Here are fifteen quotation marks: \"\"\"\"\"\"\"\"\"\"\"\"\"\"\"'''\n"
      "litapos = ''''That,' she said, 'is still pointless.''''\n");
  CHECK(strcmp(toml_get_string(doc, "str1", ""), "Roses are red\nViolets are blue") == 0);
  CHECK(strcmp(toml_get_string(doc, "str2", ""),
               "The quick brown fox jumps over the lazy dog.") == 0);
  CHECK(strcmp(toml_get_string(doc, "str3", ""),
               "The quick brown fox jumps over the lazy dog.") == 0);
  CHECK(strcmp(toml_get_string(doc, "quotes2", ""),
               "Here are two quotation marks: \"\". Simple enough.") == 0);
  CHECK(strcmp(toml_get_string(doc, "quotes3", ""),
               "Here are three quotation marks: \"\"\".") == 0);
  CHECK(strcmp(toml_get_string(doc, "quotes7", ""),
               "\"This,\" she said, \"is just a pointless statement.\"") == 0);
  CHECK(strcmp(toml_get_string(doc, "lit", ""), "I [dw]on't need \\d{2} apples") == 0);
  CHECK(strcmp(toml_get_string(doc, "litlines", ""),
               "The first newline is\ntrimmed in literal strings.\n   All other "
               "whitespace\n   is preserved.\n") == 0);
  CHECK(strcmp(toml_get_string(doc, "lit15", ""),
               "Here are fifteen quotation marks: \"\"\"\"\"\"\"\"\"\"\"\"\"\"\"") == 0);
  CHECK(strcmp(toml_get_string(doc, "litapos", ""),
               "'That,' she said, 'is still pointless.'") == 0);
  toml_free(doc);

  {
    toml_t *crlf = PARSE("a = \"\"\"\r\nline1\r\nline2\r\n\"\"\"\r\nb = '''\r\nlit\r\n'''\r\n");
    CHECK(strcmp(toml_get_string(crlf, "a", ""), "line1\nline2\n") == 0);
    CHECK(strcmp(toml_get_string(crlf, "b", ""), "lit\n") == 0);
    toml_free(crlf);
  }
  {
    toml_t *escaped = PARSE("0=\"\"\"\\\r\n\"\"\"\r\n");
    CHECK(strcmp(toml_get_string(escaped, "0", "x"), "") == 0);
    toml_free(escaped);
  }

  PARSE_FAILS("a = \"\"\"unterminated\n");
  PARSE_FAILS("a = '''unterminated\n");
  PARSE_FAILS("a = \"\"\"too many \"\"\"\"\"\"\n");
  PARSE_FAILS("a = '''too many ''''''\n");
  PARSE_FAILS("a = \"\"\"bad escape \\q\"\"\"\n");
  PARSE_FAILS("a = \"\"\"control \x02\"\"\"\n");
  PARSE_FAILS("a = '''control \x02'''\n");
}

static void test_datetimes(void) {
  toml_t *doc = PARSE(
      "odt1 = 1979-05-27T07:32:00Z\n"
      "odt2 = 1979-05-27T00:32:00-07:00\n"
      "odt3 = 1979-05-27T00:32:00.999999-07:00\n"
      "odt4 = 1979-05-27 07:32:00Z\n"
      "odt5 = 1979-05-27 07:32Z\n"
      "odt6 = 1979-05-27T07:32:00+00:00\n"
      "ldt1 = 1979-05-27T07:32:00\n"
      "ldt2 = 1979-05-27T07:32\n"
      "ld1 = 1979-05-27\n"
      "lt1 = 07:32:00\n"
      "lt2 = 00:32:00.5\n"
      "lt3 = 07:32\n"
      "edge = 0001-01-01 00:00:00Z\n"
      "leap = 2000-02-29T00:00:00Z\n");
  const toml_datetime_t *dt;

  dt = toml_get_datetime(doc, "odt1");
  CHECK(dt && dt->kind == TOML_OFFSET_DATETIME);
  CHECK(dt->year == 1979 && dt->month == 5 && dt->day == 27);
  CHECK(dt->hour == 7 && dt->minute == 32 && dt->second == 0 && dt->microsecond == 0);
  CHECK(dt->offset_minutes == 0);

  dt = toml_get_datetime(doc, "odt2");
  CHECK(dt && dt->offset_minutes == -420 && dt->hour == 0 && dt->minute == 32);

  dt = toml_get_datetime(doc, "odt3");
  CHECK(dt && dt->microsecond == 999999 && dt->offset_minutes == -420);

  dt = toml_get_datetime(doc, "odt4");
  CHECK(dt && dt->hour == 7 && dt->minute == 32);

  dt = toml_get_datetime(doc, "odt5");
  CHECK(dt && dt->second == 0 && dt->microsecond == 0 && dt->offset_minutes == 0);

  dt = toml_get_datetime(doc, "odt6");
  CHECK(dt && dt->kind == TOML_OFFSET_DATETIME && dt->offset_minutes == 0);

  dt = toml_get_datetime(doc, "ldt1");
  CHECK(dt && dt->kind == TOML_LOCAL_DATETIME);
  dt = toml_get_datetime(doc, "ldt2");
  CHECK(dt && dt->kind == TOML_LOCAL_DATETIME && dt->second == 0);

  dt = toml_get_datetime(doc, "ld1");
  CHECK(dt && dt->kind == TOML_LOCAL_DATE && dt->day == 27);

  dt = toml_get_datetime(doc, "lt1");
  CHECK(dt && dt->kind == TOML_LOCAL_TIME && dt->hour == 7);
  dt = toml_get_datetime(doc, "lt2");
  CHECK(dt && dt->kind == TOML_LOCAL_TIME && dt->microsecond == 500000);
  dt = toml_get_datetime(doc, "lt3");
  CHECK(dt && dt->kind == TOML_LOCAL_TIME && dt->hour == 7 && dt->minute == 32);

  dt = toml_get_datetime(doc, "edge");
  CHECK(dt && dt->year == 1 && dt->month == 1 && dt->day == 1);
  CHECK(toml_get_datetime(doc, "leap") != NULL);
  CHECK(toml_get_datetime(doc, "missing") == NULL);
  CHECK(toml_type(toml_get(doc, "ld1")) == TOML_DATETIME);
  toml_free(doc);

  PARSE_FAILS("a = 1979-02-30\n");
  PARSE_FAILS("a = 2100-02-29T15:15:15Z\n");
  PARSE_FAILS("a = 1979-13-01\n");
  PARSE_FAILS("a = 1979-00-01\n");
  PARSE_FAILS("a = 1979-01-00\n");
  PARSE_FAILS("a = 1979-01-32\n");
  PARSE_FAILS("a = 1979-05-27T24:00:00\n");
  PARSE_FAILS("a = 1979-05-27T07:60:00\n");
  PARSE_FAILS("a = 1979-05-27T07:32:61\n");
  PARSE_FAILS("a = 1979-05-27T07:32:00+25:00\n");
  PARSE_FAILS("a = 1979-05-27T07:32:00+07:60\n");
  PARSE_FAILS("a = 1979-05-27T07:32:00.\n");
  PARSE_FAILS("a = 1987-07-0517:45:00Z\n");
  PARSE_FAILS("a = 07:32Z\n");
  PARSE_FAILS("a = 07:3\n");
  PARSE_FAILS("a = 02026-05-07\n");
  PARSE_FAILS("a = 10000-01-01\n");
  PARSE_FAILS("a = 1979-5-27\n");
  PARSE_FAILS("a = 1979-05-27T\n");
}

static void test_dotted_keys(void) {
  toml_t *doc = PARSE(
      "name = \"Orange\"\n"
      "physical.color = \"orange\"\n"
      "physical.shape = \"round\"\n"
      "site.\"google.com\" = true\n"
      "fruit.name = \"banana\"\n"
      "fruit. color = \"yellow\"\n"
      "fruit . flavor = \"banana\"\n"
      "3.14159 = \"pi\"\n");
  CHECK(strcmp(toml_get_string(doc, "physical.color", ""), "orange") == 0);
  CHECK(strcmp(toml_get_string(doc, "physical.shape", ""), "round") == 0);
  CHECK(toml_boolean(toml_child(toml_get(doc, "site"), "google.com"), 0) == 1);
  CHECK(strcmp(toml_get_string(doc, "fruit.color", ""), "yellow") == 0);
  CHECK(strcmp(toml_get_string(doc, "fruit.flavor", ""), "banana") == 0);
  CHECK(strcmp(toml_get_string(doc, "3.14159", ""), "pi") == 0); /* two-part dotted key */
  toml_free(doc);

  {
    toml_t *t = PARSE("a.b.c = 1\na.b.d = 2\na.e = 3\n");
    CHECK(toml_get_integer(t, "a.b.c", 0) == 1);
    CHECK(toml_get_integer(t, "a.b.d", 0) == 2);
    CHECK(toml_get_integer(t, "a.e", 0) == 3);
    toml_free(t);
  }
  {
    toml_t *t = PARSE("[fruit]\napple.color = \"red\"\napple.taste.sweet = true\n"
                      "[fruit.apple.texture]\nsmooth = true\n");
    CHECK(toml_get_boolean(t, "fruit.apple.taste.sweet", 0) == 1);
    CHECK(toml_get_boolean(t, "fruit.apple.texture.smooth", 0) == 1);
    toml_free(t);
  }
  {
    toml_t *t = PARSE("apple.type = \"fruit\"\norange.type = \"fruit\"\n"
                      "apple.skin = \"thin\"\norange.skin = \"thick\"\n");
    CHECK(strcmp(toml_get_string(t, "apple.type", ""), "fruit") == 0);
    CHECK(strcmp(toml_get_string(t, "orange.skin", ""), "thick") == 0);
    toml_free(t);
  }
  {
    toml_t *t = PARSE("\"\" = \"blank\"\n\"127.0.0.1\" = 1\n");
    CHECK(strcmp(toml_get_string(t, "", "x"), "x") == 0);       /* root is not a string */
    CHECK(toml_get(t, "") == t);                                /* empty path returns the value */
    CHECK(strcmp(toml_string(toml_child(t, "")), "blank") == 0); /* empty key is a real key */
    CHECK(toml_integer(toml_child(t, "127.0.0.1"), 0) == 1);    /* dots in quoted keys */
    toml_free(t);
  }

  PARSE_FAILS("name = \"Tom\"\nname = \"Pradyun\"\n");
  PARSE_FAILS("spelling = \"favorite\"\n\"spelling\" = \"favourite\"\n");
  PARSE_FAILS("fruit.apple = 1\nfruit.apple.smooth = true\n");
  PARSE_FAILS("a.b.c = 1\na.b = 2\n");
  PARSE_FAILS("a = 1\na.b = 2\n");
  PARSE_FAILS("a = {k1 = 1, k1.name = \"joe\"}\n");
  PARSE_FAILS("a.b = 1\na.b.c = 2\n");
  PARSE_FAILS("= \"no key name\"\n");
  PARSE_FAILS("\"\"\"key\"\"\" = 1\n");
  PARSE_FAILS("'''key''' = 1\n");
  PARSE_FAILS("a. = 1\n");
  PARSE_FAILS(".a = 1\n");
  PARSE_FAILS("a..b = 1\n");
  PARSE_FAILS("a = 1\n[a.b]\n");
}

static void test_tables(void) {
  toml_t *doc = PARSE(
      "[table-1]\nkey1 = \"some string\"\nkey2 = 123\n"
      "[table-2]\nkey1 = \"another string\"\nkey2 = 456\n"
      "[dog.\"tater.man\"]\ntype.name = \"pug\"\n"
      "[x.y.z.w]\n"
      "[x]\n"
      "[a.b.c]\n"
      "[ d.e.f ]\n"
      "[ g .  h  . i ]\n"
      "[ j . \"k\" . 'l' ]\n");
  CHECK(toml_get_integer(doc, "table-1.key2", 0) == 123);
  CHECK(toml_get_integer(doc, "table-2.key2", 0) == 456);
  CHECK(strcmp(toml_get_string(doc, "dog.tater.man.type.name", ""), "") == 0); /* dots split paths */
  CHECK(strcmp(toml_string(toml_child(toml_child(toml_child(toml_get(doc, "dog"), "tater.man"), "type"), "name")), "pug") == 0);
  CHECK(toml_get(doc, "x.y.z.w") != NULL);
  CHECK(toml_get(doc, "g.h.i") != NULL);
  CHECK(toml_get(doc, "j.k.l") != NULL);
  CHECK(toml_get(doc, "a.b.c") != NULL);
  CHECK(strcmp(toml_type_name(toml_type(toml_get(doc, "x"))), "table") == 0);
  CHECK(toml_get(doc, "x.y") != NULL);
  CHECK(toml_count(toml_get(doc, "x.y.z.w")) == 0); /* w is empty */
  CHECK(toml_count(doc) == 8);
  {
    size_t i;
    const char *keys[] = {"table-1", "table-2", "dog", "x", "a", "d", "g", "j"};
    for (i = 0; i < toml_count(doc); i++) {
      CHECK(strcmp(toml_key(doc, i), keys[i]) == 0);
    }
    CHECK(toml_at(doc, toml_count(doc)) == NULL);
    CHECK(toml_key(doc, 99) == NULL);
  }
  toml_free(doc);

  PARSE_FAILS("[fruit]\napple = \"red\"\n[fruit]\norange = \"orange\"\n");
  PARSE_FAILS("[fruit]\napple = \"red\"\n[fruit.apple]\ntexture = \"smooth\"\n");
  PARSE_FAILS("[naughty..naughty]\n");
  PARSE_FAILS("[]\n");
  PARSE_FAILS("[[a]]\n[a]\n");
  PARSE_FAILS("[a]\n[a]\n");
  PARSE_FAILS("[a.b]\n[a]\n[a.b]\n");
  PARSE_FAILS("[a]\n[a.b]\n[a.b]\n");
  PARSE_FAILS("[a]]\n");
  PARSE_FAILS("[a\n");
  PARSE_FAILS("[a] b = 1\n");
  PARSE_FAILS("[a.b.c]\nz = 9\n[a]\nb.c.t = 1\n");
  PARSE_FAILS("[a.b.c.d]\nz = 9\n[a]\nb.c.d.k.t = 1\n");
  PARSE_FAILS("[a.b.c]\nz = 9\n[[unrelated]]\nx = 1\n[a]\nb.c.t = 1\n");
  PARSE_FAILS("[fruit]\napple.color = \"red\"\n[fruit.apple]\ntexture = \"smooth\"\n");
}

static void test_arrays(void) {
  toml_t *doc = PARSE(
      "integers = [ 1, 2, 3 ]\n"
      "colors = [ \"red\", \"yellow\", \"green\" ]\n"
      "nested = [ [ 1, 2 ], [3, 4, 5] ]\n"
      "mixed = [ 0.1, 0.2, 0.5, 1, 2, 5 ]\n"
      "spanning = [\n  1, 2, 3\n]\n"
      "trailing = [\n  1,\n  2, # comment\n]\n"
      "empty = []\n"
      "contributors = [\n  \"Foo Bar <foo@example.com>\",\n  { name = \"Baz Qux\" }\n]\n");
  const toml_t *integers = toml_get(doc, "integers");
  const toml_t *nested = toml_get(doc, "nested");
  CHECK(toml_count(integers) == 3);
  CHECK(toml_integer(toml_at(integers, 0), 0) == 1);
  CHECK(toml_integer(toml_at(integers, 2), 0) == 3);
  CHECK(toml_integer(toml_at(integers, 3), -1) == -1);
  CHECK(toml_count(nested) == 2);
  CHECK(toml_integer(toml_at(toml_at(nested, 1), 2), 0) == 5);
  CHECK(toml_count(toml_get(doc, "mixed")) == 6);
  CHECK(toml_count(toml_get(doc, "empty")) == 0);
  CHECK(toml_count(toml_get(doc, "spanning")) == 3);
  CHECK(toml_count(toml_get(doc, "trailing")) == 2);
  CHECK(toml_count(toml_get(doc, "contributors")) == 2);
  CHECK(strcmp(toml_string(toml_child(toml_at(toml_get(doc, "contributors"), 1), "name")), "Baz Qux") == 0);
  CHECK(toml_type(integers) == TOML_ARRAY);
  CHECK(toml_string(toml_at(integers, 0)) == NULL);
  CHECK(toml_key(integers, 0) == NULL);
  toml_free(doc);

  {
    toml_t *t = PARSE("a = [1, 2, 3,]\n");
    CHECK(toml_count(toml_get(t, "a")) == 3);
    toml_free(t);
  }
  {
    toml_t *t = PARSE("a = [\n  # only a comment\n]\n");
    CHECK(toml_count(toml_get(t, "a")) == 0);
    toml_free(t);
  }
  {
    toml_t *t = PARSE("a = [1979-05-27, 1979-05-28]\n");
    CHECK(toml_count(toml_get(t, "a")) == 2);
    toml_free(t);
  }

  PARSE_FAILS("a = [1, 2\n");
  PARSE_FAILS("a = [1 2]\n");
  PARSE_FAILS("a = [,]\n");
  PARSE_FAILS("a = [1,,2]\n");
  PARSE_FAILS("a = [1, 2]]\n");
}

static void test_inline_tables(void) {
  toml_t *doc = PARSE(
      "name = { first = \"Tom\", last = \"Preston-Werner\" }\n"
      "point = {x=1, y=2}\n"
      "animal = { type.name = \"pug\" }\n"
      "contact = {\n"
      "    personal = {\n"
      "        name = \"Donald Duck\",\n"
      "        email = \"donald@duckburg.com\",\n"
      "    },\n"
      "    work = { name = \"Coin cleaner\", },\n"
      "}\n"
      "empty = {}\n"
      "points = [ { x = 1, y = 2, z = 3 },\n"
      "           { x = 7, y = 8, z = 9 } ]\n");
  CHECK(strcmp(toml_get_string(doc, "name.first", ""), "Tom") == 0);
  CHECK(toml_get_integer(doc, "point.x", 0) == 1);
  CHECK(toml_get_integer(doc, "point.y", 0) == 2);
  CHECK(strcmp(toml_get_string(doc, "animal.type.name", ""), "pug") == 0);
  CHECK(strcmp(toml_get_string(doc, "contact.personal.email", ""), "donald@duckburg.com") == 0);
  CHECK(strcmp(toml_get_string(doc, "contact.work.name", ""), "Coin cleaner") == 0);
  CHECK(toml_count(toml_get(doc, "empty")) == 0);
  CHECK(toml_count(toml_get(doc, "points")) == 2);
  CHECK(toml_get_integer(doc, "points.0.x", 0) == 0); /* arrays are not traversed */
  CHECK(toml_integer(toml_child(toml_at(toml_get(doc, "points"), 1), "y"), 0) == 8);
  toml_free(doc);

  {
    toml_t *t = PARSE("a = { b.c = 1, b.d = 2 }\n");
    CHECK(toml_get_integer(t, "a.b.c", 0) == 1);
    CHECK(toml_get_integer(t, "a.b.d", 0) == 2);
    toml_free(t);
  }

  PARSE_FAILS("a = {b = 1\n");
  PARSE_FAILS("a = {b = 1,}\n}a\n");
  PARSE_FAILS("a = {b = 1, b = 2}\n");
  PARSE_FAILS("a = {b = 1, b.c = 2}\n");
  PARSE_FAILS("a = {}\na.b = 1\n");
  PARSE_FAILS("a = {b = 1}\na.c = 2\n");
  PARSE_FAILS("a = {b = 1}\n[a]\n");
  PARSE_FAILS("a = {b = 1}\n[a.b]\n");
  PARSE_FAILS("a = {b = {c = 1}}\na.b.d = 2\n");
  PARSE_FAILS("a = 1\n[a]\n");
  PARSE_FAILS("a = [1]\n[a]\n");
}

static void test_arrays_of_tables(void) {
  toml_t *doc = PARSE(
      "[[product]]\nname = \"Hammer\"\nsku = 738594937\n"
      "[[product]]\n"
      "[[product]]\nname = \"Nail\"\nsku = 284758393\ncolor = \"gray\"\n");
  const toml_t *product = toml_get(doc, "product");
  CHECK(toml_count(product) == 3);
  CHECK(strcmp(toml_get_string(doc, "product.0.name", ""), "") == 0); /* arrays not traversed */
  CHECK(strcmp(toml_string(toml_child(toml_at(product, 0), "name")), "Hammer") == 0);
  CHECK(toml_count(toml_at(product, 1)) == 0);
  CHECK(strcmp(toml_string(toml_child(toml_at(product, 2), "color")), "gray") == 0);
  toml_free(doc);

  {
    toml_t *t = PARSE(
        "[[fruits]]\nname = \"apple\"\n"
        "[fruits.physical]\ncolor = \"red\"\nshape = \"round\"\n"
        "[[fruits.varieties]]\nname = \"red delicious\"\n"
        "[[fruits.varieties]]\nname = \"granny smith\"\n"
        "[[fruits]]\nname = \"banana\"\n"
        "[[fruits.varieties]]\nname = \"plantain\"\n");
    const toml_t *fruits = toml_get(t, "fruits");
    const toml_t *varieties;
    CHECK(toml_count(fruits) == 2);
    CHECK(strcmp(toml_get_string(toml_at(fruits, 0), "physical.color", ""), "red") == 0);
    CHECK(strcmp(toml_string(toml_child(toml_child(toml_at(fruits, 0), "physical"), "color")), "red") == 0);
    varieties = toml_child(toml_at(fruits, 0), "varieties");
    CHECK(toml_count(varieties) == 2);
    CHECK(strcmp(toml_string(toml_child(toml_at(varieties, 1), "name")), "granny smith") == 0);
    varieties = toml_child(toml_at(fruits, 1), "varieties");
    CHECK(toml_count(varieties) == 1);
    CHECK(strcmp(toml_string(toml_child(toml_at(varieties, 0), "name")), "plantain") == 0);
    toml_free(t);
  }
  {
    toml_t *t = PARSE("[[a.b]]\nx = 1\n");
    CHECK(toml_count(toml_get(t, "a.b")) == 1);
    toml_free(t);
  }
  {
    toml_t *t = PARSE("[[a]]\n[a.b]\nc = 1\n[[a]]\n[a.b]\nc = 2\n");
    CHECK(toml_get_integer(toml_at(toml_get(t, "a"), 0), "b.c", 0) == 1);
    CHECK(toml_get_integer(toml_at(toml_get(t, "a"), 1), "b.c", 0) == 2);
    toml_free(t);
  }
  {
    toml_t *t = PARSE("[[a]]\nb.c = 1\n[[a]]\nb.c = 2\n");
    CHECK(toml_get_integer(toml_at(toml_get(t, "a"), 1), "b.c", 0) == 2);
    toml_free(t);
  }

  PARSE_FAILS("fruits = []\n[[fruits]]\n");
  PARSE_FAILS("[fruit.physical]\ncolor = \"red\"\n[[fruit]]\nname = \"apple\"\n");
  PARSE_FAILS("[[fruits]]\n[[fruits.varieties]]\n[fruits.varieties]\n");
  PARSE_FAILS("[[fruits]]\n[fruits.physical]\n[[fruits.physical]]\n");
  PARSE_FAILS("[[a.b]]\n[a]\nb.y = 2\n");
  PARSE_FAILS("[[albums.songs]]\nname = \"Glory Days\"\n[[albums]]\nname = \"Born in the USA\"\n");
  PARSE_FAILS("a = 1\n[[a]]\n");
}

static void test_document_shape(void) {
  {
    toml_t *t = PARSE("");
    CHECK(toml_count(t) == 0);
    CHECK(toml_type(t) == TOML_TABLE);
    CHECK(toml_get(t, "") == t);
    toml_free(t);
  }
  {
    toml_t *t = PARSE("# comment only\n# another\n");
    CHECK(toml_count(t) == 0);
    toml_free(t);
  }
  {
    toml_t *t = PARSE("  \t \n\n# c\nkey = 1 # trailing\n\n");
    CHECK(toml_get_integer(t, "key", 0) == 1);
    toml_free(t);
  }
  {
    toml_t *t = PARSE("a = 1\r\nb = 2\r\n");
    CHECK(toml_get_integer(t, "b", 0) == 2);
    toml_free(t);
  }

  PARSE_FAILS("a = 1 b = 2\n");
  PARSE_FAILS("\r");
  PARSE_FAILS("a = 1\rb = 2\n");
  PARSE_FAILS("a = 1 # control \x01\n");
  PARSE_FAILS("# control \x01\n");
  PARSE_FAILS("key =\n");
  PARSE_FAILS("key\n");
  PARSE_FAILS("[a]\nkey\n");
}

static void test_utf8(void) {
  {
    toml_t *t = PARSE("key = \"\xc3\xa9\xe6\x97\xa5\xf0\x9f\x98\x80\"\n");
    CHECK(strcmp(toml_get_string(t, "key", ""), "\xc3\xa9\xe6\x97\xa5\xf0\x9f\x98\x80") == 0);
    toml_free(t);
  }
  {
    toml_t *t = PARSE("\xef\xbb\xbfkey = 1\n");
    CHECK(toml_get_integer(t, "key", 0) == 1);
    toml_free(t);
  }
  {
    toml_error_t error;
    toml_t *t = toml_parse("a = \"\xff\"\n", 8, &error);
    CHECK(t == NULL);
    CHECK(error.line == 1);
    toml_free(t);
  }
  {
    toml_error_t error;
    toml_t *t = toml_parse("a = \"\xc0\xaf\"\n", 9, &error);
    CHECK(t == NULL);
    toml_free(t);
  }
  {
    toml_error_t error;
    toml_t *t = toml_parse("a = \"\xed\xa0\x80\"\n", 9, &error);
    CHECK(t == NULL);
    toml_free(t);
  }
  {
    toml_error_t error;
    toml_t *t = toml_parse("\xff", 1, &error);
    CHECK(t == NULL);
    CHECK(error.line == 1 && error.column == 1);
    toml_free(t);
  }
  {
    toml_error_t error;
    toml_t *t = toml_parse("bad \xc3(\n", 6, &error);
    CHECK(t == NULL);
    toml_free(t);
  }
}

static void test_error_reporting(void) {
  toml_error_t error;
  memset(&error, 0, sizeof(error));
  CHECK(toml_parse("a = 1\nb = @\n", 13, &error) == NULL);
  CHECK(error.line == 2);
  CHECK(error.column > 0);
  CHECK(error.message[0] != '\0');
  CHECK(strlen(error.message) < TOML_ERROR_MESSAGE_SIZE);

  memset(&error, 0, sizeof(error));
  CHECK(toml_parse(NULL, 0, &error) == NULL);
  CHECK(error.message[0] != '\0');
}

static void test_api(void) {
  toml_t *doc = PARSE(
      "title = \"TOML\"\n"
      "answer = 42\n"
      "pi = 3.25\n"
      "on = true\n"
      "[server]\nhost = \"localhost\"\nports = [80, 443]\n"
      "[[server.node]]\nname = \"a\"\n"
      "[[server.node]]\nname = \"b\"\n");
  const toml_t *node;

  CHECK(strcmp(toml_get_string(doc, "title", "x"), "TOML") == 0);
  CHECK(strcmp(toml_get_string(doc, "missing", "x"), "x") == 0);
  CHECK(strcmp(toml_get_string(doc, "answer", "x"), "x") == 0);
  CHECK(toml_get_integer(doc, "answer", -1) == 42);
  CHECK(toml_get_integer(doc, "pi", -1) == -1);
  CHECK(toml_get_float(doc, "pi", 0) == 3.25);
  CHECK(toml_get_boolean(doc, "on", 0) == 1);
  CHECK(toml_get_boolean(doc, "off", 1) == 1);
  CHECK(toml_get_string(doc, "server.host", "x") != NULL);
  CHECK(strcmp(toml_get_string(doc, "server.host", ""), "localhost") == 0);
  CHECK(toml_get_integer(doc, "server.ports", 0) == 0);

  node = toml_get(doc, "server.ports");
  CHECK(toml_count(node) == 2);
  CHECK(toml_integer(toml_at(node, 0), 0) == 80);
  CHECK(toml_integer(toml_at(node, 1), 0) == 443);

  node = toml_child(toml_get(doc, "server"), "node");
  CHECK(toml_count(node) == 2);
  CHECK(strcmp(toml_string(toml_child(toml_at(node, 1), "name")), "b") == 0);

  CHECK(toml_get(doc, "server.") == NULL);
  CHECK(toml_get(doc, ".server") == NULL);
  CHECK(toml_get(doc, "server..host") == NULL);
  CHECK(toml_get(doc, "nope") == NULL);
  CHECK(toml_get(NULL, "x") == NULL);
  CHECK(toml_child(NULL, "x") == NULL);
  CHECK(toml_string(NULL) == NULL);
  CHECK(toml_datetime(NULL) == NULL);
  CHECK(toml_type(NULL) == TOML_INVALID);
  CHECK(strcmp(toml_type_name(TOML_INVALID), "invalid") == 0);
  CHECK(strcmp(toml_type_name(TOML_TABLE), "table") == 0);
  CHECK(strcmp(toml_type_name(TOML_ARRAY), "array") == 0);
  CHECK(strcmp(toml_type_name(TOML_STRING), "string") == 0);
  CHECK(strcmp(toml_type_name(TOML_INTEGER), "integer") == 0);
  CHECK(strcmp(toml_type_name(TOML_FLOAT), "float") == 0);
  CHECK(strcmp(toml_type_name(TOML_BOOLEAN), "boolean") == 0);
  CHECK(strcmp(toml_type_name(TOML_DATETIME), "datetime") == 0);
  CHECK(toml_count(NULL) == 0);
  CHECK(toml_at(NULL, 0) == NULL);
  CHECK(toml_key(NULL, 0) == NULL);
  CHECK(toml_integer(NULL, 5) == 5);
  CHECK(toml_float(NULL, 5.5) == 5.5);
  CHECK(toml_boolean(NULL, 5) == 5);

  toml_free(doc);
  toml_free(NULL);

  {
    toml_t *nul = PARSE("\"\\u0000\" = 1\n\"\" = 2\n");
    CHECK(toml_count(nul) == 2); /* the two keys are distinct */
    CHECK(toml_key_length(nul, 0) == 1);
    CHECK(toml_key_length(nul, 1) == 0);
    CHECK(toml_integer(toml_at(nul, 0), 0) == 1);
    CHECK(toml_integer(toml_at(nul, 1), 0) == 2);
    toml_free(nul);
  }

  {
    toml_error_t error;
    memset(&error, 0, sizeof(error));
    CHECK(toml_load("this/path/does/not/exist.toml", &error) == NULL);
    CHECK(error.message[0] != '\0');
  }
  {
    toml_t *sample = toml_load(TOML_TEST_EXAMPLE, NULL);
    CHECK(sample != NULL);
    if (sample) {
      CHECK(strcmp(toml_get_string(sample, "server.host", ""), "127.0.0.1") == 0);
      toml_free(sample);
    }
  }
}

int main(void) {
  test_integers();
  test_floats();
  test_booleans();
  test_strings();
  test_multiline_strings();
  test_datetimes();
  test_dotted_keys();
  test_tables();
  test_arrays();
  test_inline_tables();
  test_arrays_of_tables();
  test_document_shape();
  test_utf8();
  test_error_reporting();
  test_api();
  printf("%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
