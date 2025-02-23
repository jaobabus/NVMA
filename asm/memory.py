
from dataclasses import dataclass


@dataclass()
class SourcePos:
    file: str
    line: int

    def __str__(self):
        return f"{self.file}:{self.line + 1}"

UnknownSourcePos = SourcePos("<unknown>", 0)


@dataclass()
class MemoryFragment:
    name: str
    position: int | None
    source_pos: SourcePos

    def eval_position(self, base: int) -> int:
        raise NotImplementedError()

    def eval_size(self) -> int:
        raise NotImplementedError()

    def get_data(self) -> bytes:
        raise NotImplementedError()


@dataclass()
class MemoryOffset(MemoryFragment):
    size: int

    def eval_position(self, base: int) -> int:
        self.position = base
        return base + self.eval_size()

    def eval_size(self) -> int:
        return self.size

    def get_data(self) -> bytes:
        return b'\x00' * self.size


@dataclass()
class MemoryData(MemoryFragment):
    data: bytes

    def eval_position(self, base: int) -> int:
        self.position = base
        return base + self.eval_size()

    def eval_size(self) -> int:
        return len(self.data)

    def get_data(self) -> bytes:
        return self.data


@dataclass()
class MemoryRegion(MemoryFragment):
    fragments: list[MemoryFragment]
    max_size: int
    max_top: int | None

    def eval_position(self, base: int) -> int:
        self.position = base
        for f in self.fragments:
            base = f.eval_position(base)
        return base

    def eval_size(self) -> int:
        size = 0
        for f in self.fragments:
            size += f.eval_size()
        return size

    def get_data(self) -> bytes:
        return join_region(self)


def join_region(region: MemoryRegion, default: int = 0x00) -> bytes:
    if region.position is None:
        raise RuntimeError(f"Region {region.name} position not specified")

    region.eval_position(region.position)

    data = bytearray()
    for frag in region.fragments:
        if frag.position is not None:
            if len(data) + region.position < frag.position:
                data.extend([default] *  (frag.position - len(data)))
            elif len(data) + region.position > frag.position:
                raise RuntimeError(f"Memory fragment overlapping ({len(data) + region.position} > {frag.position})")

        frag.position = len(data) + region.position
        data.extend(frag.get_data())

    if len(data) > region.max_size:
        raise RuntimeError(f"Out of memory region {region.name} ({len(data)} > {region.max_size})")

    if region.max_top is not None and len(data) + region.position > region.max_top:
        raise RuntimeError(f"Out of top memory region {region.name} ({len(data) + region.position} > {region.max_top})")

    return bytes(data)


def find_frag(region: MemoryRegion, name: str) -> MemoryFragment:
    for frag in region.fragments:
        if frag.name == name:
            return frag
    raise RuntimeError(f"Not found frag {name} in {region.name}")
