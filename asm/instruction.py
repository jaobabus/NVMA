import enum
from dataclasses import dataclass

from bitstring import BitArray

from memory import MemoryFragment, MemoryRegion, SourcePos


@dataclass()
class BitFrag:
    start: int
    end: int

    def encode(self, data: BitArray, value: int):
        mask = ~(0xFFFFFF << (self.end - self.start)) & 0xFFFFFF
        data[self.start : self.end] = reversed(BitArray(uint=value & mask, length=self.end-self.start))
        value >>= self.end - self.start
        return value

    def decode(self, data: BitArray) -> int:
        w = data[self.start : self.end]
        w.reverse()
        return w.uint


class ArgCathegory(enum.Enum):
    Register = 'register'
    Const = 'const'
    Code = 'code'


@dataclass()
class InstructionDesc:
    opcode: int
    args: list[str]
    length: int
    args_cathegories: dict[str, ArgCathegory]

    def encode(self, position: SourcePos, args: dict[str, int | str]) -> MemoryFragment:
        raise NotImplementedError()

    def decode(self, value: bytes) -> dict[str, int]:
        raise NotImplementedError()

    def check(self, value: bytes) -> tuple[bool, str]:
        raise NotImplementedError()


@dataclass()
class BuilderDescImpl(InstructionDesc):
    handlers: dict[str, BitFrag]
    consts: dict[str, tuple[BitFrag, int]]

    def encode(self, source_pos: SourcePos, args: dict[str, int | str]) -> MemoryFragment:
        data = BitArray(self.length * 8)
        if len(args) != len(set(args.keys()) | set(self.handlers.keys())):
            raise RuntimeError(f"Arguments lens not matches")
        for bits, value in self.consts.values():
            bits.encode(data, value)
        for name, arg in args.items():
            r = self.handlers[name].encode(data, arg)
            if r:
                bs = self.handlers[name].end - self.handlers[name].start
                raise RuntimeError(f"Argument {name} value 0x{arg:02X} overflow of 0x{~(0xFFFFFF << bs) & 0xFFFFFF:02X}",
                                   source_pos)

        @dataclass()
        class Frag(MemoryFragment):
            data: bytes

            def eval_position(self, base: int) -> int:
                self.position = base
                return base + self.eval_size()

            def eval_size(self) -> int:
                return len(self.data)

            def get_data(self) -> bytes:
                return self.data

        data.reverse()
        data_bytes = bytes(reversed(data.tobytes()))

        return Frag(f"Op[{self.opcode}]@{source_pos}",
                    None,
                    source_pos,
                    data_bytes)

    def decode(self, value: bytes) -> dict[str, int]:
        if len(value) < self.length:
            raise RuntimeError(f"Length error")

        word = BitArray(length=len(value) * 8, bytes=bytes(reversed(value)))
        word.reverse()

        args = {}
        for name, arg in self.handlers.items():
            args[name] = arg.decode(word)

        return args

    def check(self, value: bytes) -> tuple[bool, str]:
        if len(value) < self.length:
            return False, "Too small word"

        word = BitArray(length=len(value) * 8, bytes=bytes(reversed(value)))
        word.reverse()

        for name, const in self.consts.items():
            if const[0].decode(word) != const[1]:
                return False, f"Const {name} error {const[0].decode(word)} != {const[1]}"

        return True, ''


class InstructionDescBuilder:
    def __init__(self, opcode: int, length: int):
        self._opcode = opcode
        self._length = length
        self._handlers: dict[str, BitFrag] = {}
        self._consts: dict[str, tuple[BitFrag, int]] = {}
        self.const_bits('opcode', 5, 8, opcode)
        self._args_cathegories: dict[str, ArgCathegory] = {}

    def const_bits(self, name: str, start: int, end: int, value: int) -> "InstructionDescBuilder":
        self._consts[name] = (BitFrag(start, end), value)
        return self

    def arg(self, name: str, frag: BitFrag) -> "InstructionDescBuilder":
        self._handlers[name] = frag
        return self

    def arg_bits(self, name: str, start: int, end: int = -1, cathegory: ArgCathegory = ArgCathegory.Register) -> "InstructionDescBuilder":
        if end == -1:
            end = start + 1

        return self.arg(name, BitFrag(start, end)).arg_cathegory(name, cathegory)

    def arg_cathegory(self, arg: str, cath: ArgCathegory) -> "InstructionDescBuilder":
        self._args_cathegories[arg] = cath
        return self

    def build(self) -> InstructionDesc:
        handlers = {}
        partial = {}
        for key, value in self._handlers.items():
            if '@' in key:
                key, part = key.split('@')
                partial[key] = partial.get(key, {})
                partial[key][int(part)] = value
            else:
                handlers[key] = value

        def handler_builder(_seq: list):
            @dataclass()
            class SequencedBitFrag(BitFrag):
                sequence: list[BitFrag]

                def encode(self, data: BitArray, _value: int):
                    for v in self.sequence:
                        _value = v.encode(data, _value)
                    return _value

                def decode(self, data: BitArray) -> int:
                    _value = 0
                    for v in reversed(self.sequence):
                        _value <<= v.end - v.start
                        _value |= v.decode(data)
                    return _value

            top = sum(s.end - s.start for s in seq)
            return SequencedBitFrag(0, top, seq)

        for key, seq in partial.items():
            seq = [val for part, val in sorted(seq.items(), key=lambda x: x[0])]
            handlers[key] = handler_builder(seq)

        return BuilderDescImpl(self._opcode,
                               list(handlers.keys()),
                               self._length,
                               self._args_cathegories,
                               handlers,
                               self._consts)


class MovInstruction(InstructionDesc):
    def __init__(self):
        super().__init__(-1, ['mem1', 'mem2'], 2,
                         {
                             'mem1': ArgCathegory.Register,
                             'mem2': ArgCathegory.Register
                         })

    def encode(self, position: SourcePos, args: dict[str, int | str]) -> MemoryFragment:
        return MemoryRegion("mov", None, position, [
            Instructions.LOAD_OP.encode(position, {'mem': args['mem2']}),
            Instructions.STORE_OP.encode(position, {'mem': args['mem1']}),
        ], 2, None)

    def check(self, value: bytes) -> tuple[bool, str]:
        return False, "Is composite instruction"


class Instructions:
    Builder = InstructionDescBuilder

    LOAD_OP   = Builder(0, 1) \
                .arg_bits('mem', 0, 5) \
                .build()

    STORE_OP  = Builder(1, 1) \
                .arg_bits('mem', 0, 5) \
                .build()

    MOV       = MovInstruction()

    JL        = Builder(2, 2) \
                .const_bits('is_jl', 4, 5, 1) \
                .arg_bits('rarg', 0, 4) \
                .arg_bits('data', 8, 16, ArgCathegory.Code) \
                .build()

    JZ        = Builder(2, 2) \
                .const_bits('is_jl', 4, 5, 0) \
                .arg_bits('rarg', 0, 4) \
                .arg_bits('data', 8, 16, ArgCathegory.Code) \
                .build()

    LOAD_LOW  = Builder(3, 2) \
                .const_bits('is_high', 4, 5, 0) \
                .arg_bits('low@1', 0, 4) \
                .arg_bits('low@0', 8, 16) \
                .arg_cathegory('low', ArgCathegory.Code) \
                .build()

    LOAD_HIGH = Builder(3, 3) \
                .const_bits('is_high', 4, 5, 1) \
                .arg_bits('low@2', 0, 4) \
                .arg_bits('low@1', 8, 16) \
                .arg_bits('low@0', 16, 24) \
                .arg_cathegory('low', ArgCathegory.Const) \
                .build()

    ADD       = Builder(4, 2) \
                .const_bits('is_sub', 4, 5, 0) \
                .arg_bits('result', 0, 4) \
                .arg_bits('mem1', 12, 16, ArgCathegory.Register) \
                .arg_bits('mem2', 8, 12, ArgCathegory.Register) \
                .build()

    SUB       = Builder(4, 2) \
                .const_bits('is_sub', 4, 5, 1) \
                .arg_bits('result', 0, 4) \
                .arg_bits('mem1', 12, 16) \
                .arg_bits('mem2', 8, 12) \
                .build()

    AND       = Builder(5, 2) \
                .const_bits('is_or', 4, 5, 0) \
                .arg_bits('result', 0, 4) \
                .arg_bits('mem1', 12, 16) \
                .arg_bits('mem2', 8, 12) \
                .build()

    OR        = Builder(5, 2) \
                .const_bits('is_or', 4, 5, 1) \
                .arg_bits('result', 0, 4) \
                .arg_bits('mem1', 12, 16) \
                .arg_bits('mem2', 8, 12) \
                .build()

    LS        = Builder(6, 2) \
                .const_bits('is_right', 4, 5, 0) \
                .arg_bits('result', 0, 4) \
                .arg_bits('mem', 12, 16) \
                .arg_bits('count', 8, 12, ArgCathegory.Const) \
                .build()

    RS        = Builder(6, 2) \
                .const_bits('is_right', 4, 5, 1) \
                .arg_bits('result', 0, 4) \
                .arg_bits('mem', 12, 16) \
                .arg_bits('count', 8, 12, ArgCathegory.Const) \
                .build()

    CALL      = Builder(7, 2) \
                .const_bits('extend', 4, 5, 0) \
                .arg_bits('callback', 0, 4) \
                .arg_bits('result', 12, 16) \
                .arg_bits('arg', 8, 12) \
                .build()

    PC_SWP    = Builder(7, 2) \
                .const_bits('extend3', 2, 5, 6) \
                .arg_bits('mem@0', 13, 16) \
                .arg_bits('mem@1', 0, 2) \
                .arg_cathegory('mem', ArgCathegory.Register) \
                .arg_bits('save', 8, 13) \
                .build()

    HALT      = Builder(7, 1) \
                .const_bits('extend', 4, 5, 1) \
                .const_bits('extend2', 4, 5, 1) \
                .const_bits('halt', 0, 4, 0xF) \
                .build()

    LOAD3     = Builder(7, 1) \
                .const_bits('extend', 4, 5, 1) \
                .const_bits('extend2', 3, 4, 0) \
                .arg_bits('value', 0, 3, ArgCathegory.Const) \
                .build()

    all_instructions: dict[str, InstructionDesc] = {}


Instructions.all_instructions = {inst: getattr(Instructions, inst)
                                 for inst in dir(Instructions)
                                 if isinstance(getattr(Instructions, inst), InstructionDesc)
                                 }
