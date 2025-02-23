#include <atomic>
#include <csignal>
#include <iostream>
#include <unordered_set>

#include <getopt.h>

#include "runtime_compiler.hpp"
#include "vmop.hpp"
#include "utils.hpp"

#include <nlohmann/json.hpp>




class Debugger {
public:
    Debugger(NVMAObject& obj)
        : obj(obj), pc(0), running(true)
    {
        memset(ram, 0, sizeof(ram));
    }

    void run()
    {
        std::memcpy(ram, obj.ram.data.data(), obj.ram.data.size());
        while (running) {
            std::string command;
            std::cout << "(debug) ";
            std::getline(std::cin, command);
            process_command(command);
        }
    }

    bool cancel_now()
    {
        return cancel.exchange(true);
    }

private:
    void process_command(const std::string& command)
    {
        if (command == "step" or command == "n") {
            step();
        } else if (command.substr(0, 4) == "goto" or command.substr(0, 2) == "g " or command == "g") {
            go_to(command);
        } else if (command == "continue" or command.substr(0, 1) == "c") {
            continue_execution();
        } else if (command.substr(0, 5) == "break" or command.substr(0, 1) == "b") {
            set_breakpoint(command);
        } else if (command.substr(0, 3) == "mem" or command.substr(0, 1) == "p") {
            show_memory(command);
        } else if (command == "lr") {
            show_lr();
        } else if (command.substr(0, 4) == "list"
                   or command.substr(0, 2) == "l "
                   or command == "l") {
            list_instructions(command);
        } else if (command == "exit" or command.substr(0, 1) == "q") {
            running = false;
        } else {
            std::cout << "Unknown command! Available: step, continue, break [addr], mem [addr], lr, list, exit" << std::endl;
        }
    }

    std::string format_line(const DecompiledLine& line,
                            const uint32_t* prev_ram,
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
                if (all_labels.count(arg)) {
                    auto pos = all_labels[arg].pos / 4;
                    oss << "[";
                    if (prev_ram and ram[pos] != prev_ram[pos])
                        oss << "0x" << fhex(prev_ram[pos], 8) << "->";
                    oss << "0x" << fhex(ram[pos], 8);
                    oss << "]";
                }
                if (&arg != &line.args.back())
                    oss << ", ";
            }
            if (instructions_with_lr.count(line.command)) {
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

    void step()
    {
        if (pc >= obj.text.data.size()) {
            std::cout << "End of program." << std::endl;
            running = false;
            return;
        }
        uint32_t prev_ram[sizeof(ram) / 4];
        std::memcpy(prev_ram, ram, sizeof(ram));
        auto prev = pc;
        execute_one(ram, obj.text.data.data(), pc, nullptr);
        std::cout << format_line(get_decompiled_map().at(prev), prev_ram, true) << std::endl;
    }

    void go_to(const std::string& command)
    {
        auto arg = (command.find(' ') != command.npos ? command.substr(command.find(' ') + 1) : std::to_string((int)pc));
        pc = std::stoi(arg);
        std::cout << "pc = " << "0123456789abcdef"[pc / 16] << "0123456789abcdef"[pc & 0xF] << std::endl;
    }

    void continue_execution()
    {
        while (pc < obj.text.data.size()) {
            if (breakpoints.count(pc) or cancel) {
                cancel = false;
                std::cout << "Hit breakpoint at PC: " << (int)pc << std::endl;
                return;
            }
            step();
        }
    }

    void set_breakpoint(const std::string& command)
    {
        auto arg = command.substr(command.find(' ') + 1);
        int addr = std::stoi(arg, 0, 16);
        breakpoints.insert(addr);
        std::cout << "Breakpoint set at address " << addr << std::endl;
    }

    void show_memory(const std::string& command)
    {
        auto arg = command.substr(command.find(' ') + 1);
        auto value = (arg.find('=') != arg.npos ? arg.substr(arg.find('=')) : "");
        if (value.size() and value[0] == '=')
            value = value.substr(1);
        arg = arg.substr(0, arg.find('='));
        int addr = -1;
        if (not std::isdigit(arg[0])) {
            for (auto psec : NVMAObject::sections) {
                auto& sec = obj.*psec;
                if (sec.labels.count(arg)) {
                    addr = sec.labels.at(arg).pos / 4;
                    break;
                }
            }
        }
        else {
            addr = std::stoi(arg);
        }

        if (addr != -1 and value.size()) {
            uint32_t uval = 0;
            if (value.size() > 1 and (value.substr(0, 2) == "0x" or value.substr(0, 2) == "0X")) {
                uval = std::stoul(value, nullptr, 16);
            }
            else {
                uval = std::stoul(value);
            }
            ram[addr] = uval;
        }

        if (addr != -1) {
            std::cout << "Memory[" << addr << "] = " << ram[addr] << std::endl;
        }
        else {
            std::cout << "Var " << arg << " not found" << std::endl;
        }
    }

    void show_lr()
    {
        std::cout << "LR = " << ram[0] << std::endl;
    }

    void list_instructions(const std::string& command)
    {
        auto arg = (command.find(' ') != command.npos ? command.substr(command.find(' ') + 1) : "");
        const int context_size = (arg.size() ? std::stoi(arg) : 5); // 5 вверх, 5 вниз
        auto& decompiled = get_decompiled();

        auto it = std::find_if(decompiled.begin(), decompiled.end(), [this](const DecompiledLine& line) {
            return line.pos == pc;
        });

        auto start = (it - decompiled.begin() > context_size) ? it - context_size : decompiled.begin();
        auto end = (it + context_size < decompiled.end()) ? it + context_size : decompiled.end();

        std::cout << "Listing instructions:" << std::endl;

        for (auto current = start; current != end; ++current) {
            std::cout << format_line(*current, nullptr, current == it) << std::endl;
        }
    }

    const std::vector<DecompiledLine>& get_decompiled()
    {
        if (decompiled_cache.empty()) {
            try {
                decompiled_cache = decompile(obj);
                for (auto psec : NVMAObject::sections) {
                    auto& sec = obj.*psec;
                    for (auto& [k, v] : sec.labels)
                        all_labels[k] = v;
                }
            }
            catch (const std::runtime_error& e) {
                std::cout << "Error while decompile: " << e.what() << std::endl;
            }
        }
        return decompiled_cache;
    }

    const std::map<uint8_t, DecompiledLine>& get_decompiled_map()
    {
        if (decompiled_map_cache.empty()) {
            auto& decompiled = get_decompiled();
            for (auto& l : decompiled) {
                decompiled_map_cache[l.pos] = l;
            }
        }
        return decompiled_map_cache;
    }

private:
    inline const static std::map<std::string, bool> instructions_with_lr = {
        {"LOAD_OP", true}, {"STORE_OP", true},
        {"LOAD_LOW", true}, {"LOAD_HIGH", true},
        {"JZ", true}, {"JL", true},
        {"LOAD3", true}
    };

private:
    NVMAObject& obj;
    uint32_t ram[32];
    uint8_t pc;
    bool running;
    std::map<std::string, NVMAObject::Label> all_labels;
    std::unordered_set<uint8_t> breakpoints;
    std::atomic<bool> cancel;
    std::vector<DecompiledLine> decompiled_cache;
    std::map<uint8_t, DecompiledLine> decompiled_map_cache;

};


Debugger* global_dbg = nullptr;

void sigint_signal(int)
{
    if (global_dbg) {
        if (global_dbg->cancel_now()) {
            std::cerr << "Debugger not responding" << std::endl;
            exit(1);
        }
    }
}


struct Arguments
{
    std::string source;
    std::string binding;
};


int parse_args(Arguments& args, int argc, char* argv[])
{
    int c;
    auto opts = "i:I:";
    while ((c = getopt(argc, argv, opts)) != -1)
    {
        switch (c)
        {
        case 'i':
            args.source = optarg;
            break;
        case 'I':
            args.binding = optarg;
            break;
        case '?': {
            auto pos = std::string(opts).find(optopt);
            if (pos == std::string::npos) {
                std::cerr << "Unknown option '"
                          << (isprint(optopt) ? std::string(1, optopt) :
                                                std::string("\\x")
                                                + "0123456789ABDF"[optopt / 16]
                                                + "0123456789ABDF"[optopt & 0xF] )
                          << "'" << std::endl;
            }
            else if (opts[pos + 1] == ':') {
                std::cerr << "Option " << optopt << " requires argument" << std::endl;
            }
            else {
                std::cerr << "Unknown option error " << optopt << std::endl;
            }
            break;
        }
        default:
            std::cerr << "Unknown options error " << std::endl;
            return 1;
        }
    }
    return 0;
}


int main(int argc, char* argv[]) {
    Arguments args;
    if (parse_args(args, argc, argv) != 0)
        return 1;

    signal(SIGINT, sigint_signal);

    NVMAObject obj;
    try {
        auto code = load_file(args.source);
        obj = compile(code);

        if (args.binding.size()) {
            auto content = load_file(args.binding);
            parse_sections_file(obj, content);
        }
    }
    catch (const std::runtime_error& e) {
        std::cout << "Error while process args: " << e.what() << std::endl;
        return 1;
    }

    Debugger debugger(obj);
    global_dbg = &debugger;
    debugger.run();
    global_dbg = nullptr;
    return 0;
}
