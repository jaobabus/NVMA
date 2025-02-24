
#include <future>
#include <iostream>
#include <unistd.h>
#include <vector>
#include <memory>
#include <getopt.h>

#include "runtime_compiler.hpp"
#include "vmop.hpp"
#include "utils.hpp"




std::mutex stdout_mutex;

class AbstractNVMTest
{
public:
    virtual ~AbstractNVMTest() = default;
    virtual std::string get_name() const { return "Unknown"; }
    virtual const NVMAObject& get_binary() const = 0;
    virtual bool check_result(const uint32_t* ram) const = 0;
    virtual void dump_error(const uint32_t* ram) const = 0;
};


class NVMTestFromFile : public AbstractNVMTest
{
public:
    NVMTestFromFile(const std::string& source,
                    const std::string& input,
                    const std::map<std::string, uint32_t>& values)
        : name(source)
    {
        obj = compile(load_file(source));

        if (input.size())
            parse_sections_file(obj, load_file(input));

        for (auto& [key, value] : values) {
            if (key.find('.') == key.npos)
                throw std::runtime_error("Can't set value to section, use <section>.<label>=<value>");
            auto first = key.substr(0, key.find('.'));
            auto second = key.substr(key.find('.') + 1);
            if (NVMAObject::sections_mapping.count(first))
                ref_value32(obj.ram,
                            obj.*NVMAObject::sections_mapping.at(first),
                            second) = value;
            else
                throw std::runtime_error("Unknown section " + first);
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

            const std::string ok_color = "\033[38;5;118m";
            const std::string er_color = "\033[38;5;196m";
            const std::string wn_color = "\033[38;5;184m";
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


std::map<std::string, size_t> stdout_pos_map;
size_t stdout_last_pos = 0;

std::unique_lock<std::mutex> lock_stdout_for_test(const AbstractNVMTest& test)
{
    class StdoutLock : public std::unique_lock<std::mutex>
    {
    public:
        StdoutLock(std::mutex& mtx)
            : std::unique_lock<std::mutex>(mtx)
        {
            std::cout << "\033[s" << std::flush;
        }
        ~StdoutLock()
        {
            if (this->owns_lock()) {
                std::cout << "\033[u" << std::flush;
            }
        }

    };

    auto lock = StdoutLock(stdout_mutex);

    if (not stdout_pos_map.count(test.get_name())) {
        stdout_pos_map.insert(std::make_pair(test.get_name(), ++stdout_last_pos));
    }

    std::cout << "\033[" << stdout_pos_map.at(test.get_name()) << "B" << std::flush;
    return lock;
}


std::array<uint32_t, 32> run_test(const AbstractNVMTest& test, size_t pd)
{
    {
        auto lock = lock_stdout_for_test(test);
        std::cout << "Running test: " << test.get_name() << " ... ";
    }

    const NVMAObject& obj = test.get_binary();
    std::array<uint32_t, 32> ram = {0};
    for (auto& [name, label] : obj.input.labels) {
        std::memcpy((uint8_t*)&ram + label.pos, obj.ram.data.data() + label.pos, 4);
    }

    if (obj.text.data.size())
        execute(ram.data(), obj.text.data.data(), 0, nullptr, nullptr);
    else
        throw std::runtime_error(".text section is empty");

    auto lock = lock_stdout_for_test(test);
    if (test.check_result(ram.data())) {
        std::cout << "Running test: " << test.get_name() << " ... " << std::string(pd, ' ') << "\033[1;38;5;76m" << "PASSED" << "\033[0m" << std::endl;
    }
    else {
        std::cerr << "Running test: " << test.get_name() << " ... " << std::string(pd, ' ') << "\033[1;38;5;160m" << "FAILED" << "\033[0m" << std::endl;
    }

    return ram;
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
    auto proc = [&] (char opt, const std::string& value)
    {
        switch (opt) {
        case 'i': {
            std::string arg = optarg;
            if (arg.find(':') == arg.npos)
                throw std::runtime_error("Expected -i <source>:<input>[:<name>=<value>]*");

            auto source_end = arg.find(':');
            auto input_end = arg.find(':', source_end + 1);

            auto source = arg.substr(0, source_end);
            auto input = arg.substr(source_end + 1, input_end - source_end - 1);
            auto vars_start = input_end;

            Arguments::Source info{source, input};
            for (auto next = vars_start;
                 next != std::string::npos;
                 next = arg.find(':', next + 1))
            {
                auto end = arg.find(':', next + 1);
                auto pair = arg.substr(next + 1, end - next - 1);
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
                    uvalue = std::stoul(value);
                }

                info.values.emplace(std::make_pair(pair.substr(0, eq_pos),
                                                   uvalue));
            }

            args.sources.emplace_back(std::move(info));
        }   break;
        }
    };

    parse_args("i:", argc, argv, proc);

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

    auto max_name_size = 0;
    for (const auto& test : tests)
        max_name_size = std::max<size_t>(max_name_size, test->get_name().size());

    std::vector<std::future<std::pair<AbstractNVMTest*, std::array<uint32_t, 32>>>> futures;
    for (const auto& test : tests)
    {
        futures.push_back(std::async(std::launch::async,
                          [&] () -> std::pair<AbstractNVMTest*, std::array<uint32_t, 32>>
        {
            return std::make_pair(test.get(), run_test(*test, max_name_size - test->get_name().size()));
        }));
    }

    for (auto& future : futures) {
        future.wait();
    }

    std::cerr << std::endl;

    for (auto& future : futures) {
        auto [test, ram] = future.get();
        if (not test->check_result(ram.data())) {
            std::cerr << "Results of test " << test->get_name() << ":" << std::endl;
            test->dump_error(ram.data());
            std::cerr << std::endl;
        }
    }

    return 0;
}
