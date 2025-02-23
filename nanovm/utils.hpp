#pragma once

#include <cstring>
#include <string>

#include <nlohmann/json.hpp>

#include "runtime_compiler.hpp"




std::string load_file(const std::string& path, std::ios::openmode mode = std::ios::in);

uint32_t& ref_value32(NVMAObject::Section& master, const NVMAObject::Section& sec, const std::string& name);
uint32_t get_value32(const NVMAObject::Section& master, const NVMAObject::Section& sec, const std::string& name);

uint32_t& ref_value32(void* ram, const NVMAObject::Section& sec, const std::string& name);
uint32_t get_value32(const void* ram, const NVMAObject::Section& sec, const std::string& name);


void parse_section(NVMAObject::Section& master_section,
                   NVMAObject::Section& section,
                   const nlohmann::json::object_t& binding);

void parse_sections_file(NVMAObject& obj, const std::string& content);
