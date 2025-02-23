
#include "vmop.hpp"




bool execute_one(uint32_t* ram,
                 const uint8_t* code,
                 uint8_t& pc,
                 uint32_t (*proc)(uint32_t proc_id, uint32_t arg))
{
    static const uint8_t opcodes_sizes[] = {1, 1, 2, 2, 2, 2, 2, 1};
    uint8_t header = code[pc];
    uint8_t opcode = (header >> 5);
    uint32_t long_arg = ((uint32_t)(header & 0x1F) << 16) | ((uint16_t)code[pc + 1] << 8) | (code[pc + 2]);
    pc += opcodes_sizes[opcode];

    if (opcode == LoadOp)
    {
        ram[0] = ram[header & 0x1F];
    }
    else if (opcode == StoreOp)
    {
        ram[header & 0x1F] = ram[0];
    }
    else if (opcode == Jump)
    {
        uint8_t next_pc = (long_arg >> 8) & 0xFF;
        if (header & 0x10) {
            if (ram[0] < ram[header & 0xF])
                pc = next_pc;
        }
        else {
            if (ram[0] == ram[header & 0xF])
                pc = next_pc;
        }
    }
    else if (opcode == Load1)
    {
        uint32_t lr = ram[0];
        if (header & 0x10) {
            auto arg = long_arg << 12;
            lr = (lr & 0xFFF) | arg;
            pc++;
        }
        else {
            lr = (long_arg >> 8) & 0xFFF;
        }
        ram[0] = lr;
    }
    else if (opcode <= Shift)
    {
        long_arg >>= 8;
        uint32_t arg = long_arg & 0xF;
        uint32_t mem2 = ram[arg];
        long_arg >>= 4;
        uint32_t mem1 = ram[long_arg & 0xF];
        long_arg >>= 4;
        switch (opcode) {
        case AddSub:
            if (long_arg & 0x10)
                mem1 = mem1 - mem2;
            else
                mem1 = mem1 + mem2;
            break;
        case AndOr:
            if (long_arg & 0x10)
                mem1 = mem1 | mem2;
            else
                mem1 = mem1 & mem2;
            break;
        case Shift:
            if (long_arg & 0x10)
                mem1 = mem1 >> arg;
            else
                mem1 = mem1 << arg;
            break;
        }
        ram[long_arg & 0xF] = mem1;
    }
    else // opcode == Extra
    {
        if (header & 0x10) {
            if (header & 0x08) {
                if (header & 0x04) // HALT or unknown
                    return false;
                // PC_SWP
                long_arg >>= 8;
                auto& prev = ram[long_arg & 0xF];
                long_arg >>= 5;
                auto new_pc = ram[long_arg & 0xF];
                prev = ++pc;
                pc = new_pc;
            }
            else
                ram[0] = header & 0x7;
        }
        else { // CALL
            uint32_t arg = ram[long_arg & 0xF];
            long_arg >>= 4;
            uint32_t callback = ram[long_arg & 0xF];
            long_arg >>= 4;
            if (proc)
                arg = proc(callback, arg);
            ram[long_arg & 0xF] = arg;
            pc++;
        }
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
