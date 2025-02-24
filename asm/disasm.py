import typing

from instruction import Instructions, BuilderDescImpl, ArgCathegory
from compiler import NanoVMMemoryObject
from memory import MemoryRegion, find_frag, MemoryFragment, MemoryData, MemoryOffset, UnknownSourcePos


class Disassembler:
    def __init__(self, memory_object: NanoVMMemoryObject):
        self.memory = memory_object
        self.all_labels: dict[str, MemoryFragment] = {
            'lr': MemoryOffset("lr", 0, UnknownSourcePos, 4),
            **{f.name: f for f in typing.cast(MemoryRegion, find_frag(self.memory.ram, 'input')).fragments},
            **{f.name: f for f in typing.cast(MemoryRegion, find_frag(self.memory.ram, 'data')).fragments},
            **{f.name: f for f in typing.cast(MemoryRegion, find_frag(self.memory.ram, 'output')).fragments},
        }

    def disassemble(self) -> str:
        data = bytes(self.memory.text.get_data())
        orig = len(data)
        instructions = []
        while data:
            opcode = data[0] >> 5
            reasons = {}

            c = False
            for key, inst in Instructions.all_instructions.items():
                c, r = inst.check(data)
                if c:
                    args = inst.decode(data)
                    formatted_args, sects = self.replace_labels(args, inst)
                    str_data = ''.join(f"{a:02x}" for a in inst.encode(None, args).get_data())
                    str_data = str_data + ' ' * (8 - len(str_data))
                    instructions.append(f"{orig - len(data): >2x}:  {str_data}  {key} {', '.join(formatted_args)} $$; {', '.join(sects)}\n")
                    data = data[len(inst.encode(None, args).get_data()):]
                    break

                if opcode == inst.opcode:
                    reasons[key] = r

            if c:
                continue

            reason = f"Unknown opcode {opcode}" if not reasons else ', '.join(f"{k}: {v}" for k, v in reasons.items())
            raise RuntimeError(f"Instruction not found at {orig - len(data)} ({data[0]:02X}), reasons: {reason}")

        max_l = max([ins.find('$$') for ins in instructions])
        for i, ins in enumerate(instructions):
            instructions[i] = ins.replace('$$', ' ' * (max_l - ins.find('$$')))
        return ''.join(instructions)

    def replace_labels(self, args, inst):
        formatted_args = []
        sects = set()
        for name in inst.args:
            value = args[name]
            label = self.find_label(value)
            if label and inst.args_cathegories.get(name) == ArgCathegory.Register:
                formatted_args.append(label.name)
                sects.add(f"{label.name}={label.position}:{label.eval_size()}")
            else:
                formatted_args.append(f"0x{value:X}")
        return formatted_args, sects

    def find_label(self, address):
        for fragment in self.all_labels.values():
            if fragment.position // 4 == address:
                return fragment
        return None


def main():
    with open("example.nvmb", "r") as f:
        lines = f.read().strip().split('\n')
        obj_data = {l.split(' ', 1)[0]: l.split(' ', 1)[1] for l in lines}
        binary_code = bytes([int(a, 16) for a in obj_data['text'].split(' ')])

    memory_obj = NanoVMMemoryObject()
    memory_obj.text.fragments = [MemoryRegion('code', 0, [MemoryData('~instructions', 0, binary_code)], 256, 256)]
    memory_obj.ram.fragments = [
        MemoryRegion('input', 0, [], 256, 256),
        MemoryRegion('output', 0, [], 256, 256),
        MemoryRegion('data', 0, [], 256, 256),
    ]
    disassembler = Disassembler(memory_obj)
    asm_code = disassembler.disassemble()
    print(asm_code)


if __name__ == "__main__":
    main()
