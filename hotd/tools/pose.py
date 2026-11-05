"""Motion data of thotd.exe and posing of body hierarchies.

Body roots: table at 0x5ef8f8 (94 entries); joint counts at 0x5efa70; motion pointers at 0x5efbe8 (674);
frame counts at 0x5f0670. A frame is 3 root values followed by 3 signed 16-bit angles per joint
(0x10000 = 360 degrees), padded to 4 bytes: (joints * 6 + 15) & ~3 bytes.
"""
import math
import struct

import exe_view as exe

BODY_TABLE = 0x5EF8F8
BODY_COUNT = 94
JOINT_COUNTS = 0x5EFA70
MOTION_TABLE = 0x5EFBE8
FRAME_COUNTS = 0x5F0670
MOTION_COUNT = (FRAME_COUNTS - MOTION_TABLE) // 4


def body_roots():
    return list(struct.unpack('<%dI' % BODY_COUNT, exe.read(BODY_TABLE, 4 * BODY_COUNT)))


def joint_count(body):
    return struct.unpack('<I', exe.read(JOINT_COUNTS + 4 * body, 4))[0]


def frame_count(motion):
    return struct.unpack('<h', exe.read(FRAME_COUNTS + 4 * motion, 2))[0]


def motion_joint_count(motion):
    """Joint count implied by the motion's data size, or None."""
    pointers = struct.unpack('<2I', exe.read(MOTION_TABLE + 4 * motion, 8))
    frames = frame_count(motion)
    if motion + 1 >= MOTION_COUNT or frames <= 0:
        return None
    for joints in range(2, 40):
        if ((joints * 6 + 15) & ~3) * frames == pointers[1] - pointers[0]:
            return joints
    return None


def motion_frame(motion, frame, joints):
    size = (joints * 6 + 15) & ~3
    base = struct.unpack('<I', exe.read(MOTION_TABLE + 4 * motion, 4))[0]
    raw = exe.read(base + frame * size, size)
    root = struct.unpack_from('<3f', raw, 0)
    angles = [struct.unpack_from('<3h', raw, 12 + 6 * j) for j in range(joints)]
    return root, angles


def rotation(axis, units):
    angle = units * 2 * math.pi / 65536.0
    c, s = math.cos(angle), math.sin(angle)
    if axis == 'x':
        return [[1, 0, 0], [0, c, -s], [0, s, c]]
    if axis == 'y':
        return [[c, 0, s], [0, 1, 0], [-s, 0, c]]
    return [[c, -s, 0], [s, c, 0], [0, 0, 1]]


def multiply(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(3)) for j in range(3)] for i in range(3)]


def apply(m, v):
    return tuple(sum(m[i][k] * v[k] for k in range(3)) for i in range(3))


IDENTITY = [[1, 0, 0], [0, 1, 0], [0, 0, 1]]


def joint_matrix(angles, order='zyx'):
    """As thotd.exe FUN_00476bc0: rotate Z by -a2, then Y by a1, then X by a0."""
    signed = {'x': angles[0], 'y': angles[1], 'z': -angles[2]}
    m = IDENTITY
    for axis in order:
        m = multiply(m, rotation(axis, signed[axis]))
    return m


def posed_nodes(root_va, nodes, angles, order='zyx', root_offset=(0.0, 0.0, 0.0), negate_z=True):
    """[(node, world rotation, world position)] for the tree, parent before child."""
    result = []
    stack = [(root_va, IDENTITY, root_offset)]
    while stack:
        va, parent_rotation, parent_position = stack.pop()
        node = nodes[va]
        x, y, z = node['offset']
        offset = apply(parent_rotation, (x, y, -z if negate_z else z))
        position = tuple(parent_position[k] + offset[k] for k in range(3))
        local = joint_matrix(angles[node['index']], order) if node['index'] < len(angles) else IDENTITY
        world = multiply(parent_rotation, local)
        result.append((node, world, position))
        for child in node['children']:
            stack.append((child, world, position))
    return result
