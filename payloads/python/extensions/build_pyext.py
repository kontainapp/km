
import json
import os
import re
import subprocess

base = os.path.basename(os.getcwd())
f = open('km_capture.json', 'r')
libs = json.load(f)
f.close()

total_L = set()
total_l = set()
f_setup_local = open('km_setup.local', 'w')
for lib in libs:
    pkg = re.sub(r'/', '.',
                 re.sub(r'\.cpython.*\.so$', '',
                        re.sub(r'build/lib\.linux-x86_64-3\.7/', '', lib['name'])))
    munged_modname = '__KM_{}'.format(
        ''.join('_{}'.format(x) for x in pkg.split('.')))
    dopts = set()
    uopts = set()
    iopts = set()
    srcs = list()
    for source in lib['sources']:
        status, output = subprocess.getstatusoutput(
            'grep PyInit_ {}'.format(source['source']))
        if status == 0:
            status, newmain = subprocess.getstatusoutput(
                "sed -e 's/PyInit_{}/PyInit_{}/' {}".format(pkg.split('.')[-1], munged_modname, source['source']))
            mainname = '{}/km_{}_{}'.format(os.path.dirname(
                source['source']), os.path.dirname(source['source']).replace('/', '_'), os.path.basename(source['source']))
            f = open(mainname, 'w')
            f.write(newmain)
            f.close()
            srcs.append(mainname)
        else:
            srcs.append(source['source'])

        for opt in source['dopts']:
            dopts.add(opt)

        for opt in source['uopts']:
            uopts.add(opt)

        for opt in source['iopts']:
            if re.match(r'-I/.*', opt):
                continue
            iopts.add(opt)

    dflags = ''.join(' {}'.format(x) for x in dopts)
    uflags = ''.join(' {}'.format(x) for x in uopts)
    iflags = ''.join(' -IModules/{}/{}'.format(base, re.sub(r'-I', '', x))
                     for x in iopts)
    sources = ''.join(' {}/{}'.format(base, x) for x in srcs)
    Lflags = [re.sub(r'-L', '', x)
              for x in lib['libpath'] if not re.match(r'-L/.*', x)]
    Lstr = ''.join(' -LModules/{}/{}'.format(base, Lflags))
    if len(Lflags) == 0:
        Lstr = ''
    lflags = ''.join(' {}'.format(x) for x in lib['ldoptions'])
    f_setup_local.write('{}{}{}{}{}{}{}\n'.format(munged_modname, sources, dflags,
                                                  uflags, iflags, Lstr, lflags))

    # Collect -L and -L for link-km.sh
    for l in Lflags:
        total_L.add(l)
    for l in lib['ldoptions']:
        total_l.add(l)

f_setup_local.close()

f = open('km_libs.json', 'w')
json.dump({'L': list(total_L), 'l': list(total_l)}, f, indent=4)
f.close()
