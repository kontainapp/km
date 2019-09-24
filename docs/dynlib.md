# Shared Library Emulation

The runtime support for for higher level languages like Python, Java, and NodeJS leverage shared libraries to support adding functionality at runtime. For example, the Python `import` facility allows for modules written in `C` and `C++`.

KM is written for a statically linked environment and does not directly support shared libraries. This paper contains a high level design for supporting language extensions in KM.

## Design Goals
* Zero changes to the core language runtime (e.g. the Python interpreter).
* Static linking. We do not want to do runtime linking for performance and complexity reasons.

## Design Approach

* Only support 'strategic' extensions. Kontain decides what is strategic.
* Customer payload-specific language runtime (e.g. standard Python interpreter plus required binary extension(s)).

## Python
The current state of affairs for Python:
* Need the .o files that would be built into a .so
* run `tools/kontain-bundlelib' to create a .o file with KM linkage

### Conversation with Mark S
Mark Sterin 11:20 AM
markupsafe internally has 2 versions - "native" py and "speedup" C .so, and markupsafe __init__.py picks up native in case .so is missing. So you need to do the following:
* Install markupsafe to payloads lib pip3 install -t ~/workspace/km/payloads/python/cpython/Lib/ markupsafe
* rename .py implementation so it's not picked up: mv ~/workspace/km/payloads/python/cpython/Lib/markupsafe/_native.py ~/workspace/km/payloads/python/cpython/Lib/markupsafe/_native.py.save
* make sure regular python gets the module , if configured to go to payload libs only: PYTHONPATH=~/workspace/km/payloads/python/cpython/Lib/ python3 -c 'import sys; print (sys.path); import markupsafe; print("MARKUPSAFE is in {}".format(markupsafe.__path__))'
* make sure km/python.km fails: km --putenv PYTHONPATH=~/workspace/km/payloads/python/cpython/Lib/ ~/.local/bin/python.km -c 'import sys; print (sys.path); import markupsafe; print("MARKUPSAFE is in {}".format(markupsafe.__path__))'Note that I deliberately moved python.km from the payload dir, because python by default uses the argv0 dir (i.e. where python.km resides) as a root for library search. and I wanted to make sure we can place .km files wherever,  if neededNow about "how do I build markupsafe":  Looking in https://github.com/pallets/markupsafe :

git clone https://github.com/pallets/markupsafe; cd markupsafe
python3 setup.py -v build

seems to be running 2 gccs - not sure why do they need the first

gcc -pthread -Wno-unused-result -Wsign-compare -DDYNAMIC_ANNOTATIONS_ENABLED=1 -DNDEBUG -O2 -g -pipe -Wall -Werror=format-security -Wp,-D_FORTIFY_SOURCE=2 -Wp,-D_GLIBCXX_ASSERTIONS -fexceptions -fstack-protector-strong -grecord-gcc-switches -m64 -mtune=generic -fasynchronous-unwind-tables -fstack-clash-protection -fcf-protection -D_GNU_SOURCE -fPIC -fwrapv -fPIC -I/usr/include/python3.7m -c src/markupsafe/_speedups.c -o build/temp.linux-x86_64-3.7/src/markupsafe/_speedups.o
gcc -pthread -shared -Wl,-z,relro -Wl,--as-needed -Wl,-z,now -g build/temp.linux-x86_64-3.7/src/markupsafe/_speedups.o -L/usr/lib64 -lpython3.7m -o build/lib.linux-x86_64-3.7/markupsafe/_speedups.cpython-37m-x86_64-linux-gnu.so

(all the above obviously assumes KM repo cloned to ~/workspace/km) (edited) 