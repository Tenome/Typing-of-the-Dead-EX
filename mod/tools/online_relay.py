"""UDP relay for todmod online play.

Both players set the same `[online] relay=<server>:<port>` and `room=<name>`; one has `mode=host`, the other
`mode=join`. The relay only forwards each room's packets between the host and the joining player, so neither player
needs to open a port. Run it on any machine both players can reach (a small VPS, or a PC with this UDP port
forwarded):

    python online_relay.py --port 7846

Packets start with the todmod header: magic "TODL", version, kind, role (1 host, 2 join), room hash, boot id.
"""
import argparse
import socket
import struct
import time

HEADER = struct.Struct("<IBBBBII")
MAGIC = 0x4C444F54
ROLE_HOST, ROLE_JOIN = 1, 2
ROOM_IDLE_SECONDS = 120


def other_role(role):
    return ROLE_JOIN if role == ROLE_HOST else ROLE_HOST


def run(port, verbose):
    relay = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    relay.bind(("0.0.0.0", port))
    relay.settimeout(1.0)
    rooms = {}
    print(f"todmod relay listening on UDP port {port}", flush=True)
    while True:
        now = time.monotonic()
        for room in [r for r, members in rooms.items() if all(now - seen > ROOM_IDLE_SECONDS
                                                              for _, seen in members.values())]:
            print(f"room {room:08x} closed (idle)", flush=True)
            del rooms[room]
        try:
            data, sender = relay.recvfrom(2048)
        except socket.timeout:
            continue
        except ConnectionResetError:
            continue
        if len(data) < HEADER.size:
            continue
        magic, _version, _kind, role, _reserved, room, _boot = HEADER.unpack_from(data)
        if magic != MAGIC or role not in (ROLE_HOST, ROLE_JOIN) or room == 0:
            continue
        members = rooms.setdefault(room, {})
        known = members.get(role)
        if not known or known[0] != sender:
            print(f"room {room:08x}: {'host' if role == ROLE_HOST else 'join'} at {sender[0]}:{sender[1]}", flush=True)
        members[role] = (sender, now)
        partner = members.get(other_role(role))
        if partner:
            relay.sendto(data, partner[0])
        elif verbose:
            print(f"room {room:08x}: waiting for the other player", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--port", type=int, default=7846)
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args()
    run(args.port, args.verbose)


if __name__ == "__main__":
    main()
