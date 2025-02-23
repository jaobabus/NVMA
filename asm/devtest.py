import time

MOUNT_POINT = "/local/nvmc-jabus"

# Код, который передается на компиляцию
assembly_code = """\
        .input
        MEMORY 4, n

        .output
        MEMORY 4, result

        .data
        MEMORY 4, return
        MEMORY 4, one
        MEMORY 0, counter
        MEMORY 4, accum

        .code
        LOAD3 1               ; one = 1
        MOV one, lr           ; one = 1
        MOV result, one       ; result = 1
        MOV counter, n        ; counter = n

    multiply:
        JZ counter, mul_exit   ; if counter == 0, exit multiply loop
        ADD accum, accum, one ; accum += one
        SUB counter, counter, one ; counter--
        JZ one, multiply      ; jump to multiply

    mul_exit:
        PC_SWP return, return

    loop:
        JZ counter, end       ; if counter == 0, jump to end
        LOAD_LOW multiply
        PC_SWP return, lr
        MOV result, lr
        SUB counter, counter, one   ; counter--
        JZ one, loop          ; jump to loop (always true)
    end:
        HALT
"""


def write_to_virtual_file(file):
    """Записывает код в виртуальный файл."""
    file.write(assembly_code + '\0')
    print("[✔] Код записан в виртуальный файл")


def read_from_virtual_file(file):
    """Читает результат компиляции из виртуального файла."""
    result = b""
    while True:
        data = file.read()
        if data:
            result += data.encode("utf-8")
        else:
            break
        time.sleep(0.1)  # Подождем немного, чтобы все данные были записаны

    print("[✔] Компиляция завершена. Выходные данные:")
    print(result.decode("utf-8"))


def test_compiler():
    print("[ℹ] Тестирование компилятора через FUSE")
    with open(MOUNT_POINT + '/compiler', "w+") as file:
        write_to_virtual_file(file)
        file.seek(0)
        time.sleep(0.5)  # Ждем немного, чтобы виртуальный файл обработал код
        read_from_virtual_file(file)


def test_decompiler():
    binary_code = bytes([0x84, 0x12, 0xE3, 0x54, 0xFF])  # Example binary code

    print("[ℹ] Sending binary to decompiler service")
    with open(MOUNT_POINT + '/decompiler', "w+") as file:
        file.write(f"text {' '.join(f'{b:02X}' for b in binary_code)}, code=0:5\0")
        file.seek(0)
        time.sleep(0.5)
        output = file.read()

    print("[✔] Decompiled Output:")
    print(output)


def test_compile_and_decompile():
    COMPILER_MOUNT = "/local/nvmc-jabus/compiler"
    DECOMPILER_MOUNT = "/local/nvmc-jabus/decompiler"

    assembly_code = """
.input
MEMORY 4, n

.output
MEMORY 4, result

.data
MEMORY 4, one
MEMORY 4, counter
MEMORY 4, accum

.code
LOAD3 1               ; lr = 1
MOV one, lr
MOV result, lr        ; result = 1
MOV counter, n        ; counter = n
loop:
JZ counter, end       ; if counter == 0, jump to end
MOV accum, result     ; accum = result
multiply:
JZ counter, next      ; if counter == 0, exit multiply loop
ADD accum, accum, result ; accum += result
SUB counter, counter, one ; counter--
JZ one, multiply      ; jump to multiply
next:
MOV result, accum     ; result = accum
SUB counter, counter, one ; counter--
JZ one, loop          ; jump to loop (always true)
end:
HALT

    """

    print("[ℹ] Compiling code")
    with open(COMPILER_MOUNT, "w+") as file:
        file.write(assembly_code + '\0')
        file.seek(0)
        time.sleep(0.5)
        compiled_output = file.read()

    print("[ℹ] Compiled")
    print(compiled_output)

    print("[ℹ] Sending compiled binary to decompiler service")
    with open(DECOMPILER_MOUNT, "w+") as file:
        file.write(f"{compiled_output}\0")
        file.seek(0)
        time.sleep(0.5)
        decompiled_output = file.read()

    print("[✔] Decompiled Output:")
    print(decompiled_output)


if __name__ == "__main__":
    test_compiler()
    test_decompiler()
    test_compile_and_decompile()
