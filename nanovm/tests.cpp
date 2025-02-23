
#include <iostream>
#include <vector>
#include <memory>
#include <getopt.h>

#include "runtime_compiler.hpp"
#include "vmop.hpp"
#include "utils.hpp"




class AbstractNVMTest
{
public:
    virtual ~AbstractNVMTest() = default;
    virtual std::string get_name() const { return "Unknown"; }
    virtual const NVMAObject& get_binary() const = 0;
    virtual bool check_result(const uint32_t* ram) const = 0;
    virtual void dump_error(const uint32_t* ram) const = 0;
};


class NVMFactorialTest : public AbstractNVMTest
{
public:
    NVMFactorialTest()
    {
        auto c = R"(
        .input
        MEMORY 4, n
        .output
        MEMORY 4, result
        .data
        MEMORY 4, zero
        MEMORY 4, one
        MEMORY 4, counter
        MEMORY 4, accum
        MEMORY 4, tmp1
        MEMORY 4, tmp2
        MEMORY 4, return
        .code
        init:
            LOAD3 0
            STORE_OP zero
            LOAD3 1
            STORE_OP one
            JZ lr, factorial
        multiply:
            LOAD_OP tmp1
            AND tmp1, tmp1, zero
        multipy_loop:
            JZ zero, multiply_end
            ADD tmp1, tmp1, tmp2
            SUB lr, lr, one
            JZ lr, multipy_loop
        multiply_end:
            LOAD_OP tmp1
            PC_SWP return, return
        factorial:
            MOV result, one
            MOV counter, n
        loop:
            LOAD3 0
            JZ counter, end
            MOV tmp1, counter
            MOV tmp2, result
            LOAD3 0
            LOAD_LOW multiply
            PC_SWP return, lr
            STORE_OP result
            SUB counter, counter, one
            JZ lr, loop
        end:
            HALT
        )";
        obj = compile(c);
        ref_value32(obj.ram, obj.input, "n") = 12;
    }

    std::string get_name() const override { return "Factorial of 12"; }

    const NVMAObject& get_binary() const override
    {
        return obj;
    }

    bool check_result(const uint32_t* ram) const override
    {
        return get_value32(ram, obj.output, "result") == 479001600; // result should be 12! = 479001600
    }

    void dump_error(const uint32_t* ram) const override
    {
        auto value = get_value32(ram, obj.output, "result");
        std::cerr << "Result " << value << " not match with expected " << 479001600 << std::endl;
    }

private:
    NVMAObject obj;

};


class NVMTestFromFile : public AbstractNVMTest
{
public:
    NVMTestFromFile(const std::string& source,
                    const std::string& input,
                    const std::map<std::string, uint32_t>& values)
        : name("Test from " + source)
    {
        obj = compile(load_file(source));

        if (input.size())
            parse_sections_file(obj, load_file(input));

        for (auto& [key, value] : values) {
            if (key.find('.') == key.npos)
                throw std::runtime_error("Can't set value to section, use <section>.<label>=<value>");
            auto first = key.substr(0, key.find('.'));
            auto secont = key.substr(key.find('.') + 1);
            if (NVMAObject::sections_mapping.count(name))
                ref_value32(obj.ram,
                            obj.*NVMAObject::sections_mapping.at(name),
                            name) = value;
            else
                throw std::runtime_error("Unknown section " + name);
        }
    }

public:
    std::string get_name() const override
    {
        return name;
    }

    const NVMAObject& get_binary() const override
    {
        return obj;
    }

    bool check_result(const uint32_t* ram) const override
    {
        for (auto [name, label] : obj.output.labels)
        {
            if (get_value32(ram, obj.output, name) != get_value32(obj.ram, obj.output, name))
                return false;
        }
        return true;
    }

    void dump_error(const uint32_t* ram) const override
    {
        int max = -1;
        for (auto& [name, label] : obj.output.labels)
            max = std::max<int>(name.size(), max);

        for (auto& [name, label] : obj.output.labels)
        {
            auto v = get_value32(ram, obj.output, name);
            auto e = get_value32(obj.ram, obj.output, name);

            const std::string ok_color = "\033[1;38;5;118m";
            const std::string er_color = "\033[1;38;5;160m";
            const std::string wn_color = "\033[1;38;5;184m";
            auto pd = std::string(max - name.size(), ' ');
            std::cerr
                    << (v == e ? ok_color + "OK\033[0m   : " : er_color + "ERROR\033[0m: ")
                    << pd << name << ": "
                    << "got=" << (v == e ? "" : er_color) << "0x" << fhex(v, 8) << "\033[0m" << ", "
                    << "exp=" << (v == e ? "" : wn_color) << "0x" << fhex(e, 8) << "\033[0m" << std::endl;
        }
    }

private:
    std::string name;
    NVMAObject obj;

};


void run_test(const AbstractNVMTest& test)
{
    std::cout << "Running test: " << test.get_name() << "... ";
    const NVMAObject& obj = test.get_binary();
    uint32_t ram[32] = {0};
    for (auto& [name, label] : obj.input.labels) {
        std::memcpy((uint8_t*)&ram + label.pos, obj.ram.data.data() + label.pos, 4);
    }

    if (obj.text.data.size())
        execute(ram, obj.text.data.data(), 0, nullptr);
    else
        throw std::runtime_error(".text section is empty");

    if (test.check_result(ram)) {
        std::cout << "\033[38;5;76m" << "PASSED" << "\033[0m" << std::endl;
    }
    else {
        std::cerr << "\033[38;5;160m" << "FAILED" << "\033[0m" << std::endl;
        test.dump_error(ram);
    }
}


struct Arguments
{
    struct Source {
        std::string source;
        std::string input;
        std::map<std::string, uint32_t> values;
    };

    std::vector<Source> sources;
};


Arguments parse_args(int argc, char* argv[])
{
    Arguments args;
    int c;
    auto opts = "i:";
    while ((c = getopt(argc, argv, opts)) != -1)
    {
        switch (c)
        {
        case 'i': {
            std::string arg = optarg;
            if (arg.find(':') == arg.npos)
                throw std::runtime_error("Expected -i <source>:<input>[:<name>=<value>]*");

            auto source = arg.substr(0, arg.find(':'));
            auto vars_start = arg.find(':', arg.find(':') + 1);
            auto input = arg.substr(arg.find(':') + 1, vars_start);

            Arguments::Source info{source, input};
            for (auto next = vars_start;
                 vars_start != std::string::npos;
                 vars_start = arg.find(':', vars_start + 1))
            {
                auto end = arg.find(':', next + 1);
                auto pair = arg.substr(next, end - next);
                auto eq_pos = pair.find('=');

                if (eq_pos == std::string::npos)
                    throw std::runtime_error("Parse pair '" + pair + "' error");

                auto value = pair.substr(eq_pos + 1);
                uint32_t uvalue;
                if (value.substr(0, 2) == "0x"
                        or value.substr(0, 2) == "0X") {
                    uvalue = std::stoul(value.substr(2), nullptr, 16);
                }
                else {
                    uvalue = std::stoul(value.substr(2));
                }

                info.values.emplace(std::make_pair(pair.substr(0, eq_pos),
                                                   uvalue));
            }

            args.sources.emplace_back(std::move(info));
        }   break;

        case '?': {
            auto pos = std::string(opts).find(optopt);
            if (pos == std::string::npos) {
                std::cerr << "Unknown option '"
                          << (isprint(optopt) ? std::string(1, optopt) :
                                                std::string("\\x")
                                                + "0123456789ABDF"[optopt / 16]
                                                + "0123456789ABDF"[optopt & 0xF] )
                          << "'" << std::endl;
                throw std::runtime_error("See above");
            }
            else if (opts[pos + 1] == ':') {
                std::cerr << "Option " << optopt << " requires argument" << std::endl;
                throw std::runtime_error("See above");
            }
            else {
                std::cerr << "Unknown option error " << optopt << std::endl;
                throw std::runtime_error("See above");
            }
            break;
        }

        default:
            std::cerr << "Unknown options error " << std::endl;
            throw std::runtime_error("See above");
        }
    }
    return args;
}


int main(int argc, char* argv[])
{
    std::vector<std::unique_ptr<AbstractNVMTest>> tests;

    try {
        auto args = parse_args(argc, argv);
        for (auto& [source, input, values] : args.sources)
        {
            tests.push_back(std::make_unique<NVMTestFromFile>(source, input, values));
        }
    }
    catch (const std::runtime_error& e) {
        std::cout << "Error while process args: " << e.what() << std::endl;
        return 1;
    }

    for (const auto& test : tests)
    {
        run_test(*test);
    }

    return 0;
}
