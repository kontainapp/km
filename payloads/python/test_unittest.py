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

"""
import unittest

class TestModules(unittest.TestCase):

    def test_import_modules(self):

        with self.assertRaises(ModuleNotFoundError):
            import foo
        import sys

        # Get a list of module names.
        moduleNames = list(
            filter(lambda x: not x.startswith('_'), sys.modules.keys()))

        number_of_modules = len(set(moduleNames))

        # Import them all.
        import importlib
        modules = list(map(lambda x: importlib.import_module(x), moduleNames))
        for name in ['__name__', '__doc__', '__package__', '__loader__', '__spec__']:
            for module in modules:
                self.assertIn(name, module.__dict__.keys())

        # Verify
        names = sorted(set(map(lambda x: x.__name__, modules)))

        """
        stackoverflow:
        'os.path works in a funny way. It looks like os should be a package with a submodule path, 
        but in reality os is a normal module that does magic with sys.modules to inject os.pah'
        """
        self.assertEqual(set(moduleNames) - set(names), {'os.path'})
        self.assertEqual(len(names)+1, number_of_modules,
                         "Should have found {} modules".format(len(names)+1))


if __name__ == '__main__':
    unittest.main()
