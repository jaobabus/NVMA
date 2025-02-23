
import sys
from argparse import ArgumentParser

from compiler import NanoVMAsmParser


def run(target: str, output: str):
    asm = open(target).read()
    compiler = NanoVMAsmParser()
    compiler.init()
    compiler.process_file(asm)
    obj = compiler.get_memory()
    data = [
        f"ram {' '.join(f'{a:02X}' for a in obj.ram.get_data())}",
        f"text {' '.join(f'{a:02X}' for a in obj.text.get_data())}",
    ]
    data = '\n'.join(data) + '\n'
    with open(output, 'w') as w:
        w.write(data)


def main():
    args_parser = ArgumentParser()
    args_parser.add_argument('target')
    args_parser.add_argument('output')
    args = args_parser.parse_args(sys.argv[1:])

    run(target=args.target, output=args.output)


if __name__ == '__main__':
    main()
