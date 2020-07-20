.text
.global __clone
.hidden __clone
.type   __clone,@function
__clone:
   and $-16, %rsi    # rounddown(stack, 16)
   mov 8(%rsp), %r10 # ctid, 7th arg from stack
   # fn in %rdi, args in %rcx

   # fill in this threads hc_args structure
   mov %r9,%gs:40     # newtls, arg5
   mov %r10,%gs:32    # ctid,  arg4
   mov %r8,%gs:24     # ptid,  arg3
   mov %rsi,%gs:16    # stack, arg2
   mov %rdx,%gs:8     # flags, arg1

   # clone hc
   mov $0x8038, %edx  # KM_HCALL_PORT_BASE | SYS_clone
   mov %gs, %rax
   outl %eax, (%dx)
   mov %gs:0, %eax    # Get hc_ret into %rax

   test %eax,%eax
   jnz 1f             # parent jumps

   # child - on a new stack now
   mov %rdi, %r9  # fn
   mov %rcx, %rdi # args
   call *%r9
   # fn returns, exit now

   mov %rax, %gs:8  # return value of fn to arg1 of SYS_exit
   mov %gs, %rax
   mov $0x803c, %edx # KM_HCALL_PORT_BASE | SYS_exit
   outl %eax, (%dx)
   hlt
1:
   ret
