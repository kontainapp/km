# Interrupt and Exception Handling in the Guest

If a guest does anything that results in an exception or a trap and there is no Interrupt Descriptor Table defined, KVM generates a KVM_EXIT_SHUTDOWN and the guest is unusable from then on. This behavior is at odd with our requirements for signal handling in the guest since things like stray memory references (SIGSEGV), divide-by-zero (SIGFPE) and executing undefined istructions (SIGILL) need to generate signals.

Because of this, the guest needs to have interrupt handlers in order to catch these events and pass them on to KM.

## Concept

KM guest runtime will define a simple interrupt and exception handling function that simply makes a hypercall into the monitor.
The KVM API provides mechanisms that allow the monitor to determine what interrupt was triggered (KVM_GET_VCPU_EVENTS) and the values of the processor written stack variables are (KVM_GET_REGS), so a single guest interrupt function suffices.

All of the intelligence for dealing with the interrupt occurs in the monitor.

Interrupts translate into signals for the guest process.