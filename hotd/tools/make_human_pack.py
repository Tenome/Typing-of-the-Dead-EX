"""Builds the "hotd_humans" model pack: Typing of the Dead EX's civilians and story characters become the people
of The House of the Dead (agents, Sophie, scientists ...). Kinds 62-64 and 66-67 can then be chosen as
"Player 1/2 as" in todmod_config.exe to play as them.

    python make_human_pack.py <thotd.exe> <HOTD creature folder> <TODEX folder> [--name hotd_humans]
"""
import argparse
import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import to_tod

# (TOD kind, HOTD body, name shown in the configurator)
CAST = [
    (63, 2, 'AMS agent in a brown jacket'),
    (67, 2, 'AMS agent in a brown jacket (story)'),
    (62, 13, 'AMS agent in a dark suit'),
    (66, 13, 'AMS agent in a dark suit (story)'),
    (64, 15, 'Man in a dark suit, yellow tie'),
    (56, 34, 'Sophie'),
    (68, 67, 'Sophie (second look)'),
    (32, 17, 'Scientist'),
    (33, 23, 'Scientist (woman)'),
    (34, 25, 'Scientist (woman, blond)'),
    (35, 14, 'Man in a red jacket'),
    (36, 19, 'Scientist 2'),
    (37, 26, 'Scientist (woman, brown hair)'),
    (38, 34, 'Sophie'),
    (39, 67, 'Sophie (second look)'),
    (40, 27, 'Scientist (woman 2)'),
    (41, 60, 'Robed figure with a sword'),
    (42, 78, 'Robed figure with a sword 2'),
    (43, 20, 'Scientist 3'),
    (44, 21, 'Scientist 4'),
    (45, 22, 'Scientist 5'),
    (46, 49, 'Scientist 6'),
    (47, 2, 'AMS agent in a brown jacket'),
    (48, 13, 'AMS agent in a dark suit'),
    (49, 15, 'Man in a dark suit, yellow tie'),
    (50, 14, 'Man in a red jacket'),
    (51, 17, 'Scientist'),
    (52, 19, 'Scientist 2'),
    (53, 20, 'Scientist 3'),
    (54, 23, 'Scientist (woman)'),
    (55, 26, 'Scientist (woman, brown hair)'),
]
DESCRIPTION = 'House of the Dead (1998) people (agents, Sophie, scientists) for the civilians and story characters.'


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('exe')
    parser.add_argument('creatures')
    parser.add_argument('todex')
    parser.add_argument('--name', default='hotd_humans')
    options = parser.parse_args()
    for folder in ('models', 'textures'):
        shutil.rmtree(os.path.join(options.todex, folder, options.name), ignore_errors=True)
    for kind, body, _ in CAST:
        args = argparse.Namespace(exe=options.exe, creatures=options.creatures, todex=options.todex, body=body,
                                  kinds=[kind], motion=None, slot=None, name=options.name,
                                  vmc=os.path.join(options.creatures, 'texture.vmc'))
        to_tod.convert(args)
    folder = os.path.join(options.todex, 'models', options.name)
    with open(os.path.join(folder, 'description.txt'), 'w', encoding='utf-8') as out:
        out.write(DESCRIPTION + '\n')
    with open(os.path.join(folder, 'characters.tsv'), 'w', encoding='utf-8', newline='\n') as out:
        out.write('# kind\tname shown on the Characters tab\n')
        out.write(''.join('%d\t%s (House of the Dead)\n' % (kind, name) for kind, _, name in CAST))


main()
