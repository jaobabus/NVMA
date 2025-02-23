
from dataclasses import dataclass, field

import regex

from memory import MemoryRegion, MemoryFragment, MemoryOffset, UnknownSourcePos, SourcePos
from instruction import Instructions, InstructionDesc, BuilderDescImpl, ArgCathegory

all_regex=regex.compile(r"^[ \t]*(?<label>\w+)\:$|^[ \t]*(?<op>\w+)(?:[ \t]+(?&arg)(?:[ \t]*,[ \t]*(?<arg>[0-9][0-9xA-Fa-f]*|\w+))*)?$|^[ \t]*\.(?<section>\w+)$", regex.M | regex.I)


@dataclass()
class NanoVMMemoryObject:
    ram: MemoryRegion = field(default_factory=lambda: MemoryRegion('ram', 0, UnknownSourcePos, [], 128, 128))
    text: MemoryRegion = field(default_factory=lambda: MemoryRegion('text', 0,UnknownSourcePos, [], 256, 256))


class LazyInstruction(MemoryRegion):
    def __init__(self, compiler: "NanoVMAsmParser", position: SourcePos, inst: InstructionDesc, args: list[str]):
        super().__init__(f"LazyOp[{inst.opcode}]", None, position, [], inst.length, None)
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
                int_args[name] = self.position
            elif arg[0].isdigit():
                if arg.startswith('0x') or arg.startswith('0X'):
                    int_args[name] = int(arg[2:], 16)
                else:
                    int_args[name] = int(arg)
            else:
                frag = self.compiler.resolve_label(arg)
                if frag.position is None:
                    raise RuntimeError(f"Var {frag.name} not evaluated")
                if self.inst.args_cathegories.get(name) == ArgCathegory.Register:
                    int_args[name] = frag.position // 4
                else:
                    int_args[name] = frag.position
        reg = self.inst.encode(self.source_pos, int_args)
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
        self._make_section(MemoryRegion('lr', 0, UnknownSourcePos, [MemoryOffset('LR', 0, UnknownSourcePos, 0), MemoryOffset('lr', 0, UnknownSourcePos, 4)], 4, 4), self._memory.ram)
        self._make_section(MemoryRegion('code', None, UnknownSourcePos, [], 256, 256), self._memory.text)
        self._make_section(MemoryRegion('input', None, UnknownSourcePos, [], 256, 256), self._memory.ram)
        self._make_section(MemoryRegion('output', None, UnknownSourcePos, [], 256, 256), self._memory.ram)
        self._make_section(MemoryRegion('data', None, UnknownSourcePos, [], 256, 256), self._memory.ram)
        self._select_section(UnknownSourcePos, 'code')

    def _make_label(self, source_pos: SourcePos, name: str, size: int = 0):
        label = MemoryOffset(name, None, source_pos, size)
        self._labels[name] = label
        self._section.fragments.append(label)

    def _select_section(self, source_pos: SourcePos, name: str):
        self._section = self._sections[name]

    def _add_instruction(self, instruction: MemoryFragment):
        self._section.fragments.append(instruction)

    def _process_instruction(self, source_pos: SourcePos, name: str, args: list[str]):
        name = name.upper()
        if name in ('MEMORY', ):
            self._make_label(source_pos, args[1] if len(args) > 1 else '', int(args[0]))
        else:
            if name not in Instructions.all_instructions:
                raise RuntimeError(f"Instruction {name} not found")
            inst = Instructions.all_instructions[name]
            if len(args) != len(inst.args):
                raise RuntimeError(f"Args of {name} length not match")
            if any(not arg[0].isdigit() for arg in args):
                lazy_inst = LazyInstruction(self, source_pos, inst, args)
                self._section.fragments.append(lazy_inst)
            else:
                int_args = {name: eval(arg) for name, arg in zip(inst.args, args)}
                self._section.fragments.append(inst.encode(source_pos, int_args))

    def _process_line(self, source_pos: SourcePos, line: str):
        m = all_regex.match(line)
        if not m:
            raise RuntimeError(f"Error parse line '{line}'")
        cd = m.capturesdict()
        if cd['label']:
            self._make_label(source_pos, cd['label'][0])
        elif cd['op']:
            self._process_instruction(source_pos, cd['op'][0], cd['arg'])
        elif cd['section']:
            self._select_section(source_pos, cd['section'][0])
        else:
            raise RuntimeError("???")

    def resolve_label(self, name: str) -> MemoryFragment:
        if name not in self._labels:
            raise RuntimeError(f"Label {name} not found")
        return self._labels[name]

    def process_file(self, file: str, filename: str):
        for i, line in enumerate(file.split('\n')):
            line = line.strip()
            line = line.split(';', 1)[0].strip()
            source_pos = SourcePos(filename, i)
            if line == '':
                continue
            try:
                self._process_line(source_pos, line)
            except RuntimeError:
                self.last_error_line = i
                raise

    def get_memory(self) -> NanoVMMemoryObject:
        return self._memory
