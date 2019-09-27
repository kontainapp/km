# sitecustomize.py for KM. Installed in site directory as sitecustomize.py.
# Python runs script before main is called which allows us to insert our
# custom MetaPathFinder into the import system.
from importlib.abc import MetaPathFinder, Loader
from importlib.util import find_spec
from importlib.machinery import ModuleSpec
import sys


class MyMetaPathFinder(MetaPathFinder):
    def find_spec(self, fullname, path, target=None):
        mname = '__KM_{}'.format(fullname.replace('.', '_'))
        if mname in sys.builtin_module_names:
            return find_spec(mname)
        return None


sys.meta_path.insert(0, MyMetaPathFinder())
print('KM Import Hook installed')
