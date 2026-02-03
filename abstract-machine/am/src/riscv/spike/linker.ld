ENTRY(_start)

SECTIONS {
  . = 0x80000000;

  .text : {
    *(entry)    /*the first `*` matches all .o files participating linking. the `entry` matches all the section named `entry` in each .o file that the first `*` matches.  BUT: usually, there is only one section named `entry` in all .o files.  IN THIS CASE, is in "start.S."*/
    *(.text*)   /* the 2nd `*` matches all sections looks like `.test*` in each .o file that the first `*` matches.*/
  }

  etext = .;
  _etext = .;

  .rodata : {
    *(.rodata*)
  }

/*this section named .htif. Meaning: Host-Target Interface, used for communication between the host and the target.*/
  .htif : {
    PROVIDE(__htif_base = . );  /*PROVIDE key word: define a symbol if it is not defined yet. Actually, it generates a weak symbol.*/
    *(.htif)  /*find all sections named .htif in each .o file, and put them here.*/
  }

  .data : {
    *(.data)
  }

  edata = .;  /*end of data section.*/
  _data = .;

  .bss : {
	_bss_start = .;
    *(.bss*)
    *(.sbss*)
    *(.scommon)
  }
  
  _stack_top = ALIGN(0x1000);  /*ALIGN(0X1000):  return  `.`'s next multiple of 0x1000. doesnot change `.`.  and here we got the stack top(the lowest addr in stack space).*/
  . = _stack_top + 0x8000;  /* move `.` forward by 32KB. the current `.` is the stack's bottom, the highest addr in stack space.*/
  _stack_pointer = .;
  end = .;
  _end = .;   /*all the code and static data is end here.*/
  _heap_start = ALIGN(0x1000);  /*start with the next multiple of 4KB, up above is all empty. and here is where heap space starts.*/
}
