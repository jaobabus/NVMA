#!/usr/bin/python3

import errno
import threading
import typing
from dataclasses import dataclass

try:
    from fusepy import FUSE, Operations
except ImportError:
    exec('from fuse import FUSE, Operations')

import regex

from memory import find_frag, MemoryRegion, MemoryData
from compiler import NanoVMAsmParser, NanoVMMemoryObject
from disasm import Disassembler


region_pattern = regex.compile(r"(?<name>\w+)(?<data>(?: (?<byte>[0-9A-Fa-f]{2}))| )+(?:,(?<frags>(?: +(?<k>\w+)=(?<v>\d+:\d+))+| ))?")


@dataclass()
class BaseTask:
    id: int

    def start(self):
        raise NotImplementedError()

    def write(self, data: bytes) -> int:
        raise NotImplementedError()

    def read(self, size: int) -> bytes:
        raise NotImplementedError()

    def size(self) -> int:
        raise NotImplementedError()


class NVMCompilerTask(BaseTask):
    def __init__(self, _id: int):
        super().__init__(id)
        self._task_thread = None
        self._buffer = bytearray()
        self._output = None

    @staticmethod
    def _dump_region(region: MemoryRegion):
        data = ' '.join(f"{a:02X}" for a in region.get_data())
        frags = ' '.join(f"{frag.name}={frag.position}:{len(frag.get_data())}" for frag in region.fragments)
        return f"{region.name} {data}, {frags}"

    def _task(self):
        comp = NanoVMAsmParser()
        try:
            buf = self._buffer.decode('utf-8')
            print(f"Compiling {buf[:64]}...")
            comp.init()
            comp.process_file(buf)
            obj = comp.get_memory()

            json_obj = {
                'ram': self._dump_region(obj.ram),
                'text': self._dump_region(obj.text),
                'input': self._dump_region(typing.cast(MemoryRegion, find_frag(obj.ram, 'input'))),
                'output': self._dump_region(typing.cast(MemoryRegion, find_frag(obj.ram, 'output'))),
                'data': self._dump_region(typing.cast(MemoryRegion, find_frag(obj.ram, 'data'))),
            }
            self._output = ''.join(f"{v}\n" for _, v in json_obj.items()).encode('utf-8')
            print(f"Done")

        except RuntimeError:
            import traceback
            self._output = f"error at {(comp.last_error_line or 0) + 1}: {traceback.format_exc()}".encode('utf-8')
            print(f"Compile error at {(comp.last_error_line or 0) + 1}")

    def start(self):
        self._task_thread = threading.Thread(None, self._task)
        self._task_thread.start()

    def write(self, data: bytes):
        self._buffer += data
        return len(data)

    def read(self, size: int) -> bytes | None:
        if self._output is not None:
            r = self._output[:size]
            self._output = self._output[size:]
            print(f"Sent {r[:16]}... bytes")
            return r
        return None

    def size(self) -> int:
        return len(self._output) if self._output else 0


class NVMDecompilerTask(BaseTask):
    def __init__(self, _id: int):
        super().__init__(id)
        self._task_thread = None
        self._buffer = bytearray()
        self._output = None

    @staticmethod
    def _load_region(line: str) -> MemoryRegion:
        match = region_pattern.match(line)
        if not match:
            raise RuntimeError("Region regex match error")
        cd = match.capturesdict()

        data = bytes([int(a, 16) for a in cd['byte']])

        fragments = []
        for k, v in zip(cd['k'], cd['v']):
            pos, size = map(int, v.split(':'))
            fragments.append(MemoryData(k, pos, data[pos:pos+size]))

        position = min([256, *[f.position if f.position is not None else 256 for f in fragments]])
        return MemoryRegion(cd['name'][0], position != 256 and position or None, fragments, 256, 256)

    def _task(self):
        try:
            buf = self._buffer.decode('utf-8')
            print(f"Decompiling {buf[:64]}...")
            obj = NanoVMMemoryObject()
            for line in buf.strip().split('\n'):
                reg = self._load_region(line.strip())
                if reg.name in ('input', 'output', 'data'):
                    obj.ram.fragments.append(reg)
                if reg.name in ('text', ):
                    reg.position = 0
                    obj.text = reg

            for required in ('input', 'output', 'data'):
                try:
                    find_frag(obj.ram, required)
                except RuntimeError:
                    obj.ram.fragments.append(MemoryRegion(required, None, [], 128, 128))

            decompiler = Disassembler(obj)
            self._output = decompiler.disassemble().encode('utf-8')
            print(f"Done")
        except RuntimeError:
            import traceback
            self._output = f"error: {traceback.format_exc()}".encode('utf-8')
            print(f"Decompile error")

    def start(self):
        self._task_thread = threading.Thread(None, self._task)
        self._task_thread.start()

    def write(self, data: bytes):
        self._buffer += data
        return len(data)

    def read(self, size: int) -> bytes | None:
        if self._output is not None:
            r = self._output[:size]
            self._output = self._output[size:]
            print(f"Sent {r[:16]}... bytes")
            return r
        return None

    def size(self) -> int:
        return len(self._output) if self._output else 0


class BaseVirtualFile:
    def __init__(self, path: str):
        self._path = path
        self._tasks: dict[int, BaseTask] = {}
        self._last_task_id = 0

    def getattr(self, fh=None):
        s = (self._tasks[fh].size() or 0
             if fh is not None
             else (self._tasks[self._last_task_id].size() or 0
                   if self._last_task_id in self._tasks
                   else 0))
        print(f"Size {s}")
        return {"st_mode": (0o666 | 0o100000), "st_nlink": 1,
                "st_size": s}  # Файл

    def open(self, flags):
        self._last_task_id += 1
        self._tasks[self._last_task_id] = self.new_task(self._last_task_id)
        return self._last_task_id

    def read(self, size, offset, fh):
        data = self._tasks[self._last_task_id].read(size)
        if data == b'':
            self._tasks.pop(self._last_task_id)
        return data or b''

    def write(self, data, offset, fh):
        run = False
        if b'\0' in data:
            run = True
            data = data.replace(b'\0', b'')
        r = self._tasks[self._last_task_id].write(data)
        if run:
            self._tasks[self._last_task_id].start()
        return r + int(run)

    def truncate(self, length, **kwargs):
        int()

    def new_task(self, _id: int):
        raise NotImplementedError()


class VirtualFileCompiler(BaseVirtualFile):
    def new_task(self, _id: int):
        return NVMCompilerTask(_id)


class VirtualFileDecompiler(BaseVirtualFile):
    def new_task(self, _id: int):
        return NVMDecompilerTask(_id)


class VirtualDir(Operations):
    def __init__(self):
        self._files: dict[str, BaseVirtualFile] = {
            '/compiler': VirtualFileCompiler('/compiler'),
            '/decompiler': VirtualFileDecompiler('/decompiler'),
        }
        self._seq_ids: dict[str, int] = {
            '/compiler': 1,
            '/decompiler': 2,
        }

    def getattr(self, path, fh=None):
        if path == "/":
            return {"st_mode": (0o755 | 0o040000), "st_nlink": 2}  # Директория
        elif path in self._files:
            return self._files[path].getattr(fh // 16 if fh is not None else fh)
        else:
            raise OSError(errno.ENOENT, "No such file")

    def open(self, path, flags):
        _id = self._files[path].open(flags)
        return _id * 16 + self._seq_ids[path]

    def read(self, path, size, offset, fh):
        return self._files[path].read(size, offset, fh // 16)

    def write(self, path, data, offset, fh):
        return self._files[path].write(data, offset, fh // 16)

    def truncate(self, path, length, **kwargs):
        return self._files[path].truncate(length, **kwargs)


def main(mount_point):
    FUSE(VirtualDir(), mount_point, foreground=True, nothreads=True)


if __name__ == "__main__":
    mount_dir = "/local/nvmc-jabus"
    main(mount_dir)
