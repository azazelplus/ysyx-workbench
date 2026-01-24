/* Abstract Machine linker script for RISC-V/MiniRV */

/* Set program entry point */
ENTRY(_start)

/* Define program segments for ELF output */
PHDRS { 
  text PT_LOAD;    /* Loadable text segment (code) */
  data PT_LOAD;    /* Loadable data segment */
}

SECTIONS {
  /* _pmem_start and _entry_offset are defined by compiler LDFLAGS */
  . = _pmem_start + _entry_offset;  /* Set output address to memory start + entry offset */
  
  /* Code section */
  .text : {
    *(entry)      /* Link entry code first */
    *(.text*)     /* Link all .text* sections from input files */
  } : text        /* Assign to text segment */
  etext = .;      /* Mark end of text section */
  _etext = .;
  
  /* Read-only data section (constants, string literals, etc) */
  .rodata : {
    *(.rodata*)   /* Link all read-only data sections */
  }
  
  /* Initialized data section (global variables with initializers) */
  .data : {
    *(.data*)     /* Link all .data* sections */
  } : data        /* Assign to data segment */
  edata = .;      /* Mark end of data section */
  _data = .;
  
  /* Uninitialized data section (BSS - Block Started by Symbol) */
  .bss : {
	_bss_start = .;  /* Mark start of BSS for zero-initialization */
    *(.bss*)       /* Link all .bss* sections (uninitialized globals) */
    *(.sbss*)      /* Small BSS sections */
    *(.scommon)    /* Common symbols */
  }
  
  /* Stack layout: reserve 32KB (0x8000) stack space */
  _stack_top = ALIGN(0x1000);       /* Align stack top to 4KB boundary */
  . = _stack_top + 0x8000;          /* Reserve 32KB for stack growth */
  _stack_pointer = .;               /* Initial stack pointer (top of stack) */
  
  /* Heap layout: starts after BSS, aligned to 4KB */
  end = .;                          /* End of program */
  _end = .;
  _heap_start = ALIGN(0x1000);      /* Heap starts here, aligned to 4KB */
}
