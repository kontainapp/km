To use gdb in conjunction with km payloads and their child processes you need to be aware of these rules:  
 - a child payload inherits km command line debug settings from the parent km, so if gdbstub is running in the parent km, it will be running in the child km,  
 - to stop a child payload after a fork() call returns, set the parent km's environment variable KM_GDB_CHILD_FORK_WAIT to the name of km payload that called fork() (or clone()), the value is a regular expression,  
 - to gain control in the child payload after an execve() completes use the gdb "catch exec" command brefore the execve() call is made by the child payload  

The following is an example of debugging a simple program that forks and execs to another simple program.
The goal of the example is to set a breakpoint in the child payload and do some debugging.
Keep in mind that we have 3 terminal sessions in this example.  One session where we start the payload and view the output of the payloads.
Another session where we can debug the parent payload.
And a last session where we can debug the child payload.

Parent program gdb_forker_test.c:

```c
int main(int argc, char* argv[])
{
   char payload[] = "hello_test";
   pid_t pid;

   pid = fork();
   if (pid < 0) {
      fprintf(stderr, "fork() in %s failed, %s\n", argv[0], strerror(errno));
      return 1;
   } else if (pid == 0) {
      char* new_argv[2];
      new_argv[0] = payload;
      new_argv[1] = NULL;
      char* new_envp[1];
      new_envp[0] = NULL;
      fprintf(stderr, "Child pid %d exec()'ing to %s\n", getpid(), payload);
      execve(payload, new_argv, new_envp);
      fprintf(stderr, "execve() to %s, pid %d, failed %s\n", payload, getpid(), strerror(errno));
      return 1;
   } else { // parent
      fprintf(stderr, "Waiting for child pid %d to terminate\n", pid);
      pid_t waited_pid;
      int status;
      waited_pid = waitpid(pid, &status, 0);
      assert(waited_pid == pid);
      fprintf(stdout, "Child pid %d terminated with status %d (0x%x)\n", pid, status, status);
   }
   return 0;
}
```


Child program hello_test.c:

```c
static const char msg[] = "Hello,";

int main(int argc, char** argv)
{
   char* msg2 = "world";

   printf("%s %s\n", msg, msg2);
   for (int i = 0; i < argc; i++) {
      printf("%s argv[%d] = '%s'\n", msg, i, argv[i]);
   }
   exit(0);
}
```


Start the parent program, attach gdb and let the payload continue:

```
[paulp@work tests]$ export KM_GDB_CHILD_FORK_WAIT=".*gdb_forker.*"
[paulp@work tests]$ env | grep KM
[paulp@work tests]$ ../build/km/km -g  gdb_forker_test.km
19:05:35.707719 km_gdb_attach_messag 319  km      Waiting for a debugger. Connect to it like this:
	gdb -q --ex="target remote work:2159" /home/paulp/ws/ws2/km/tests/gdb_forker_test.km
GdbServerStubStarted
```


Start an instance of gdb that attaches to gdb_forker_test.km

```
[paulp@work km]$ gdb -q --ex="target remote work:2159"
Remote debugging using work:2159
Reading /home/paulp/ws/ws2/km/tests/gdb_forker_test.km from remote target...
warning: File transfers from remote targets can be slow. Use "set sysroot" to access files locally instead.
Reading /home/paulp/ws/ws2/km/tests/gdb_forker_test.km from remote target...
Reading symbols from target:/home/paulp/ws/ws2/km/tests/gdb_forker_test.km...
0x0000000000201116 in _start ()
(gdb) c
Continuing.
```



The parent program gdb_forker_test resumes, forks a child, and the child km wants another instance of gdb to attach to the child payload:

```
19:06:39.884242 km_gdb_accept_connec 255  km      Connection from debugger at 10.1.10.47
19:07:08.481122 km_gdb_attach_messag 319  1001.km      Waiting for a debugger. Connect to it like this:
	gdb -q --ex="target remote work:2160" /home/paulp/ws/ws2/km/tests/gdb_forker_test.km
GdbServerStubStarted

Waiting for child pid 1001 to terminate
```



Now the child instance of gdb_forker_test is waiting for another instance of gdb to attach to it before proceeding.
So we attach and find gdb is waiting after the fork() call in the child returns:

```
[paulp@work km]$ gdb -q --ex="target remote work:2160"
Remote debugging using work:2160
Reading /home/paulp/ws/ws2/km/tests/gdb_forker_test.km from remote target...
warning: File transfers from remote targets can be slow. Use "set sysroot" to access files locally instead.
Reading /home/paulp/ws/ws2/km/tests/gdb_forker_test.km from remote target...
Reading symbols from target:/home/paulp/ws/ws2/km/tests/gdb_forker_test.km...
__syscall0 (n=57) at ./syscall_arch.h:21
21	   return arg.hc_ret;
(gdb) bt
#0  __syscall0 (n=57) at ./syscall_arch.h:21
#1  fork () at musl/src/process/fork.c:21
#2  0x0000000000201262 in main (argc=1, argv=0x7fffffdfc838) at gdb_forker_test.c:38
(gdb) 
```


Then we tell gdb we want to gain control when the execve() call in the gdb_forker_test child completes:

```
gdb) catch exec
Catchpoint 1 (exec)
(gdb) c
Continuing.
Remote target is executing new program: /home/paulp/ws/ws2/km/tests/hello_test.km
Reading /home/paulp/ws/ws2/km/tests/hello_test.km from remote target...
Reading /home/paulp/ws/ws2/km/tests/hello_test.km from remote target...

Catchpoint 1 (exec'd /home/paulp/ws/ws2/km/tests/hello_test.km), 0x0000000000201032 in _start ()
(gdb)
```



Next set a breakpoint in hello_test, continue execution, the breakpoint fires, we look at local variables, and let the hello_test program finish:

```
(gdb) br printf
Breakpoint 2 at 0x201435: file musl/src/stdio/printf.c, line 5.
(gdb) c
Continuing.

Breakpoint 2, printf (fmt=0x20400d "%s %s\n") at musl/src/stdio/printf.c:5
5	{
(gdb) bt
#0  printf (fmt=0x20400d "%s %s\n") at musl/src/stdio/printf.c:5
#1  0x000000000020118e in main (argc=1, argv=0x7fffffdfdcf8) at hello_test.c:23
(gdb) f 1
#1  0x000000000020118e in main (argc=1, argv=0x7fffffdfdcf8) at hello_test.c:23
23	   printf("%s %s\n", msg, msg2);
(gdb) p msg
$1 = "Hello,"
(gdb) p msg2
$2 = 0x204007 "world"
(gdb) del br 2
(gdb) cont
Continuing.
[Inferior 1 (Remote target) exited normally]
(gdb)
```



Back to the output from the gdb_forker_test and hello_test programs, which shows:
- the hello_test output,
- km messages showing both instances of gdb detached,
- and the output from gdb_forker_test when the child process finishes

```
Hello, world
Hello, argv[0] = '/home/paulp/ws/ws2/km/tests/hello_test.km'
19:28:02.483754 km_gdb_detach        402  1001.km      gdb client disconnected
Child pid 1001 terminated with status 0 (0x0)
19:28:02.487561 km_gdb_detach        402     1.km      gdb client disconnected
[paulp@work tests]$ 
```


And, for completeness, the gdb_forker_test debug session finishes up:

```
[Inferior 1 (Remote target) exited normally]
(gdb)
```
