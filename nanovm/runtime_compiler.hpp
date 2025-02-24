#pragma once

#include <cstring>
#include <iostream>
#include <fstream>
#include <regex>
#include <thread>
#include <vector>




constexpr std::string_view mountpoint_decompiler = "/local/nvmc-jabus/decompiler";
constexpr std::string_view mountpoint_compiler = "/local/nvmc-jabus/compiler";


struct NVMAObject
{
    struct Label {
        std::string name;
        uint8_t pos;
        uint8_t size;
    };

    struct Section {
        std::string name;
        std::vector<uint8_t> data;
        std::map<std::string, Label> labels;
    };

    Section text;
    Section ram;
    Section input;
    Section output;
    Section data;

    static NVMAObject::Section NVMAObject::* sections[];
    static std::map<std::string, NVMAObject::Section NVMAObject::*> sections_mapping;

    std::string dump() const;
};


inline NVMAObject::Section NVMAObject::* NVMAObject::sections[] = {
    &NVMAObject::text,
    &NVMAObject::ram,
    &NVMAObject::input,
    &NVMAObject::output,
    &NVMAObject::data,
};


inline std::map<std::string, NVMAObject::Section NVMAObject::*> NVMAObject::sections_mapping = {
    {"text", &NVMAObject::text},
    {"ram", &NVMAObject::ram},
    {"input", &NVMAObject::input},
    {"output", &NVMAObject::output},
    {"data", &NVMAObject::data},
};


inline std::ostream& operator<<(std::ostream& os, const NVMAObject& obj)
{
    for (auto psec : NVMAObject::sections) {
        auto& sec = (obj.*psec);
        os << "." << sec.name << ":\n";
        for (auto& [k, l] : sec.labels) {
            os << "  " << l.name << ": " << (int)l.pos << ":" << (int)l.size << "\n";
        }
    }
    return os;
}


inline NVMAObject parse_nvma_object(std::string data)
{
    std::regex pattern(R"((\w+)((?: +[0-9A-Fa-f]{2})+| ),((?: +\w+=\d+:\d+)+| ))");
    std::regex label_pattern(R"( (\w+)=(\d+):(\d+))");

    NVMAObject obj;

    while (data.size()) {
        std::smatch match;
        auto line = data.substr(0, data.find('\n'));
        data = data.substr(line.size() + 1);
        if (std::regex_match(line, match, pattern)) {
            std::vector<uint8_t> bin;
            auto bindata = match.str(2);
            bin.resize(bindata.size() / 3);

            for (size_t i = 0, j = 0; i < bindata.size() and bindata.size() > 2; i += 3, j++) {
                sscanf(bindata.c_str() + i, " %2hhx", &bin[j]);
            }

            std::map<std::string, NVMAObject::Label> labels;
            auto labels_data = match.str(3);
            while (labels_data.size() > 1) {
                std::smatch kv_match;
                auto s = labels_data.substr(0, labels_data.find(' ', 1));
                if (std::regex_match(s, kv_match, label_pattern)) {
                    labels[kv_match.str(1)] = NVMAObject::Label{kv_match.str(1), (uint8_t)std::stoi(kv_match.str(2)), (uint8_t)std::stoi(kv_match.str(3))};
                    labels_data = labels_data.substr(s.size());
                }
                else {
                    throw std::runtime_error("Internal error");
                }
            }

            NVMAObject::Section sec =
            {
                match.str(1),
                std::move(bin),
                std::move(labels)
            };

            if (NVMAObject::sections_mapping.count(sec.name))
                obj.*NVMAObject::sections_mapping.at(sec.name) = sec;
            else
                throw std::runtime_error("Unknown section " + sec.name);
        } else {
            throw std::runtime_error("Compile output error");
        }
    }

    return obj;
}


inline NVMAObject compile(const std::string& code)
{
    std::fstream compiler(mountpoint_compiler.data(), std::ios::in | std::ios::out);
    if (not compiler.is_open())
        throw std::runtime_error("Compiler not accessible");

    compiler.write(code.data(), code.size());
    compiler.write("\0", 1);

    std::string data;
    data.resize(4096); // max size 4096
    auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < end)
    {
        data[0] = '\0';
        compiler.seekg(0, std::ios::end);
        auto size = compiler.tellg();
        compiler.seekg(0, std::ios::beg);
        if (size)
            compiler.read(data.data(), data.size());
        if (data[0] != '\0')
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(33));
    }

    if (data[0] == '\0')
        throw std::runtime_error("Compile timeout error");
    data.resize(std::strlen(data.data()));
    if (data.substr(0, 5) == "error")
        throw std::runtime_error("Compile error: " + data);

    return parse_nvma_object(data);
}


struct DecompiledLine
{
    std::string original;
    uint8_t pos;
    std::vector<uint8_t> code;
    std::string command;
    std::vector<std::string> args;
    std::vector<NVMAObject::Label> labels;
};


inline std::vector<DecompiledLine> decompile(const NVMAObject& obj)
{
    std::fstream compiler(mountpoint_decompiler.data(), std::ios::in | std::ios::out);
    if (not compiler.is_open())
        throw std::runtime_error("Compiler not accessible");

    auto code = obj.dump();
    compiler.write(code.data(), code.size());
    compiler.write("\0", 1);

    std::string data;
    data.resize(4096); // max size 4096
    auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < end)
    {
        data[0] = '\0';
        compiler.seekg(0, std::ios::end);
        auto size = compiler.tellg();
        compiler.seekg(0, std::ios::beg);
        if (size)
            compiler.read(data.data(), data.size());
        if (data[0] != '\0')
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(33));
    }

    if (data[0] == '\0')
        throw std::runtime_error("Decompile timeout error");
    data.resize(std::strlen(data.data()));
    if (data.substr(0, 5) == "error")
        throw std::runtime_error("Decompile error: " + data);

    std::regex pattern(R"(\s*([0-9a-fA-F]+)\:\s*([0-9a-fA-F]+)\s*([^;]+)\s*;\s*((?:.|\s)*)$)");
    std::regex arglist_pattern(R"((?:(\w+)(?:(\=[^,$]+))?)(?:(\s*,\s*)(\w+)(?:(\=[^,$]+))?)*\s*)");
    std::regex skip_pattern(R"(\s+)");

    std::vector<DecompiledLine> lines;
    while (data.size()) {
        std::smatch match;
        std::smatch skip_match;
        auto line = data.substr(0, data.find('\n'));
        data = data.substr(line.size() + 1);
        if (std::regex_match(line, match, pattern)) {
            auto pos = match.str(1);
            auto bin = match.str(2);
            auto cmd = match.str(3);
            auto sec = match.str(4);
            auto original = match.str();

            DecompiledLine dline;

            { // common
                dline.original = original;
                dline.pos = std::strtoul(pos.c_str(), nullptr, 16);
            }

            { // code
                auto ulbin = std::strtoul(bin.c_str(), nullptr, 16);
                auto revbin = std::vector<uint8_t>(reinterpret_cast<const uint8_t*>(&ulbin),
                                                   reinterpret_cast<const uint8_t*>(&ulbin) + bin.size() / 2);
                dline.code = std::vector<uint8_t>(revbin.rbegin(), revbin.rend());
            }

            { // command
                dline.command = cmd.substr(0, cmd.find(' '));
                if (cmd.find(' ') != cmd.npos) {
                    auto args = cmd.substr(cmd.find(' ') + 1);
                    std::smatch args_match;
                    if (std::regex_match(args, skip_match, skip_pattern))
                        args = args.substr(skip_match.str(0).size());
                    while (args.size()) {
                        if (std::regex_match(args, args_match, arglist_pattern)) {
                            dline.args.push_back(args_match.str(1));
                            args = args.substr(args_match.str(1).size() + args_match.str(3).size());
                            if (std::regex_match(args, skip_match, skip_pattern))
                                args = args.substr(skip_match.str(0).size());
                        }
                        else {
                            throw std::runtime_error("Error parse command args");
                        }
                    }
                }
            }

            { // sections
                std::smatch sec_match;
                if (std::regex_match(sec, skip_match, skip_pattern))
                    sec = sec.substr(skip_match.str(0).size());
                while (sec.size()) {
                    if (std::regex_match(sec, sec_match, arglist_pattern)) {
                        auto args = sec_match.str(2).substr(1);
                        dline.labels.push_back(NVMAObject::Label{sec_match.str(1),
                                                                 (uint8_t)std::stoi(args.substr(0, args.find(':'))),
                                                                 (uint8_t)std::stoi(args.substr(args.find(':') + 1))});
                        sec = sec.substr(sec_match.str(1).size() + sec_match.str(2).size() + sec_match.str(3).size());
                        if (std::regex_match(sec, skip_match, skip_pattern))
                            sec = sec.substr(skip_match.str(0).size());
                    }
                    else {
                        throw std::runtime_error("Error parse section args");
                    }
                }
            }

            lines.emplace_back(std::move(dline));
        }
        else {
            throw std::runtime_error("Parse decompiled line error");
        }
    }

    return lines;
}

inline std::string NVMAObject::dump() const
{
    std::ostringstream output;
    for (auto psec : sections) {
        auto& sec = (this->*psec);
        output << sec.name;
        for (auto byte : sec.data) {
            output << " ";
            output << "0123456789ABCDEF"[byte >> 4];
            output << "0123456789ABCDEF"[byte & 0xF];
        }
        if (sec.data.empty())
            output << " ";
        output << ",";
        for (auto& [k, l] : sec.labels) {
            output << " " << l.name << "=" << (int)l.pos << ":" << (int)l.size;
        }
        output << "\n";
    }
    return output.str();
}
