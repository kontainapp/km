# This extracts a shared object build that was captured with 'bear'
# into 'compile_commands.json' and the stdout captured
# into 'bear.out'. This merges the data to capture the
import json
import os.path
import subprocess
import re


if __name__ == '__main__':
    # Load the
    f = open('compile_commands.json', 'r')
    compiles = json.load(f)
    f.close()

    sources = dict()
    for cmd in compiles:
        objs = [opt for opt in cmd['arguments'] if re.match(r'.*\.o', opt)]
        if len(objs) != 1:
            raise Exception('should have a single object')
        sources[objs[0]] = {'source': cmd['file'],
                            'dopts': [opt for opt in cmd['arguments'] if re.match(r'-D.*', opt)],
                            'uopts': [opt for opt in cmd['arguments'] if re.match(r'-U.*', opt)],
                            'iopts': [opt for opt in cmd['arguments'] if re.match(r'-I.*', opt)],
                            }

    status, output = subprocess.getstatusoutput("grep '\.so' bear.out")
    if status != 0:
        raise Exception('bad status from grep - {}'.format(status))

    libs = list()
    for line in output.splitlines():
        #ldopts = [f for f in line.split() if re.match(r'-Wl.*', f)]
        ldopts = [f for f in line.split() if re.match(
            r'-l.*', f) and not re.match(r'-lpython*', f)]
        libpath = [f for f in line.split() if re.match(r'-L.*', f)]
        objs = [f for f in line.split() if re.match(r'.*\.o', f)]
        so = [f for f in line.split() if re.match(r'.*\.so', f)]
        if len(so) != 1:
            raise Exception('expected one .so file. Found {}', len(so))
        libs.append({'name': so[0], 'objects': objs,
                     'ldoptions': ldopts, 'libpath': libpath,
                     'sources': [sources[obj] for obj in objs]})

    f = open('km_capture.json', 'w')
    json.dump(libs, f, indent=4)
    f.close()
