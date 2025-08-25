.section .boot, "ax"
.global bootstrap

_start:
    # Get thread ID
    myid s0
    
    # Calculate stack top: __sys_stacks + 1024 * (thread_id + 1)
    addi a0, s0, 1          # a0 = thread_id + 1
    slli a0, a0, 10         # a0 = (thread_id + 1) * 1024
    la a1, __sys_stacks     # Load base address of stacks
    add a0, a0, a1          # a0 = __sys_stacks + offset
    
    # Align to 16-byte boundary (RISC-V ABI requirement)
    andi a0, a0, -16        # a0 &= ~0xF
    mv sp, a0               # Set stack pointer
    
    # Call main function
    call main
    
    # Halt this thread when main returns
    li a0, 1                # a0 = 1
    sll a0, a0, s0          # a0 = 1 << thread_id
    csrc 0x0, a0            # Clear thread bit in CSR0 (halt)
    
    # Infinite loop with ebreak
1:
    ebreak
    j 1b
