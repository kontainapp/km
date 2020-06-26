.text
.global __clone
.hidden __clone
.type   __clone,@function
__clone:
   and $-16, %rsi    # rounddown(stack, 16)
   mov 8(%rsp), %r10 # ctid, 7th arg from stack
   # fn in %rdi, args in %rcx
   # km_hc_args_t structure
   sub $8, %rsp # arg6
   push %r9     # newtls
   push %r10    # ctid
   push %r8     # ptid
   push %rsi    # stack
   push %rdx    # flags
   sub $8, %rsp # hc_ret
   # clone hc
   mov $0x8038, %edx  # KM_HCALL_PORT_BASE + SYS_clone
   mov %rsp, %rax       # rax is the address of the km_hc_args_t on the stack
   outl %eax, (%dx)
   mov (%rsp), %eax # Get return code into %rax
   add $56, %rsp
   test %eax,%eax
   jnz 1f         # parent jumps
   # child - on a new stack now
   mov %rdi, %r9  # fn
   mov %rcx, %rdi # args
   call *%r9
   # fn returns, exit now
   sub $56, %rsp  # setup an empty km_hc_args_t on the stack
   mov %rsp, %rax   # rax is the address of km_hc_args_t 
   mov $0x803c, %edx # KM_HCALL_PORT_BASE + SYS_exit
   outl %eax, (%dx)
   hlt
1:
   ret
