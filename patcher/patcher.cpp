// patcher.cpp : Этот файл содержит функцию "main". Здесь начинается и заканчивается выполнение программы.
//

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

typedef struct pattern_s
{
	std::string name;
	uint8_t wildcard = 0;
	uint8_t replace_wildcard = 0;
	struct pattern_s *parent;
	std::vector<uint8_t> patt;
	std::vector<uint8_t> replace;
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
	auto neg_comparer = [&replace_wildcard](uint8_t val)
	{
		if (!replace_wildcard)
			return true;

		return (val != replace_wildcard);
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
		if (pattern.replace.size())
		{
			std::cout << indent << "Patching..." << std::endl;
			std::copy_if(pattern.replace.begin(), pattern.replace.end(), res + pattern.replace_offset, neg_comparer);
		}
		if (pattern.childs.size())
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

std::vector<uint8_t> HexToBytes(const std::string& hex) {
	std::vector<uint8_t> bytes;

	for (unsigned int i = 0; i < hex.length(); i += 2) {
		std::string byteString = hex.substr(i, 2);
		uint8_t byte = (uint8_t)strtol(byteString.c_str(), NULL, 16);
		bytes.push_back(byte);
	}

	return bytes;
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
				p.patt = HexToBytes(v.as_string());
			}
			<< "replace" >> [&] {
				p.replace = HexToBytes(v.as_string());
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
	if (argc != 3)
	{
		cout << "Usage: patcher.exe <file_to_patch> <patch_data.json>";
		return 0;
	}
	// read a JSON file
	std::ifstream json_file(argv[2]);

	if(!json_file.good())
	{
		cout << "Error while opening json file" << std::endl;
		return 1;
	}
	std::string str((std::istreambuf_iterator<char>(json_file)),
		std::istreambuf_iterator<char>());

	buffer_context ctx((char*)str.c_str(), str.length());
	pattern_t head = parse_json(ctx);
	json_file.close();

	ifstream ifs(argv[1], std::ios::binary);
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
		fp.open(std::string(argv[1]).append(".patched"), ios::out | ios::binary);
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
