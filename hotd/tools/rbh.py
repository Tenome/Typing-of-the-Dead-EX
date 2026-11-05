"""Reads The House of the Dead (PC) RBH files: PIFF/RBHF container, BODY sections and RELC pointer fixups."""
import struct


class Rbh:
    def __init__(self, path):
        data = open(path, 'rb').read()
        if data[:4] != b'PIFF' or data[8:12] != b'RBHF':
            raise ValueError(path + ' is not an RBH file')
        self.sections = []
        self.relocations = []
        offset = 12
        while offset + 8 <= len(data):
            tag = data[offset:offset + 4]
            size = struct.unpack_from('<I', data, offset + 4)[0]
            body = data[offset + 8:offset + 8 + size]
            if tag == b'BODY':
                self.sections.append(bytearray(body))
            elif tag == b'RELC':
                source, target = struct.unpack_from('<II', body)
                offsets = struct.unpack_from('<%dI' % ((size - 8) // 4), body, 8)
                self.relocations.append((source, target, offsets))
            offset += 8 + size + (size & 1)
        self.pointer_targets = [{} for _ in self.sections]
        for source, target, offsets in self.relocations:
            for at in offsets:
                self.pointer_targets[source][at] = target

    def u32(self, section, at):
        return struct.unpack_from('<I', self.sections[section], at)[0]

    def pointer(self, section, at):
        """Returns (target section, offset) for a relocated pointer, or None."""
        target = self.pointer_targets[section].get(at)
        if target is None:
            return None
        return target, self.u32(section, at)


GROUP_HEADER = 8
FACE_STRIDES = {0: 8, 1: 10, 2: 36, 4: 44}


class Part:
    """One mesh: vertex positions and faces (vertex indices, texture page or None, pixel UVs or colour index)."""

    def __init__(self, rbh, offset):
        s0 = rbh.sections[0]
        self.offset = offset
        self.face_pointer = rbh.pointer(0, offset)
        self.vertex_pointer = rbh.pointer(0, offset + 4)
        self.colour_pointer = rbh.pointer(0, offset + 8)
        self.vertex_count, self.face_count = struct.unpack_from('<II', s0, offset + 16)
        self.radius = struct.unpack_from('<f', s0, offset + 24)[0]
        section, at = self.vertex_pointer
        self.vertices = [struct.unpack_from('<3f', rbh.sections[section], at + 12 * i) for i in range(self.vertex_count)]
        self.faces = []
        self.group_kinds = []
        section, at = self.face_pointer
        data = rbh.sections[section]
        while len(self.faces) < self.face_count:
            count, kind, size, _words = struct.unpack_from('<4H', data, at)
            if kind == 0xFFFF:
                break
            self.group_kinds.append((kind, count))
            stride = FACE_STRIDES[kind & 7]
            body = at + GROUP_HEADER
            for i in range(count):
                self.faces.append(self._face(data, body + i * stride, kind & 7))
            at = body + size - 2

    @staticmethod
    def _face(data, at, shape):
        if shape == 4:
            indices = struct.unpack_from('<4H', data, at)
            page = struct.unpack_from('<I', data, at + 8)[0]
            uvs = struct.unpack_from('<8f', data, at + 12)
            return indices, page, [uvs[0:2], uvs[2:4], uvs[4:6], uvs[6:8]]
        if shape == 2:
            indices = struct.unpack_from('<3H', data, at)
            page = struct.unpack_from('<I', data, at + 6)[0]
            uvs = struct.unpack_from('<6f', data, at + 12)
            return indices, page, [uvs[0:2], uvs[2:4], uvs[4:6]]
        corners = 4 if shape == 1 else 3
        values = struct.unpack_from('<%dH' % (corners + 1), data, at)
        return values[:corners], None, values[corners]

    def colour(self, rbh, index):
        if not self.colour_pointer:
            return (0.5, 0.5, 0.5)
        section, at = self.colour_pointer
        blue, green, red, _ = rbh.sections[section][at + 4 * index:at + 4 * index + 4]
        return (red / 255, green / 255, blue / 255)


def part_offsets(rbh):
    """Offsets of the id table entries: [(id, offset)] until the table ends."""
    s0 = rbh.sections[0]
    entries = []
    at = 0
    while struct.unpack_from('<H', s0, at + 2)[0] == 0xFFFF:
        ident = struct.unpack_from('<H', s0, at)[0]
        entries.append((ident, rbh.u32(0, at + 4)))
        at += 8
    return entries
