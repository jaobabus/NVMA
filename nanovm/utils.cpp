#include "utils.hpp"

#include <getopt.h>

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


void parse_args(const char* optargs,
                int argc,
                char* argv[],
                std::function<void (char opt, const std::string&)> fn)
{
    int c;
    while ((c = getopt(argc, argv, optargs)) != -1)
    {
        switch (c)
        {
        case '?': {
            auto pos = std::string(optargs).find((char)optopt);
            if (pos == std::string::npos) {
                std::cerr << "Unknown option '"
                          << (isprint(optopt) ? std::string(1, optopt) :
                                                std::string("\\x")
                                                + "0123456789ABDF"[optopt / 16]
                                                + "0123456789ABDF"[optopt & 0xF] )
                          << "'" << std::endl;
                throw std::runtime_error("See above");
            }
            else if (optargs[pos + 1] == ':') {
                std::cerr << "Option " << optopt << " requires argument" << std::endl;
                throw std::runtime_error("See above");
            }
            else {
                std::cerr << "Unknown option error " << optopt << std::endl;
                throw std::runtime_error("See above");
            }
            break;
        }

        default: {
            auto pos = std::string(optargs).find((char)c);
            if (pos != std::string::npos) {
                if (optargs[pos + 1] == ':')
                    fn(c, optarg);
                else
                    fn(c, "");
            }
            else {
                throw std::runtime_error(std::string("Unknown option error ") + (char)optopt);
            }
        }
        }
    }
}


inline const static std::map<std::string, bool> instructions_with_lr = {
    {"LOAD_OP", true}, {"STORE_OP", true},
    {"LOAD_LOW", true}, {"LOAD_HIGH", true},
    {"JZ", true}, {"JL", true},
    {"LOAD3", true}
};

std::string format_line(const DecompiledLine& line,
                        const uint32_t* ram,
                        const uint32_t* prev_ram,
                        const std::map<std::string, NVMAObject::Label>& all_labels,
                        bool is_current)
{
    std::ostringstream oss;
    oss << "0123456789abcdef"[line.pos / 16] << "0123456789abcdef"[line.pos & 0xF] << ": ";
    for (uint8_t b : line.code) {
        oss << "0123456789abcdef"[b / 16] << "0123456789abcdef"[b & 0xF];
    }
    oss << std::string(8 - line.code.size() * 2, ' ');
    if (is_current) {
        oss << " -> " << line.command << " ";
        for (auto& arg : line.args) {
            oss << arg;
            if (ram and all_labels.count(arg)) {
                auto pos = all_labels.at(arg).pos / 4;
                oss << "[";
                if (prev_ram and ram and ram[pos] != prev_ram[pos])
                    oss << "0x" << fhex(prev_ram[pos], 8) << "->";
                oss << "0x" << fhex(ram[pos], 8);
                oss << "]";
            }
            if (&arg != &line.args.back())
                oss << ", ";
        }
        if (ram and instructions_with_lr.count(line.command)) {
            oss << " | lr[";
            if (prev_ram and ram[0] != prev_ram[0])
                oss << "0x" << fhex(prev_ram[0], 8) << "->";
            oss << "0x" << fhex(ram[0], 8) << "]";
        }
    }
    else {
        oss << "    " << line.command << " ";
        for (auto& arg : line.args) {
            oss << arg;
            if (&arg != &line.args.back())
                oss << ", ";
        }
    }
    return oss.str();
}
