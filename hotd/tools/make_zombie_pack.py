"""Builds the "hotd_zombies" model pack: every human zombie kind of Typing of the Dead EX becomes a different
House of the Dead zombie.

    python make_zombie_pack.py <thotd.exe> <HOTD creature folder> <TODEX folder> [--name hotd_zombies]

Choose the pack in todmod_config.exe ("Model packs"); todmod uses textures\\<pack> with it automatically.
"""
import argparse
import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import to_tod

TOD_ZOMBIE_KINDS = list(range(1, 21)) + [22, 24, 25]
HOTD_ZOMBIE_BODIES = [24, 84, 57, 62, 36, 35, 28, 29, 30, 53, 61, 73, 82, 83, 86, 45, 47, 11, 12, 16, 32, 31, 33]
DESCRIPTION = 'House of the Dead (1998) zombies for every human zombie of Typing of the Dead, from your own copy.'


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('exe')
    parser.add_argument('creatures')
    parser.add_argument('todex')
    parser.add_argument('--name', default='hotd_zombies')
    options = parser.parse_args()
    for folder in ('models', 'textures'):
        shutil.rmtree(os.path.join(options.todex, folder, options.name), ignore_errors=True)
    for kind, body in zip(TOD_ZOMBIE_KINDS, HOTD_ZOMBIE_BODIES):
        args = argparse.Namespace(exe=options.exe, creatures=options.creatures, todex=options.todex, body=body,
                                  kinds=[kind], motion=None, slot=None, name=options.name,
                                  vmc=os.path.join(options.creatures, 'texture.vmc'))
        to_tod.convert(args)
    with open(os.path.join(options.todex, 'models', options.name, 'description.txt'), 'w', encoding='utf-8') as out:
        out.write(DESCRIPTION + '\n')
    with open(os.path.join(options.todex, 'models', options.name, 'characters.tsv'), 'w', encoding='utf-8',
              newline='\n') as out:
        out.write('# kind\tname shown on the Characters tab\n')
        out.write(''.join('%d\tHouse of the Dead zombie (body %d)\n' % (kind, body)
                          for kind, body in zip(TOD_ZOMBIE_KINDS, HOTD_ZOMBIE_BODIES)))


main()
