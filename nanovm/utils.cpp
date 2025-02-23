#include "utils.hpp"

#include <stdexcept>




std::string load_file(const std::string& path, std::ios::openmode mode)
{
    std::ifstream file(path, mode);
    if (!file) {
        throw std::runtime_error("Error: Cannot open file '" + path + "': " + strerror(errno));
    }

    std::string content;
    file.seekg(0, std::ios::end);
    content.resize(file.tellg());
    file.seekg(0, std::ios::beg);
    file.read(content.data(), content.size());

    return content;
}


uint32_t& ref_value32(NVMAObject::Section& master, const NVMAObject::Section& sec, const std::string& name)
{
    return ref_value32(master.data.data(), sec, name);
}

uint32_t get_value32(const NVMAObject::Section& master, const NVMAObject::Section& sec, const std::string& name)
{
    return get_value32(master.data.data(), sec, name);
}


uint32_t& ref_value32(void* ram, const NVMAObject::Section& sec, const std::string& name)
{
    return *reinterpret_cast<uint32_t*>((uint8_t*)ram + sec.labels.at(name).pos);
}

uint32_t get_value32(const void* ram, const NVMAObject::Section& sec, const std::string& name)
{
    return *reinterpret_cast<const uint32_t*>((const uint8_t*)ram + sec.labels.at(name).pos);
}


std::string fhex(uint64_t hex, int octets)
{
    std::string out;
    for (int i = octets - 1; i >= 0; i--) {
        out += "0123456789ABCDEF"[(hex >> i * 4) & 0xF];
    }
    return out;
}



void parse_section(NVMAObject::Section& master_section, NVMAObject::Section& section, const nlohmann::json::object_t& binding)
{
    for (auto& [name, jvalue] : binding)
    {
        auto full_name = section.name + "." + name;
        if (not jvalue.is_number() and not jvalue.is_string()) {
            throw std::runtime_error("Type of " + full_name + " not supported");
        }
        if (not section.labels.count(name)) {
            throw std::runtime_error("Name " + name + " not found in section " + section.name);
        }
        if (section.labels.at(name).size != 4) {
            throw std::runtime_error("Size not 4 not supported");
        }

        uint32_t uvalue;
        if (jvalue.is_string()) {
            auto svalue = *jvalue.get_ptr<const nlohmann::json::string_t*>();
            if (svalue.substr(0, 2) == "0x"
                    or svalue.substr(0, 2) == "0X") {
                uvalue = std::stoul(svalue.substr(2), nullptr, 16);
            }
            else {
                uvalue = std::stoul(svalue.substr(2));
            }
        }
        else if (jvalue.is_number()) {
            uvalue = *(jvalue.get_ptr<const nlohmann::json::number_unsigned_t*>()
                       ? jvalue.get_ptr<const nlohmann::json::number_unsigned_t*>()
                       : (const nlohmann::json::number_unsigned_t*)
                         jvalue.get_ptr<const nlohmann::json::number_integer_t*>());
        }

        std::memcpy(master_section.data.data() + section.labels.at(name).pos, &uvalue, 4);
    }
}


void parse_sections_file(NVMAObject& obj, const std::string& content)
{
    auto json = nlohmann::json::parse(content);
    auto root = json.get_ptr<const nlohmann::json::object_t*>();
    if (not root)
        throw std::runtime_error("Root is not object.");

    for (auto& [name, sec] : *root)
    {
        auto bind = sec.get_ptr<const nlohmann::json::object_t*>();
        if (not bind)
            throw std::runtime_error("Section " + name + " is not object");

        if (NVMAObject::sections_mapping.count(name))
            parse_section(obj.ram,
                          obj.*NVMAObject::sections_mapping.at(name),
                          *bind);
        else
            throw std::runtime_error("Unknown section " + name);
    }
}
