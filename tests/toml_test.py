#!/usr/bin/env python3
"""Run the toml-test conformance suite (https://github.com/toml-lang/toml-test).

Usage: python3 tests/toml_test.py SUITE_DIR [DECODER]

SUITE_DIR is a checkout of toml-test.  The decoder is built by `make
build/toml-test-decoder` by default.  Only the TOML 1.1 file list is used.
"""

import datetime
import json
import math
import os
import subprocess
import sys

MAX_REPORTED = 25


def parse_datetime(kind, text):
    text = text.strip().replace(" ", "T").replace("t", "T")
    if text.endswith("z") or text.endswith("Z"):
        text = text[:-1] + "+00:00"
    if kind == "datetime":
        return datetime.datetime.fromisoformat(text)
    if kind == "datetime-local":
        return datetime.datetime.fromisoformat(text)
    if kind == "date-local":
        return datetime.date.fromisoformat(text)
    return datetime.time.fromisoformat(text)


def values_equal(kind, expected, actual):
    if kind == "float":
        e = expected.strip().lower()
        a = actual.strip().lower()
        if e.endswith("nan") or a.endswith("nan"):
            return e.endswith("nan") and a.endswith("nan")
        if e.endswith("inf") or a.endswith("inf"):
            return float(e.replace("inf", "Infinity")) == float(a.replace("inf", "Infinity"))
        return float(e) == float(a)
    if kind in ("datetime", "datetime-local", "date-local", "time-local"):
        return parse_datetime(kind, expected) == parse_datetime(kind, actual)
    if kind == "bool":
        return expected.lower() == actual.lower()
    return expected == actual


def compare(expected, actual, path, errors):
    if isinstance(expected, list):
        if not isinstance(actual, list):
            errors.append(f"{path}: expected an array, got {type(actual).__name__}")
            return
        if len(expected) != len(actual):
            errors.append(f"{path}: expected {len(expected)} elements, got {len(actual)}")
            return
        for i, (e, a) in enumerate(zip(expected, actual)):
            compare(e, a, f"{path}[{i}]", errors)
        return
    if isinstance(expected, dict) and set(expected) == {"type", "value"}:
        if not isinstance(actual, dict) or set(actual) != {"type", "value"}:
            errors.append(f"{path}: expected a tagged value")
            return
        if expected["type"] != actual["type"]:
            errors.append(f"{path}: expected type {expected['type']!r}, got {actual['type']!r}")
            return
        if not values_equal(expected["type"], str(expected["value"]), str(actual["value"])):
            errors.append(
                f"{path}: expected {expected['value']!r}, got {actual['value']!r} "
                f"({expected['type']})"
            )
        return
    if isinstance(expected, dict):
        if not isinstance(actual, dict):
            errors.append(f"{path}: expected a table, got {type(actual).__name__}")
            return
        missing = set(expected) - set(actual)
        extra = set(actual) - set(expected)
        for key in sorted(missing):
            errors.append(f"{path}: missing key {key!r}")
        for key in sorted(extra):
            errors.append(f"{path}: unexpected key {key!r}")
        for key in sorted(set(expected) & set(actual)):
            compare(expected[key], actual[key], f"{path}.{key}", errors)
        return
    if expected != actual:
        errors.append(f"{path}: expected {expected!r}, got {actual!r}")


def main():
    if len(sys.argv) < 2:
        print(__doc__.strip())
        return 2
    suite = sys.argv[1]
    decoder = sys.argv[2] if len(sys.argv) > 2 else os.path.join("build", "toml-test-decoder")
    file_list = os.path.join(suite, "tests", "files-toml-1.1.0")
    with open(file_list, "r", encoding="utf-8") as handle:
        entries = [line.strip() for line in handle if line.strip()]

    passed = 0
    failed = 0
    failures = []
    for entry in entries:
        if entry.endswith(".json"):
            continue  # encoder side of a valid test
        path = os.path.join(suite, "tests", entry)
        with open(path, "rb") as handle:
            source = handle.read()
        result = subprocess.run([decoder], input=source, stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE)
        if entry.startswith("invalid/"):
            if result.returncode == 0:
                failed += 1
                failures.append(f"{entry}: accepted an invalid document")
            else:
                passed += 1
            continue
        if result.returncode != 0:
            failed += 1
            message = result.stderr.decode("utf-8", "replace").strip()
            failures.append(f"{entry}: rejected a valid document ({message})")
            continue
        expected_path = path[: -len(".toml")] + ".json"
        try:
            with open(expected_path, "r", encoding="utf-8") as handle:
                expected = json.load(handle)
            actual = json.loads(result.stdout.decode("utf-8"))
        except (OSError, ValueError) as error:
            failed += 1
            failures.append(f"{entry}: cannot compare JSON ({error})")
            continue
        errors = []
        compare(expected, actual, "", errors)
        if errors:
            failed += 1
            failures.append(f"{entry}: {'; '.join(errors[:4])}")
        else:
            passed += 1

    print(f"toml-test (TOML 1.1): {passed} passed, {failed} failed")
    for line in failures[:MAX_REPORTED]:
        print(f"  FAIL {line}")
    if len(failures) > MAX_REPORTED:
        print(f"  ... and {len(failures) - MAX_REPORTED} more")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
