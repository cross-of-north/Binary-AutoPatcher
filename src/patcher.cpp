#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>
#include <sstream>
#include <iomanip>

#include "minijson_reader.hpp"

using namespace std;
using namespace minijson;
bool force_invalid = false;
typedef struct pattern_s
{
    std::string name;
    uint8_t wildcard = 0;
    uint8_t replace_wildcard = 0;
    struct pattern_s *parent;
    std::vector<uint8_t> patt;
    std::vector<uint8_t> replace;
    std::vector<uint8_t> validate;
    uint64_t max_search = 0;
    uint64_t replace_offset = 0;
    std::vector<struct pattern_s> childs;
} pattern_t;

void search_and_patch_r(string indent, uint8_t* cstart, uint8_t* cend, pattern_t pattern)
{
    const uint8_t wildcard = pattern.wildcard;
    const uint8_t replace_wildcard = pattern.replace_wildcard;
    auto comparer = [&wildcard](uint8_t val1, uint8_t val2)
    {
        return (val1 == val2 || (wildcard && val2 == wildcard));
    };
    auto comparer2 = [&replace_wildcard](uint8_t val, uint8_t val2)
    {
        if (!replace_wildcard)
            return val;
        else if (val == replace_wildcard)
        {
            return val2;
        }

        return val;
    };
    int found = 0;
    for (;;)
    {
        uint8_t* res = std::search(cstart, cend, pattern.patt.begin(), pattern.patt.end(), comparer);
        if (res >= cend)
        {
            if(!found)
                std::cout << indent << "Warning: <" << pattern.name << "> was not found!" << std::endl;
            break;
        }
        found++;
        std::cout << indent << "Found <" << pattern.name << "> at 0x" << std::hex << (res-cstart) << std::endl;
        bool patch = true;
        if (!pattern.validate.empty())
        {
            auto found = std::search(res, res + pattern.validate.size() + 5, pattern.validate.begin(), pattern.validate.end(), comparer);
            if (found >= res + pattern.validate.size() + 5)
            {
                patch = force_invalid;
            }
        }
        if (!pattern.replace.empty())
        {
            if (patch)
            {
                std::cout << indent << "Patching..." << std::endl;
                std::transform(pattern.replace.begin(), pattern.replace.end(), res + pattern.replace_offset, res + pattern.replace_offset, comparer2);
            }
            else
            {
                std::cout << indent << "Can't apply patch! (search region don't pass validation), use -force_invalid to force." << std::endl;
            }
        }
        if (!pattern.childs.empty())
        {
            for (auto child : pattern.childs)
            {
                std::cout << indent << "Scanning SUB:<" << child.name << ">" << std::endl;
                auto end = child.max_search ? res + child.max_search : cend;
                indent += "...";
                search_and_patch_r(indent, res, end, child);
            }
        }
        cstart = res + pattern.patt.size();
    }
}

std::vector<uint8_t> HexToBytes(const std::string& hex_) {
    std::vector<uint8_t> bytes;
    std::string hex(hex_);
    // https://stackoverflow.com/questions/83439/remove-spaces-from-stdstring-in-c
    hex.erase( remove_if( hex.begin(), hex.end(), isspace ), hex.end() );

    for (unsigned int i = 0; i < hex.length(); i += 2) {
        std::string byteString = hex.substr(i, 2);
        uint8_t byte = (uint8_t)strtol(byteString.c_str(), NULL, 16);
        bytes.push_back(byte);
    }

    return bytes;
}

std::vector<uint8_t> StringToBytes(const std::string& str) {
    
    std::string s(str);
    return std::vector<uint8_t> (s.begin(), s.end());
}


bool IsThisStringAHexNumber(std::string const &str)
{
    for (size_t i = 0, n = str.length(); i < n; ++i)
        if (!std::isxdigit(str[i]) && !isspace( str[ i ] ))
            return false;

    return true;
}


pattern_t parse_json(buffer_context &ctx)
{
    pattern_t p;
    parse_object(ctx, [&](const char* k, value v)
    {
        dispatch(k) 
            << "pattern" >> [&] {
                p.childs.push_back(parse_json(ctx));
            }
            << "name" >> [&] {
                p.name = v.as_string();
            }
            << "search" >> [&] { 
                if (IsThisStringAHexNumber(v.as_string()))
                {
                    p.patt = HexToBytes(v.as_string());
                }
                else
                    p.patt = StringToBytes(v.as_string());
            }
            << "replace" >> [&] {
                if (IsThisStringAHexNumber(v.as_string()))
                    p.replace = HexToBytes(v.as_string());
                else
                    p.replace = StringToBytes(v.as_string());
            }
            << "validate" >> [&] {
                if (IsThisStringAHexNumber(v.as_string()))				
                    p.validate = HexToBytes(v.as_string());				
                else
                    p.validate = StringToBytes(v.as_string());
            }
            << "wildcard" >> [&] {
                p.wildcard = (uint8_t)strtol(v.as_string(), NULL, 16);
            }
            << "replace_wildcard" >> [&] {
                p.replace_wildcard = (uint8_t)strtol(v.as_string(), NULL, 16);
            }
            << "replace_offset" >> [&] {
                p.replace_offset = v.as_long();
            }
            << "max_search" >> [&] {
                p.max_search = v.as_long();
            };
    });
    return p;
}

int main(int argc, char* argv[])
{
    string json_file;
    string input_file;
    string output_file;
    for (int i = 1; i < argc; i++)
    {
        string option;
        if (i+1 < argc)
        {
            option = argv[i + 1];
        }
        if (strcmp(argv[i], "-config") == 0)
        {
            if(!option.empty())
                json_file = option;
            else
            {
                cout << "Error: You should pass second parameter to -config! ex: -config myconfig.json";
                return 1;
            }
            continue;
        }
        if (strcmp(argv[i], "-input") == 0)
        {
            if (!option.empty())
                input_file = option;
            else
            {
                cout << "Error: You should pass second parameter to -input! ex: -input file.dll";
                return 1;
            }
            continue;
        }
        if (strcmp(argv[i], "-output") == 0)
        {
            if (!option.empty())
                output_file = option;
            else
            {
                cout << "Error: You should pass second parameter to -output! ex: -output file_patched.dll";
                return 1;
            }
            continue;
        }
        if (strcmp(argv[i], "-force_invalid") == 0)
        {
            force_invalid = true;
        }
    }
    if (input_file.empty() || json_file.empty())
    {
        cout << "Error: To use program, need to enter -input and -config options.";
        return 1;
    }
    if (output_file.empty())
    {
        output_file = input_file + ".patched";
    }
    std::ifstream iStreamJson(json_file);

    if(!iStreamJson.good())
    {
        cout << "Error while opening json file" << std::endl;
        return 1;
    }
    std::string str((std::istreambuf_iterator<char>(iStreamJson)),
        std::istreambuf_iterator<char>());

    buffer_context ctx((char*)str.c_str(), str.length());
    pattern_t head = parse_json(ctx);
    iStreamJson.close();

    ifstream ifs(input_file, std::ios::binary);
    if (ifs.good())
    {
        ifs.seekg(0, std::ios::end);
        std::streamsize clen = ifs.tellg();
        ifs.seekg(0, std::ios::beg);

        std::vector<char> buffer(clen);
        ifs.read(buffer.data(), clen);
        auto start = buffer.data();
        for (auto p : head.childs)
        {
            auto end = p.max_search ? start + p.max_search : start + clen;
            search_and_patch_r("", (uint8_t*)start, (uint8_t*)end, p);
        }			
        ofstream fp;
        fp.open(output_file, ios::out | ios::binary);
        fp.write(buffer.data(), buffer.size());
        fp.close();
    }
    else
    {
        cout << "Error while opening file for patching!" << std::endl;
    }
    ifs.close();
    return 1;
}
