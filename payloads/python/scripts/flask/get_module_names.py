#!/usr/bin/env python3
#
# see examples https://www.programcreek.com/python/example/102029/modulefinder.ModuleFinder

import click
from modulefinder import ModuleFinder


@click.command()
@click.option('--all', is_flag=True, help='Show all, i.e. also show not imported modules ')
@click.argument('name')
def get_modules(name, all):
    """
    Get modules imported by script NAME
    """

    finder = ModuleFinder()
    finder.run_script(name)
    print('Loaded modules:')
    for name, mod in finder.modules.items():
        print('%s: ' % name, end='')
        print(','.join(list(mod.globalnames.keys())[:3]))
    if all:
        print('-' * 50)
        print('Modules not imported:')
        print('\n'.join(finder.badmodules.keys()))


if __name__ == '__main__':
    get_modules()
