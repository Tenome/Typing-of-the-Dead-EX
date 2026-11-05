"""Typing of the Dead EX character kinds (Table.dll BodyInitTbl): body trees, part models and banks.

  python character_kinds.py <Table.dll>

BodyInitTbl[kind] points to the root node of the kind's body. A node is
  +0x00 u32 model id (0 = none), +0x04 float x, y, z offset from the parent, +0x10 u32 flags,
  +0x14 s16 part id, +0x16 s16 child count, +0x18 child node pointers.
"""
import collections
import struct
import sys

import model_formats as mf

NODE_CHILDREN_OFFSET = 0x18


def body_parts(tables, kind):
    """[(part id, model id, (x, y, z) absolute position)] in tree order."""
    executable = tables.executable
    root = executable.u32(tables.exports["BodyInitTbl"] + 4 * kind)
    parts = []

    def walk(node, origin, depth):
        offset = executable.offset(node)
        if offset is None or depth > 32:
            return
        model_id, x, y, z = struct.unpack_from("<Ifff", executable.blob, offset)
        part_id, children = struct.unpack_from("<hh", executable.blob, offset + 0x14)
        position = (origin[0] + x, origin[1] + y, origin[2] + z)
        parts.append((part_id, model_id, position))
        for i in range(max(children, 0)):
            walk(executable.u32(node + NODE_CHILDREN_OFFSET + 4 * i), position, depth + 1)

    if root and executable.offset(root) is not None:
        walk(root, (0.0, 0.0, 0.0), 0)
    return parts


def kind_count(tables):
    count = 0
    while True:
        pointer = tables.executable.u32(tables.exports["BodyInitTbl"] + 4 * count)
        if not pointer or tables.executable.offset(pointer) is None:
            return count
        count += 1


def kind_banks(tables, kind):
    banks = collections.Counter()
    for _, model_id, _ in body_parts(tables, kind):
        if 0 < model_id < 0x24b3:
            bank = tables.u16(tables.exports["PbkTbl"] + 2 * model_id)
            if bank != mf.NO_BANK:
                banks[tables.bank_name(bank)] += 1
    return banks


def main():
    tables = mf.ModelTables(sys.argv[1])
    for kind in range(kind_count(tables)):
        parts = body_parts(tables, kind)
        models = sum(1 for _, model_id, _ in parts if model_id)
        banks = ", ".join(f"{name[:-4]}x{count}" for name, count in kind_banks(tables, kind).items())
        print(f"{kind}\t{len(parts)} nodes\t{models} models\t{banks}")


if __name__ == "__main__":
    main()
