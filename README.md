# mcc1

`mcc1`, the ~~mediocre~~mini-c compiler. I wrote this in early 2025 with the goal
of learning more about C and getting a bit more familiar with its specification.

It's far from complete, but it can be used to compile some toy programs, and it
should support calling external real world C functions.

The code is very badly documented, sorry. I was basically throwing stuff at the wall
to see what sticks. There are many design flaws, particularly in regards of memory
management (mostly arround parameter declarations and derived types) and error reporting.

## Building

Assuming you have `gcc`, `bison`, and gnu `sed` installed, simply running `make`
should do the trick. The compiler can be tested using `make test`.
The compiler emits x86 assembly so you will need an assembler
to make it do anything useful.
