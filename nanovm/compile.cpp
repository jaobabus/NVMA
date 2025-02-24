
#include "utils.hpp"

#include "runtime_compiler.hpp"



struct Arguments
{
    std::string source;
    std::string binary;
};


Arguments parse_args(int argc, char* argv[])
{
    Arguments args;
    auto proc = [&] (char opt, const std::string& value)
    {
        switch (opt) {
        case 'i':
            args.source = value;
            break;

        case 'b':
            args.binary = value;
            break;
        }
    };

    parse_args("i:b:", argc, argv, proc);

    return args;
}


int main(int argc, char* argv[])
{
    try {
        auto args = parse_args(argc, argv);

        if (args.source.size())
        {
            auto source = load_file(args.source);
            auto obj = compile(source);
            std::cout << obj.dump() << std::endl;
        }
        else if (args.binary.size())
        {
            auto binary = load_file(args.binary);
            auto obj = parse_nvma_object(binary);
            auto decompiled = decompile(obj);

            for (auto& line : decompiled)
            {
                std::cout << format_line(line, nullptr, nullptr, {}, false) << std::endl;
            }
        }
        else
        {
            throw std::runtime_error("Must be specified -i <source> or -b <binary>");
        }
    }
    catch (const std::runtime_error& err) {
        std::cerr << "Error: " << err.what() << std::endl;
        return 1;
    }

}
