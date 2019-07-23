# Copyright © 2019 Kontain Inc. All rights reserved.
#
# Kontain Inc CONFIDENTIAL
#
#  This file includes unpublished proprietary source code of Kontain Inc. The
#  copyright notice above does not evidence any actual or intended publication of
#  such source code. Disclosure of this source code or any related proprietary
#  information is strictly prohibited without the express written permission of
#  Kontain Inc.
#
"""
payloads/python/test_unittest.py

Using sys.modules, get a list of modules available
and import them.
We should find a list of 50 modules.

"""
import unittest


class TestModules(unittest.TestCase):

    def test_import_modules(self):
        NUMBER_OF_MODULES = 50

        with self.assertRaises(ModuleNotFoundError):
            import foo
        import sys

        # Get a list of module names.
        moduleNames = list(
            filter(lambda x: not x.startswith('_'), sys.modules.keys()))

        # Import them all.
        modules = list(map(__import__, moduleNames))
        for name in ['__name__', '__doc__', '__package__', '__loader__', '__spec__']:
            for module in modules:
                self.assertIn(name, module.__dict__.keys())

        # Count them.
        names = sorted(set(map(lambda x: x.__name__, modules)))
        self.assertEqual(len(names), NUMBER_OF_MODULES,
                         "Should have found {} modules".format(NUMBER_OF_MODULES))


if __name__ == '__main__':
    unittest.main()
