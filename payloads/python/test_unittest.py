#
# Copyright 2021 Kontain Inc
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# Copyright © 2019 Kontain Inc. All rights reserved.
#
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

        # Import them all, and check modules have expected fields - that should check Kontain modules link-in.
        import importlib
        modules = list(map(lambda x: importlib.import_module(x), moduleNames))
        for module in modules:
            if not isinstance(module, type(importlib)): # skip non-module classes, if any
               continue
            for name in ['__name__', '__doc__', '__package__', '__loader__', '__spec__']:
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
