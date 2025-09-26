MEMORY {
  SCRATCHPAD  (wa) : ORIGIN = 0x00000000, LENGTH = 64K
  MAIN_MEM    (wa) : ORIGIN = 0x08000000, LENGTH = 64M  
  INSTRUCTION (rx) : ORIGIN = 0x80000000, LENGTH = 64K
}

SECTIONS {
  /* Boot code must be first in instruction memory */
  .boot : {
    KEEP(*(.boot))
  } > INSTRUCTION

  /* Program code */
  .text : {
    *(.text)
      *(.text.*)
      . = ALIGN(4);
  } > INSTRUCTION

  .sbss (NOLOAD) : {
    __bss_start = .;
    *(.sbss .sbss.*)
    *(.sbss2 .sbss2.*)
    *(COMMON)
  } > SCRATCHPAD
  .bss (NOLOAD) : {
    *(.bss .bss.*)
    *(COMMON)
    /* Reserve space for per-thread stacks */
    . = ALIGN(16);
    __sys_stacks = .;
    . += 1024 * NR_TASKLETS;  /* 1024 bytes per thread stack */
    __sys_stacks_end = .;
    __bss_end = .;
  } > SCRATCHPAD

  /* Initialized data in scratchpad */
  .sdata : {
    *(.sdata .sdata.*)
    *(.sdata2 .sdata2.*)
      . = ALIGN(4);
  } > SCRATCHPAD
  .data : {
    *(.data .data.*)
    *(.rodata .rodata.*)
      . = ALIGN(4);
    __sys_used_scratchpad_end = .;
  } > SCRATCHPAD

  /* Main memory uninitialized data */
  .mram.noinit (NOLOAD) : {
    *(.mram.noinit .mram.noinit.*)
  } > MAIN_MEM

  /* Main memory initialized data */
  .mram : {
    *(.mram .mram.*)
    . = ALIGN(8);
    __sys_used_mram_end = .;
  } > MAIN_MEM

  /* Stack grows downward from end of scratchpad */
  __stack_limit = __sys_used_scratchpad_end;
  __stack_top = ORIGIN(SCRATCHPAD) + LENGTH(SCRATCHPAD);
  /* Ensure we don't overflow memory regions */
  ASSERT(__sys_used_scratchpad_end <= ORIGIN(SCRATCHPAD) + LENGTH(SCRATCHPAD), 
  "Scratchpad memory overflow")
  ASSERT(__sys_used_mram_end <= ORIGIN(MAIN_MEM) + LENGTH(MAIN_MEM), 
  "Main memory overflow")
}

/* Entry point is the bootstrap function */
ENTRY(bootstrap)
