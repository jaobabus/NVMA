
from dataclasses import dataclass, field

import regex

from memory import MemoryRegion, MemoryFragment, MemoryOffset
from instruction import Instructions, InstructionDesc, BuilderDescImpl, ArgCathegory

all_regex=regex.compile(r"^[ \t]*(?<label>\w+)\:$|^[ \t]*(?<op>\w+)(?:[ \t]+(?&arg)(?:[ \t]*,[ \t]*(?<arg>[0-9][0-9xA-Fa-f]*|\w+))*)?$|^[ \t]*\.(?<section>\w+)$", regex.M | regex.I)


@dataclass()
class NanoVMMemoryObject:
    ram: MemoryRegion = field(default_factory=lambda: MemoryRegion('ram', 0, [], 128, 128))
    text: MemoryRegion = field(default_factory=lambda: MemoryRegion('text', 0, [], 256, 256))


class LazyInstruction(MemoryRegion):
    def __init__(self, compiler: "NanoVMAsmParser", inst: InstructionDesc, args: list[str]):
        super().__init__(f"LazyOp[{inst.opcode}]", None, [], inst.length, None)
        self.compiler = compiler
        self.inst = inst
        self.args = args

    def eval_position(self, base: int) -> int:
        self.position = base
        return base + self.inst.length

    def eval_size(self) -> int:
        return self.inst.length

    def get_data(self) -> bytes:
        int_args = {}
        for name, arg in zip(self.inst.args, self.args):
            if arg == '.':
                int_args[name] = self.position // 4
            else:
                frag = self.compiler.resolve_label(arg)
                if frag.position is None:
                    raise RuntimeError(f"Var {frag.name} not evaluated")
                if self.inst.args_cathegories.get(name) == ArgCathegory.Register:
                    int_args[name] = frag.position // 4
                else:
                    int_args[name] = frag.position
        reg = self.inst.encode(None, int_args)
        reg.eval_position(self.position)
        return reg.get_data()


class NanoVMAsmParser:
    def __init__(self):
        self._sections: dict[str, MemoryRegion] | None = None
        self._section: MemoryRegion | None = None
        self._memory: NanoVMMemoryObject | None = None
        self._labels: dict[str, MemoryFragment] | None = None
        self.last_error_line = None

    def _make_section(self, region: MemoryRegion, mem: MemoryRegion):
        self._sections[region.name] = region
        self._labels[region.name] = region
        mem.fragments.append(region)

    def init(self):
        self._sections = {}
        self._labels = {}
        self._memory = NanoVMMemoryObject()
        self._make_section(MemoryRegion('lr', 0, [MemoryOffset('LR', 0, 0), MemoryOffset('lr', 0, 4)], 4, 4), self._memory.ram)
        self._make_section(MemoryRegion('code', None, [], 256, 256), self._memory.text)
        self._make_section(MemoryRegion('input', None, [], 256, 256), self._memory.ram)
        self._make_section(MemoryRegion('output', None, [], 256, 256), self._memory.ram)
        self._make_section(MemoryRegion('data', None, [], 256, 256), self._memory.ram)
        self._select_section('code')

    def _make_label(self, name: str, size: int = 0):
        label = MemoryOffset(name, None, size)
        self._labels[name] = label
        self._section.fragments.append(label)

    def _select_section(self, name: str):
        self._section = self._sections[name]

    def _add_instruction(self, instruction: MemoryFragment):
        self._section.fragments.append(instruction)

    def _process_instruction(self, name: str, args: list[str]):
        name = name.upper()
        if name in ('MEMORY', ):
            self._make_label(args[1] if len(args) > 1 else '', int(args[0]))
        else:
            if name not in Instructions.all_instructions:
                raise RuntimeError(f"Instruction {name} not found")
            inst = Instructions.all_instructions[name]
            if len(args) != len(inst.args):
                raise RuntimeError(f"Args of {name} length not match")
            if any(not arg[0].isdigit() for arg in args):
                lazy_inst = LazyInstruction(self, inst, args)
                self._section.fragments.append(lazy_inst)
            else:
                int_args = {name: eval(arg) for name, arg in zip(inst.args, args)}
                self._section.fragments.append(inst.encode(None, int_args))

    def _process_line(self, line: str):
        m = all_regex.match(line)
        if not m:
            raise RuntimeError(f"Error parse line '{line}'")
        cd = m.capturesdict()
        if cd['label']:
            self._make_label(cd['label'][0])
        elif cd['op']:
            self._process_instruction(cd['op'][0], cd['arg'])
        elif cd['section']:
            self._select_section(cd['section'][0])
        else:
            raise RuntimeError("???")

    def resolve_label(self, name: str) -> MemoryFragment:
        if name not in self._labels:
            raise RuntimeError(f"Label {name} not found")
        return self._labels[name]

    def process_file(self, file: str):
        for i, line in enumerate(file.split('\n')):
            line = line.strip()
            line = line.split(';', 1)[0].strip()
            if line == '':
                continue
            try:
                self._process_line(line)
            except RuntimeError:
                self.last_error_line = i
                raise

    def get_memory(self) -> NanoVMMemoryObject:
        return self._memory
