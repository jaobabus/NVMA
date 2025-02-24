# NanoVM Debugger and Compiler

NanoVM is a compact virtual machine designed for efficient execution of low-level assembly-like code. This repository includes a debugger, compiler, and decompiler for the NanoVM architecture.

## Features
- **NanoVM Debugger**: Step through execution, inspect memory, and track changes.
- **NanoVM Compiler**: Convert human-readable assembly code into NanoVM bytecode.
- **NanoVM Decompiler**: Reverse engineer compiled NanoVM binaries back to assembly.
- **Custom Memory Management**: Define memory regions and track variable changes during execution.
- **Breakpoint Support**: Set and hit breakpoints for controlled debugging.

## Installation
### Dependencies
Ensure you have the following dependencies installed:
- `C++17+`
- `nlohmann_json` (for JSON-based memory initialization)
- `fusepy` (for FUSE-based compilation service)

### Building the Project
```sh
mkdir build && cd build
cmake ..
cmake --build .
```

## Usage
### Running the Debugger
```sh
./dbg -i <source_file>:<input_sections>[:<var>=<value>]*
```

### Debugging Commands
| Command   | Description |
|-----------|------------|
| `n` or `step` | Execute the next instruction |
| `c` or `continue` | Continue execution until breakpoint |
| `b <addr>` | Set a breakpoint at given address |
| `p <var>` | Print variable/memory content |
| `p <var>=<value>` | Modify variable/memory content |
| `l` or `list` | List instructions around the current PC |
| `q` or `exit` | Quit debugger |

### Compiling Assembly Code
```sh
cd build
./compile -i <source code>
```

### Decompiling Bytecode
```sh
cd build
./compile -b <binary sections>
```

## Example: Factorial Calculation
```assembly
.input
MEMORY 4, n

.output
MEMORY 4, result

.data
MEMORY 4, zero
MEMORY 4, one
MEMORY 4, counter
MEMORY 4, accum
MEMORY 4, tmp1
MEMORY 4, tmp2
MEMORY 4, return

.code
init:
    LOAD3 0
    STORE_OP zero
    LOAD3 1
    STORE_OP one
    JZ lr, factorial

multiply: ; lr (tmp1 a, tmp2 b)
    ; lr = a
    ; a = 0
    ; while (lr > 0) {
    ;    a += b
    ;    lr--
    ; }
    ; return a

    LOAD_OP tmp1
    AND tmp1, tmp1, zero

    multipy_loop:
    JZ zero, multiply_end
        ADD tmp1, tmp1, tmp2
        SUB lr, lr, one
        JZ lr, multipy_loop
    multiply_end:
    LOAD_OP tmp1
    PC_SWP return, return

factorial: ; result (n)
    ; result = 1
    ; counter = n
    ; while (counter > 0) {
    ;    result *= counter
    ; }

    MOV result, one
    MOV counter, n

    loop:
    LOAD3 0
    JZ counter, end
        MOV tmp1, counter
        MOV tmp2, result
        LOAD_LOW multiply
        PC_SWP return, lr
        STORE_OP result

        SUB counter, counter, one
        JZ lr, loop

end:
HALT
```

## License
MIT License

