# Python as KM Payload

In this directory we build cpython (Python interpreter written in C, the default one) as a KM payload.

The process includes cloning python git repo, patching python config, and building python.km, using MUSL-based runtime.

We also support building a Docker container with pythin.km and pushing it to Azure Container Registry.

## Building python as a KM payload

`make` builds using 'blank' conainer (use `make buildenv-image` or `make pull-buildenv-image` to prepare environment)
`make clobber; make fromsrc` will do local make:

* It will clone all the involved repos
* it will then build musl, km, python and python.km.

After it is done, you can pass `cpython/python.km` to KM as a payload, e.g. `../../build/km/km ./cpython/python.km scripts/hello_again.py`

## Building distro package and publishing it

`make runenv-image` and `make push-runenv-image` will build Docker image and publish it to Azure ACR. 

## Known issues

* if python code uses a syscall we have not implemented yet, it can exit with KM SHUTDOWN
* I am sure there are more, so this list is a placeholder

## Debugging km related python test failures

* To have the current test logged add the -v flag to the command line, like this:

```bash
./python  ./cpython/Lib/unittest/test/ -v
```

You should see output like this:

```txt
testDefault (unittest.test.test_assertions.TestLongMessage) ... ok
testNotAlmostEqual (unittest.test.test_assertions.TestLongMessage) ... ok
testNotEqual (unittest.test.test_assertions.TestLongMessage) ... ok
test_baseAssertEqual (unittest.test.test_assertions.TestLongMessage) ... ok
test_formatMessage_unicode_error (unittest.test.test_assertions.TestLongMessage) ... ok
test_formatMsg (unittest.test.test_assertions.TestLongMessage) ... ok
testAssertNotRegex (unittest.test.test_assertions.Test_Assertions) ... ok
test_AlmostEqual (unittest.test.test_assertions.Test_Assertions) ... ok
test_AmostEqualWithDelta (unittest.test.test_assertions.Test_Assertions) ... ok
test_assertRaises (unittest.test.test_assertions.Test_Assertions) ... ok
test_assertRaises_frames_survival (unittest.test.test_assertions.Test_Assertions) ... ok
testHandlerReplacedButCalled (unittest.test.test_break.TestBreak) ... Traceback (most recent call last):
  File "/home/paulp/ws/ws3/km/payloads/python/cpython/Lib/runpy.py", line 193, in _run_module_as_main
    "__main__", mod_spec)
  File "/home/paulp/ws/ws3/km/payloads/python/cpython/Lib/runpy.py", line 85, in _run_code
    exec(code, run_globals)
  File "./cpython/Lib/unittest/test/__main__.py", line 18, in <module>
    unittest.main()
  File "/home/paulp/ws/ws3/km/payloads/python/cpython/Lib/unittest/main.py", line 101, in __init__
    self.runTests()
  File "/home/paulp/ws/ws3/km/payloads/python/cpython/Lib/unittest/main.py", line 271, in runTests
    self.result = testRunner.run(self.test)
```

This shows that test testHandlerReplacedButCalled is where the problem is.

* Once you know which test cause the problem you can run a single test like this:

./python  ./cpython/Lib/unittest/test/ -v -k testHandlerReplacedButCalled

* You can get a km trace for the test by setting the KM_VERBOSE environment variable.

```bash
export KM_VERBOSE=""
./python  ./cpython/Lib/unittest/test/ -v -k testHandlerReplacedButCalled
```

You should see this:

```txt
input ./python
Setting payload name to ./cpython/python.km
17:17:50.281460 km_fs_init           2223 python  lim.rlim_cur=727
17:17:50.281796 km_machine_setup     562  python  Trying to open device file /dev/kontain
17:17:50.281819 km_machine_setup     562  python  Trying to open device file /dev/kvm
17:17:50.281848 km_machine_setup     572  python  Using device file /dev/kvm
17:17:50.281857 km_machine_setup     579  python  Setting vm type to VM_TYPE_KVM
17:17:50.283001 km_machine_setup     643  python  Setting VendorId to 'Kontain'
17:17:50.283029 km_machine_setup     621  python  KVM: physical memory width 39
17:17:50.283261 km_add_vvar_vdso_to_ 275  python  [vvar]: km vaddr 0x7ffd4cf73000, payload paddr 0x7ffff00000, payload vaddr 0x8000000000
17:17:50.283275 km_add_vvar_vdso_to_ 275  python  [vdso]: km vaddr 0x7ffd4cf77000, payload paddr 0x7ffff04000, payload vaddr 0x8000004000
17:17:50.283280 km_monitor_pages_in_ 725  python  gva 0x8000000000, sizeof 16384, protection 0x1, tag [vvar]
17:17:50.283290 km_monitor_pages_in_ 725  python  gva 0x8000004000, sizeof 8192, protection 0x4, tag [vdso]
17:17:50.283317 km_monitor_pages_in_ 725  python  gva 0x8000008000, sizeof 4096, protection 0x4, tag [km_guest_text]
17:17:50.283324 km_monitor_pages_in_ 725  python  gva 0x8000009000, sizeof 24576, protection 0x1, tag [km_guest_data]
17:17:50.283331 km_guest_mmap_impl   658  python  0x0, 0x1000, prot 0x3 flags 0x22 fd -1 alloc 0x1
17:17:50.283373 fixup_top_page_table 543  python  grow old:0x7fffffe00000(511,511) new:0x7fffffdff000(510,511)
17:17:50.283400 km_reg_make_clean    187  python  zero km 0x7fffffdff000 sz 0x1000
17:17:50.283407 km_guest_mmap_impl   673  python  == mmap guest ret=0x7fffffdff000
17:17:50.283431 km_guest_mmap_impl   658  python  0x0, 0x1000, prot 0x3 flags 0x22 fd -1 alloc 0x1

and a lot more output not shown here
```
