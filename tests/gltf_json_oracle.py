# SPDX-License-Identifier: Apache-2.0
"""Independent exact lexical/Decimal oracle for the private native capture seam.

Run with the native test executable and an explicit scratch directory. This does
not certify glTF metadata/schema/roles; fixtures test JSON retention only.
"""
import argparse
from decimal import Decimal
import json
from pathlib import Path
import subprocess


class Number(str):
    pass


class Object(list):
    pass


def pairs(items):
    if len({key for key, _ in items}) != len(items):
        raise ValueError("decoded duplicate")
    return Object(items)


def scalar(text):
    if any(0xD800 <= ord(char) <= 0xDFFF for char in text):
        raise ValueError("non-scalar Unicode")
    return text.encode("utf-8").hex()


def expected(source):
    def nonfinite(_):
        raise ValueError("non-JSON number")

    value = json.loads(source.decode("utf-8"), parse_int=Number,
                       parse_float=Number, parse_constant=nonfinite,
                       object_pairs_hook=pairs)
    rows = []
    last = {}

    def visit(item, parent="-", key=""):
        index = len(rows)
        kind, payload = 5, ""
        if isinstance(item, Object):
            kind = 0
        elif isinstance(item, list):
            kind = 1
        elif isinstance(item, Number):
            kind, payload = 3, str(item).encode().hex()
            assert Decimal(item).is_finite()  # Exact, never binary float.
        elif isinstance(item, str):
            kind, payload = 2, scalar(item)
        elif isinstance(item, bool):
            kind, payload = 4, "1" if item else "0"
        rows.append([str(kind), str(parent), "-", "-", scalar(key), payload])
        if parent != "-":
            if parent not in last:
                rows[parent][2] = str(index)
            else:
                rows[last[parent]][3] = str(index)
            last[parent] = index
        if kind == 0:
            for child_key, child in item:
                visit(child, index, child_key)
        elif kind == 1:
            for child in item:
                visit(child, index)

    visit(value)
    return ["code\t0", "source\t" + source.hex()] + ["\t".join(row) for row in rows]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("executable", type=Path)
    parser.add_argument("scratch", type=Path)
    args = parser.parse_args()
    args.scratch.mkdir(parents=True, exist_ok=True)
    valid = [b'{"asset":{"version":"2.0"}}', b'{}', b'[]', b'null', b'true',
             b'"-1e7"', b'-0', b'1.0', b'1E+0', b'1e-9999', b'1e9999',
             b'18446744073709551616', b'-18446744073709551617',
             b'{"":[],"nested":{"":{},"x":[true,null,false]}}',
             b'{"u":"\\ud83d\\ude00\\u0000","a\\u0062":12}',
             '{"名":"资源/😀","extensions":{"UNKNOWN":{"count":1.9}}}'.encode(),
             b' \r\n { "a" : [1, 1.00, 10e-1, -0.000e+9] } \t\n',
             b'{"s":"\\\\uD800 \\\" 1e0","t":"\\b\\f\\n\\r\\t"}']
    # Every JSON escape spelling is decoded independently by Python and native.
    valid.extend(json.dumps({chr(i): chr(i)}, ensure_ascii=True).encode()
                 for i in range(128))
    valid.extend(json.dumps({chr(i): chr(i)}, ensure_ascii=False).encode()
                 for i in [0x80, 0x7FF, 0x800, 0xD7FF, 0xE000, 0xFFFF,
                           0x10000, 0x10FFFF])
    invalid = [b'', b'{', b'[1,]', b'/*x*/{}', b'true false', b'{"a":01}',
               b'[NaN]', b'[1e]', b'[--1]', b'{"a":1,"\\u0061":2}',
               b'{"":{},"":null}', b'{"x":"\\uD800"}',
               b'{"x":"\\uDC00"}', b'{"x":"\\uD800x\\uDC00"}',
               b'{"x":"\xed\xa0\x80"}', b'{"x":"\xc0\x80"}',
               b'{"x":"\xf4\x90\x80\x80"}', b'{"x":"\xf0\x90"}']
    for index, source in enumerate(valid + invalid):
        fixture = args.scratch / f"case-{index:03}.json"
        fixture.write_bytes(source)
        actual = subprocess.check_output([str(args.executable), "--dump", str(fixture)],
                                         text=True).splitlines()
        if index < len(valid):
            assert actual == expected(source), (index, actual, expected(source))
        else:
            try:
                expected(source)
            except (ValueError, UnicodeError):
                pass
            else:
                raise AssertionError((index, "oracle accepted invalid fixture"))
            assert actual in [["code\t2"], ["code\t3"]], (index, actual)
        assert fixture.read_bytes() == source
    print(f"Independent exact oracle passed: {len(valid)} valid, {len(invalid)} invalid; "
          "source/numeric lexemes/scalars/order/parents/siblings exact")


if __name__ == "__main__":
    main()
