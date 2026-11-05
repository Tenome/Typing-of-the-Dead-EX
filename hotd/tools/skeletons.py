"""Finds the body hierarchies compiled into thotd.exe.

Node layout (dwords): part id, x, y, z (offset from the parent), flag, node index, child count,
child node pointers (child count of them), 0.
"""
import struct

import exe_view as exe

MAX_CHILDREN = 8
MAX_OFFSET = 200.0


def dword(va):
    return struct.unpack('<I', exe.read(va, 4))[0]


MAX_ID = 0x4000


def parse_node(va, raw):
    values = struct.unpack('<%dI' % (len(raw) // 4), raw)
    ident, index, child_count = values[0], values[5], values[6]
    if ident >= MAX_ID or child_count > MAX_CHILDREN or index > 64 or values[4] > 16:
        return None
    if values[7 + child_count] != 0:
        return None
    offset = struct.unpack('<3f', raw[4:16])
    if any(not (abs(c) < MAX_OFFSET) for c in offset):
        return None
    children = list(values[7:7 + child_count])
    if any(child % 4 or exe.va_to_file(child) is None for child in children):
        return None
    return {'va': va, 'id': ident, 'offset': offset, 'index': index, 'flag': values[4], 'children': children}


def find_nodes(known_ids=None):
    """Nodes whose children are nodes too (repeated until stable)."""
    candidates = {}
    data = exe.EXE
    for start, raw, size in exe.SECTIONS:
        for at in range(raw, raw + size - 4 * (8 + MAX_CHILDREN), 4):
            if data[at + 26:at + 28] != b'\0\0' or data[at + 24] > MAX_CHILDREN:
                continue
            node = parse_node(exe.file_to_va(at), data[at:at + 4 * (8 + MAX_CHILDREN)])
            if node:
                candidates[node['va']] = node
    changed = True
    while changed:
        changed = False
        for va in list(candidates):
            if any(child not in candidates for child in candidates[va]['children']):
                del candidates[va]
                changed = True
    return candidates


def trees(nodes):
    """Roots with their nodes: [(root va, [node, ...])], keeping only trees whose children were all found."""
    referenced = {child for node in nodes.values() for child in node['children']}
    result = []
    for va, node in nodes.items():
        if va in referenced:
            continue
        members, stack, complete = [], [va], True
        while stack:
            current = stack.pop()
            if current not in nodes:
                complete = False
                break
            members.append(nodes[current])
            stack.extend(nodes[current]['children'])
        if complete and len(members) > 1:
            result.append((va, members))
    return result
