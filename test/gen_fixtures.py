#!/usr/bin/env python3
"""Generate black-box parse fixtures: in.cf + expected/ tree.

Expected trees are derived from the same spec used to build the container,
not from running v8unpack. Re-run: python3 test/gen_fixtures.py
"""

from __future__ import print_function

import ast
import os
import re
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
FIXTURES = os.path.join(HERE, "fixtures")
PLACEHOLDER_CPP = os.path.join(REPO, "src", "placeholder216.cpp")

F15, F16, F16Z = "f15", "f16", "f16z"

UNDEF15 = 0x7FFFFFFF
UNDEF16 = 0xFFFFFFFFFFFFFFFF
BASE16 = 0x1359
DEFAULT_PAGE = 512


def load_placeholder():
    text = open(PLACEHOLDER_CPP, "r", encoding="utf-8").read()
    body = text[text.index("{") + 1 : text.rindex("}")]
    data = bytearray()
    for raw in re.findall(r"'((?:\\x[0-9a-fA-F]{2}|\\.|[^'\\]))'", body):
        ch = ast.literal_eval("'" + raw + "'")
        data.append(ord(ch) & 0xFF)
    if len(data) != BASE16:
        raise SystemExit("placeholder216 size %d, expected %d" % (len(data), BASE16))
    return bytes(data)


PLACEHOLDER = load_placeholder()


def raw_deflate(data):
    c = zlib.compressobj(9, zlib.DEFLATED, -15)
    return c.compress(data) + c.flush()


class Fmt(object):
    def __init__(self, kind, hex_style="zero"):
        self.kind = kind
        self.hex_style = hex_style
        self.is64 = kind in (F16, F16Z)
        self.undef = UNDEF16 if self.is64 else UNDEF15
        self.base = BASE16 if kind == F16 else 0
        self.file_header_size = 20 if self.is64 else 16
        self.block_header_size = 55 if self.is64 else 31
        self.elem_addr_size = 24 if self.is64 else 12
        self.hex_width = 16 if self.is64 else 8

    def encode_hex(self, value):
        width = self.hex_width
        digits = ("%x" % value) if self.hex_style != "upper" else ("%X" % value)
        return digits.zfill(width)

    def file_header(self):
        if self.is64:
            return struct.pack("<QIII", self.undef, DEFAULT_PAGE, 0, 0)
        return struct.pack("<IIII", self.undef, DEFAULT_PAGE, 0, 0)

    def block_header(self, data_size, page_size, next_addr):
        return (
            b"\r\n"
            + self.encode_hex(data_size).encode("ascii")
            + b" "
            + self.encode_hex(page_size).encode("ascii")
            + b" "
            + self.encode_hex(next_addr).encode("ascii")
            + b" "
            + b"\r\n"
        )

    def elem_addr(self, header_addr, data_addr, marker=None):
        if marker is None:
            marker = self.undef
        if self.is64:
            return struct.pack("<QQQ", header_addr, data_addr, marker)
        return struct.pack("<III", header_addr, data_addr, marker)


def elem_header_bytes(name):
    utf16 = name.encode("utf-16le")
    return struct.pack("<QQI", 0, 0, 0) + utf16 + b"\x00\x00\x00\x00"


class BlockLayout(object):
    """How a payload is stored: one page (exact/padded) or a page chain."""

    def __init__(self, page_sizes=None, exact=False, pad_to=None):
        self.page_sizes = page_sizes
        self.exact = exact
        self.pad_to = pad_to

    def pages_for(self, data_len):
        if self.page_sizes:
            return list(self.page_sizes)
        if self.exact:
            return [data_len if data_len else 0]
        page = self.pad_to if self.pad_to is not None else max(data_len, DEFAULT_PAGE)
        if page < data_len:
            page = data_len
        return [page]


class Element(object):
    def __init__(
        self,
        name,
        data=None,
        nested=None,
        deflate=False,
        empty=False,
        header_layout=None,
        data_layout=None,
    ):
        self.name = name
        self.data = data
        self.nested = nested
        self.deflate = deflate
        self.empty = empty
        self.header_layout = header_layout or BlockLayout(exact=True)
        self.data_layout = data_layout or BlockLayout()


class Container(object):
    def __init__(
        self,
        fmt,
        elements,
        hex_style="zero",
        toc_layout=None,
        toc_extra_unused=0,
        toc_ghost=None,
    ):
        self.fmt = Fmt(fmt, hex_style)
        self.elements = list(elements)
        self.toc_layout = toc_layout or BlockLayout()
        self.toc_extra_unused = toc_extra_unused
        self.toc_ghost = toc_ghost


def payload_bytes(element):
    if element.empty:
        return None
    if element.nested is not None:
        raw = build_container(element.nested)
    else:
        raw = element.data if element.data is not None else b""
    if element.deflate:
        return raw_deflate(raw)
    return raw


def write_block(buf, fmt, rel, data, layout):
    """Append a (possibly chained) block. rel is address of this block. Returns new rel."""
    data = data if data is not None else b""
    pages = layout.pages_for(len(data))
    if not pages:
        pages = [0]
    data_size = len(data)
    offset = 0
    header_rels = []
    chunks = []
    for i, page_size in enumerate(pages):
        take = min(page_size, data_size - offset)
        if take < 0:
            take = 0
        chunk = data[offset : offset + take]
        if len(chunk) < page_size:
            chunk = chunk + b"\x00" * (page_size - len(chunk))
        header_rels.append(None)
        chunks.append((page_size, chunk, take))
        offset += take

    positions = []
    cursor = rel
    for page_size, chunk, take in chunks:
        positions.append(cursor)
        cursor += fmt.block_header_size + len(chunk)

    for i, (page_size, chunk, take) in enumerate(chunks):
        next_addr = positions[i + 1] if i + 1 < len(positions) else fmt.undef
        buf.extend(fmt.block_header(data_size, page_size, next_addr))
        buf.extend(chunk)
    return cursor


def build_container(container):
    fmt = container.fmt
    prepared = []
    for el in container.elements:
        header = elem_header_bytes(el.name)
        data = payload_bytes(el)
        prepared.append((el, header, data))
    if container.toc_ghost:
        ghost_header = elem_header_bytes(container.toc_ghost[0])
        ghost_data = container.toc_ghost[1]
        prepared_ghost = (ghost_header, ghost_data)
    else:
        prepared_ghost = None

    n_real = len(prepared)
    n_toc = n_real + container.toc_extra_unused + (1 if prepared_ghost else 0)
    toc_bytes_len = n_toc * fmt.elem_addr_size

    rel = fmt.file_header_size
    toc_rel = rel

    def disk_size(data_len, layout):
        pages = layout.pages_for(data_len)
        if not pages:
            pages = [0]
        total = 0
        remaining = data_len
        for page_size in pages:
            take = min(page_size, remaining)
            chunk_len = page_size
            total += fmt.block_header_size + chunk_len
            remaining -= take
        return total

    rel += disk_size(toc_bytes_len, container.toc_layout)
    addrs = []
    for el, header, data in prepared:
        header_rel = rel
        rel += disk_size(len(header), el.header_layout)
        if data is None:
            data_rel = fmt.undef
        else:
            data_rel = rel
            rel += disk_size(len(data), el.data_layout)
        addrs.append((header_rel, data_rel))

    ghost_addrs = None
    if prepared_ghost:
        gh, gd = prepared_ghost
        ghost_header_rel = rel
        rel += disk_size(len(gh), BlockLayout(exact=True))
        ghost_data_rel = rel
        rel += disk_size(len(gd), BlockLayout())
        ghost_addrs = (ghost_header_rel, ghost_data_rel)

    toc = bytearray()
    for header_rel, data_rel in addrs:
        toc.extend(fmt.elem_addr(header_rel, data_rel))
    for _ in range(container.toc_extra_unused):
        toc.extend(fmt.elem_addr(0, 0, marker=0))
    if ghost_addrs:
        toc.extend(fmt.elem_addr(ghost_addrs[0], ghost_addrs[1], marker=fmt.undef))

    if len(toc) != toc_bytes_len:
        raise RuntimeError("TOC size mismatch")

    body = bytearray()
    body.extend(fmt.file_header())
    write_pos = write_block(body, fmt, toc_rel, bytes(toc), container.toc_layout)
    for (el, header, data), (header_rel, data_rel) in zip(prepared, addrs):
        if write_pos != header_rel:
            raise RuntimeError("header addr drift %d != %d" % (write_pos, header_rel))
        write_pos = write_block(body, fmt, header_rel, header, el.header_layout)
        if data is not None:
            if write_pos != data_rel:
                raise RuntimeError("data addr drift %d != %d" % (write_pos, data_rel))
            write_pos = write_block(body, fmt, data_rel, data, el.data_layout)

    if prepared_ghost:
        gh, gd = prepared_ghost
        write_pos = write_block(body, fmt, ghost_addrs[0], gh, BlockLayout(exact=True))
        write_pos = write_block(body, fmt, ghost_addrs[1], gd, BlockLayout())

    if fmt.kind == F16:
        return PLACEHOLDER + bytes(body)
    return bytes(body)


def write_expected(path, container, inflate):
    if not os.path.isdir(path):
        os.makedirs(path)
    for el in container.elements:
        dest = os.path.join(path, el.name)
        stored = payload_bytes(el)
        if stored is None:
            continue
        # Root parse inflates, then recurses into a nested V8 without further inflate.
        if el.nested is not None and (inflate or not el.deflate):
            write_expected(dest, el.nested, inflate=False)
        else:
            if inflate and el.deflate and el.nested is None:
                data = el.data if el.data is not None else b""
            else:
                data = stored
            parent = os.path.dirname(dest)
            if parent and not os.path.isdir(parent):
                os.makedirs(parent)
            with open(dest, "wb") as f:
                f.write(data)


def emit(name, container):
    root = os.path.join(FIXTURES, name)
    expected = os.path.join(root, "expected")
    if os.path.exists(root):
        for dirpath, dirnames, filenames in os.walk(root, topdown=False):
            for fn in filenames:
                os.remove(os.path.join(dirpath, fn))
            for dn in dirnames:
                os.rmdir(os.path.join(dirpath, dn))
        os.rmdir(root)
    os.makedirs(expected)
    blob = build_container(container)
    with open(os.path.join(root, "in.cf"), "wb") as f:
        f.write(blob)
    write_expected(expected, container, inflate=True)
    print("  %s  (%d bytes)" % (name, len(blob)))


def main():
    if not os.path.isdir(FIXTURES):
        os.makedirs(FIXTURES)

    print("Generating fixtures...")

    emit(
        "f15-flat",
        Container(
            F15,
            [
                Element("alpha", b"one"),
                Element("beta", b"two-two"),
                Element("gamma", b"three-three-three"),
            ],
        ),
    )

    emit(
        "f15-deflate",
        Container(
            F15,
            [
                Element("plain", b"not a container payload\n", deflate=True),
                Element("more", b"another zlib-raw body", deflate=True),
            ],
        ),
    )

    emit(
        "f16z-inner",
        Container(
            F16Z,
            [
                Element("info", b"inner-64-at-zero"),
                Element("form", b"module-body"),
            ],
        ),
    )

    inner_raw = Container(
        F15,
        [
            Element("child_a", b"nested-a"),
            Element("child_b", b"nested-b"),
        ],
    )
    emit(
        "f15-nested-raw",
        Container(
            F15,
            [
                Element("outer", b"keep-me"),
                Element("pack", nested=inner_raw, deflate=False),
            ],
        ),
    )

    inner_f16z = Container(
        F16Z,
        [
            Element("doc", b"platform-like inner", deflate=True),
            Element("text", b"still compressed on disk", deflate=True),
        ],
    )
    emit(
        "f16-nested-f16z-deflate",
        Container(
            F16,
            [Element("guid-object", nested=inner_f16z, deflate=True)],
        ),
    )

    inner_f15 = Container(
        F15,
        [
            Element("file1", b"tool-style inner"),
            Element("file2", b"uncompressed child"),
        ],
    )
    emit(
        "f16-nested-f15",
        Container(
            F16,
            [Element("folder", nested=inner_f15, deflate=True)],
        ),
    )

    paged = b"ABCDEFGHIJ0123456789"
    emit(
        "f15-paged-data",
        Container(
            F15,
            [
                Element(
                    "split",
                    paged,
                    data_layout=BlockLayout(page_sizes=[8, 8, 8]),
                )
            ],
        ),
    )

    emit(
        "f15-paged-toc",
        Container(
            F15,
            [
                Element("e0", b"v0"),
                Element("e1", b"v1"),
                Element("e2", b"v2"),
                Element("e3", b"v3"),
                Element("e4", b"v4"),
            ],
            toc_layout=BlockLayout(page_sizes=[28, 28, 28]),
            toc_extra_unused=1,
            toc_ghost=("SHOULD_NOT_APPEAR", b"ghost-payload"),
        ),
    )

    emit(
        "f16-paged-data",
        Container(
            F16,
            [
                Element(
                    "wide",
                    b"offset-must-include-placeholder-prefix!!",
                    data_layout=BlockLayout(page_sizes=[10, 10, 20]),
                )
            ],
        ),
    )

    emit(
        "f15-empty-elem",
        Container(
            F15,
            [
                Element("present", b"I exist"),
                Element("absent", empty=True),
            ],
        ),
    )

    emit(
        "f15-page-padding",
        Container(
            F15,
            [Element("short", b"hello", data_layout=BlockLayout(pad_to=64))],
        ),
    )

    emit(
        "f15-guid-names",
        Container(
            F15,
            [
                Element("root", b"{2,0}"),
                Element("version", b"{{{80314,0},0}"),
                Element("30ffe4cc-eef2-4371-8b26-046597e37e22", b"metadata-object"),
            ],
        ),
    )

    emit(
        "f16-zeropad-hex",
        Container(
            F16,
            [Element("zeroed", b"same layout, zero hex digits")],
            hex_style="zero",
        ),
    )

    mixed_inner = Container(F15, [Element("inside", b"dir-content")])
    emit(
        "mixed-one-empty-one-nested",
        Container(
            F15,
            [
                Element("raw", b"as-is"),
                Element("zipped", b"inflate-me", deflate=True),
                Element("missing", empty=True),
                Element("tree", nested=mixed_inner, deflate=False),
            ],
        ),
    )

    leaf = Container(F15, [Element("leaf", b"depth-3")])
    mid = Container(F15, [Element("level2", nested=leaf, deflate=False)])
    emit(
        "f15-depth3",
        Container(F15, [Element("level1", nested=mid, deflate=False)]),
    )

    print("Done.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
