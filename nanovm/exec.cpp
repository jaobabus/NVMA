
#include "vmop.hpp"




enum InstructionOpcode {
    LoadOp  = 0,
    StoreOp = 1,
    Jump    = 2,
    Load1   = 3,
    AddSub  = 4,
    AndOr   = 5,
    Shift   = 6,
    Extra   = 7,
};

/*
Команды:
```
> LLR   M       - 0 0 0 M  M M M M  - - - -  - - - -
> SLR   M       - 0 0 1 M  M M M M  - - - -  - - - -
> JL    R, A    - 0 1 0 0  R R R R  A A A A  A A A A
> JZ    R, A    - 0 1 0 1  R R R R  A A A A  A A A A
> LLI   V       - 0 1 1 0  V V V V  V V V V  V V V V
> LHI   V       - 0 1 1 1  V V V V  V V V V  V V V V  V V V V V V V V
> ADD   S, L, R - 1 0 0 0  S S S S  L L L L  R R R R
> SUB   S, L, R - 1 0 0 1  S S S S  L L L L  R R R R
> AND   S, L, R - 1 0 1 0  S S S S  L L L L  R R R R
> OR    S, L, R - 1 0 1 1  S S S S  L L L L  R R R R
> LSL   S, L, R - 1 1 0 1  S S S S  L L L L  R R R R
> LSR   S, L, R - 1 1 0 1  S S S S  L L L L  R R R R
> CALL  C, R, A - 1 1 1 0  R R R R  C C C C  A A A A
> PCSWP M, S    - 1 1 1 1  1 0 M M  M M M S  S S S S
> HALT          - 1 1 1 1  1 1 1 1  - - - -  - - - -
> LOAD3 V       - 1 1 1 1  0 V V V  - - - -  - - - -
```
*/

bool execute_one(uint32_t* ram,
                 const uint8_t* code,
                 uint8_t& pc,
                 uint32_t (*proc)(uint32_t proc_id, uint32_t arg))
{
    static const uint8_t opcodes_sizes[] = {1, 1, 2, 2, 2, 2, 2, 1};
    uint8_t header = code[pc];
    uint8_t opcode = (header >> 5);
    uint8_t harg5 = header & 0x1F;
    pc += opcodes_sizes[opcode];

    if (opcode <= StoreOp)
    {
        if (opcode == Load1) {
            ram[0] = ram[harg5];
        }
        else {
            ram[harg5] = ram[0];
        }
    }
    else if (opcode == Load1)
    {
        auto lr = ram[0];
        auto low12 = ((uint16_t)harg5 << 8) | code[pc - 1];
        if (harg5 & 0x10) {
            auto arg = (low12 << 8) | code[pc];
            lr = (lr & 0xFFF) | (arg << 12);
            pc++;
        }
        else {
            lr = low12;
        }
        ram[0] = lr;
    }
    else if (opcode < Extra or (header & 0xF0) == 0xE0)
    {
        uint8_t pair2 = code[pc - 1];
        auto arg1 = ram[(pair2 >> 4)];
        auto arg2 = ram[pair2 & 0xF];

        switch (opcode)
        {
        case Jump:
            if (harg5 & 0x10) {
                if (ram[0] < ram[harg5 & 0xF])
                    pc = pair2;
            }
            else {
                if (ram[0] == ram[harg5 & 0xF])
                    pc = pair2;
            }
            return true;

        case AddSub:
            if (harg5 & 0x10)
                arg1 = arg1 - arg2;
            else
                arg1 = arg1 + arg2;
            break;

        case AndOr:
            if (harg5 & 0x10)
                arg1 = arg1 | arg2;
            else
                arg1 = arg1 & arg2;
            break;

        case Shift:
            if (harg5 & 0x10)
                arg1 = arg1 >> (pair2 & 0xF);
            else
                arg1 = arg1 << (pair2 & 0xF);
            break;

        default:
            if (proc)
                arg1 = proc(arg1, arg2);
        }

        ram[harg5 & 0xF] = arg1;
    }
    else {
        if (harg5 & 0x08) {
            if (harg5 & 0x04) // HALT or unknown
                return false;

            // PC_SWP
            auto arg2 = ((harg5 & 0x3) << 8) | code[pc];

            auto new_pc = ram[arg2 & 0x1F];
            ram[arg2 >> 5] = pc + 2;
            pc = new_pc;
        }
        else
            ram[0] = harg5 & 0x7;
    }

    return true;
}


void execute(uint32_t* ram,
             const void* text,
             uint8_t start,
             uint32_t (*proc)(uint32_t proc_id, uint32_t arg))
{
    uint8_t pc = start;
    const uint8_t* code = reinterpret_cast<const uint8_t*>(text);
    while (execute_one(ram, code, pc, proc)) {}
}
