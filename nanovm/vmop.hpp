#pragma once

#include <stdint.h>



/*
Формат описания полей:
`[ value1: bit[3] ] [ value2: bit[4] ] [ 1: bit ] [ data: bit[8] ]`
  - описывает 16 бит данных, биты следуют справа налево, байты тоже, то есть тут если value1=3, value2=2, data=0x70, то в памяти окажется:
0x7065 (little endian) или 0x65 0x70

Команды:
1) `LOAD_OP mem` (LR = *mem) - загрузка из памяти во временный регистр, назовем его LR
`[ 0: bit[3] ] [ mem: bit[5] ]`

2) `STORE_OP mem` (LR = *mem) - загрузка из временного регистра LR в память
`[ 1: bit[3] ] [ mem: bit[5] ]`

3) `MOV mem1, mem2` (*mem1 = *mem2) - копирование из mem2 в mem1, команда составная инвалидирует LR
`LOAD_OP mem2`
`STORE_OP mem1`

4) `JL rarg, addr` (LR < *rarg) - перейти если меньше
`[ 2: bit[3] ] [ rarg: bit[4] ] [ addr: bit[8] ]`

5) `JZ rarg, addr` (LR < *rarg) - перейти если равно
`[ 2: bit[3] ] [ rarg: bit[4] ] [ addr: bit[8] ]`

6) `LOAD1_LOW mem, value` (LR = LR & ~(0xFFF) | value & 0xFFF) - загрузить младшие 12 бит в LR
`[ 3: bit[3] ] [ 0: bit ] [ value: bit[12] ]`

7) `LOAD1_HIGH mem, value` (LR = value << 12) - загрузить в старшие 20 бит (с занулением первых 12) в LR
`[ 3: bit[3] ] [ 1: bit ] [ value: bit[20] ]`

8) `LOAD1 mem, value` - составная
`LOAD1_HIGH mem, (value >> 12)`
`LOAD1_LOW mem, value`

9) `ADD result, mem1, mem2` (*result = *mem1 + *mem2) - сложить два слова в третий
`[ 4: bit[3] ] [ 0: bit ] [ result: bit[4] ] [ mem1: bit[4] ] [ mem2: bit[4] ]`

10) `SUB result, mem1, mem2` (*result = *mem1 - *mem2) - вычесть из первого второй и сложить в третий
`[ 4: bit[3] ] [ 1: bit ] [ result: bit[4] ] [ mem1: bit[4] ] [ mem2: bit[4] ]`

11) `AND result, mem1, mem2` (*result = *mem1 & *mem2) - операций И с двумя операндами в третий
`[ 5: bit[3] ] [ 0: bit ] [ result: bit[4] ] [ mem1: bit[4] ] [ mem2: bit[4] ]`

12) `OR result, mem1, mem2` (*result = *mem1 | *mem2) - операций ИЛИ с двумя операндами в третий
`[ 5: bit[3] ] [ 1: bit ] [ result: bit[4] ] [ mem1: bit[4] ] [ mem2: bit[4] ]`

13) `LS result, mem, count` (*result = *mem << count) - операнда влево на count бит и запись в третий
`[ 6: bit[3] ] [ 1: bit ] [ result: bit[4] ] [ mem: bit[4] ] [ count: bit[4] ]`

14) `RS result, mem, count` (*result = *mem >> count) - операнда вправо на count бит и запись в третий
`[ 6: bit[3] ] [ 1: bit ] [ result: bit[4] ] [ mem: bit[4] ] [ count: bit[4] ]`

15) `CALL callback, result, arg` (C++: auto cb = reinterpret_cast<uint32_t(*)()>(*callback); *result = cb(*arg);) - вызвать функцию хостовой системы с аргументом arg и положить результат в result
`[ 7: bit[3] ] [ 0: bit ] [ callback: bit[4] ] [ result: bit[4] ] [ arg: bit[4] ]`

16) `PC_SWP mem, save` (*save = pc; pc = mem) - сохранить pc и установить его новое значение
`[ 7: bit[3] ] [ 1: bit ] [ 1: bit ] [ 0: bit ] [ mem: bit[5] ] [ save: bit[5] ]`

17) `HALT` - прервать выполнение
`[ 7: bit[3] ] [ 1: bit ] [ 1: bit ] [ 7: bit[3] ]`

18) `LOAD3` - загрузить 3 бита в LR
`[ 7: bit[3] ] [ 1: bit ] [ 0: bit ] [ value: bit[3] ]`

ОЗУ:
до 128 байт
LR - это первые 4 байта

КОД:
до 256 байт

АСМ:
1) `MEMORY size[, name]` - выделить место в памяти size по имени name


парсить можно с помощью с помощью модуля regex:
all_regex=regex.compile(r"^[ \t]*(?<label>\w+)\:$|^[ \t]*(?<op>\w+)[ \t]*(?&arg)(?:[ \t]*,[ \t]*(?<arg>[0-9][0-9xA-Fa-f]*|\w+))*$|^[ \t]*(?<section>\.\w+)$", regex.M | regex.I)
соответственно тип команды пусть определяется через capturesdict и все arg можно так же через него получить

вот рабочий пример, который складывает a, b предоставляет print_cb
```
.input
MEMORY 4, a
MEMORY 4, b
MEMORY 4, print_cb

.output
MEMORY 4, result

.data
MEMORY 4, temp

.code
start:
  ADD result, a, b
  CALL print_cb, temp, result
  HALT
```

примера запуска пока нет

*/



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



void execute(uint32_t* ram,
             const void* text,
             uint8_t start,
             uint32_t (*proc)(uint32_t, uint32_t));

bool execute_one(uint32_t* ram,
                 const uint8_t* code,
                 uint8_t& pc,
                 uint32_t (*proc)(uint32_t proc_id, uint32_t arg));
