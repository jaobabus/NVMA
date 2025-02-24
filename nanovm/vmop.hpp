#pragma once

#include <stdint.h>



void execute(uint32_t* ram,
             const void* text,
             uint8_t start,
             uint32_t (*proc)(uint32_t, uint32_t),
             uint8_t* exec_flag);

bool execute_one(uint32_t* ram,
                 const uint8_t* code,
                 uint8_t& pc,
                 uint32_t (*proc)(uint32_t proc_id, uint32_t arg));
