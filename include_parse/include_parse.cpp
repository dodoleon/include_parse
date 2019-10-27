#include <iostream>
#include <unordered_set>
#include <filesystem>
#include <stack>
#include <regex>
#include <cassert>
#include <fstream>
#include <sstream>

struct Session
{
	std::unordered_set<std::string> visiting;
	std::unordered_set<std::string> should_skip;
};

struct Preprocess_Result
{
	std::string content;
	bool has_pragma_once;
};

class Working_Directory_File_Guard {
public:
	Working_Directory_File_Guard(std::filesystem::path file)
	{
		auto p = file.parent_path();
		working_directories.emplace(p.generic_string());
	}
	~Working_Directory_File_Guard()
	{
		working_directories.pop();
	}
private:
	std::stack<std::string> working_directories;
};

auto strip_pragma_once(std::string const& source, bool& has_pragma_once) -> std::string
{
	static std::regex pragma_once_directive_re{ R"(^\s*#pragma\s+once\b)" };
	auto result = std::regex_replace(source, pragma_once_directive_re, "");
	has_pragma_once = (result.size() != source.size());
	return result;
}

auto strip_block_comments(std::string const& source) -> std::string
{
	static std::regex block_comment_re{ R"(/\*[^]*?\*/)" };
	return std::regex_replace(source, block_comment_re, "");
}

auto strip_line_comments(std::string const& source) -> std::string
{
	static std::regex line_comment_re{ R"(//.*)" };
	return std::regex_replace(source, line_comment_re, "");
}

auto strip_comments(std::string const& source) -> std::string
{
	return strip_line_comments(strip_block_comments(source));
}

auto runtime_path_from_include_path(std::string const& path) -> std::filesystem::path
{
	assert(path.size() >= 2);

	auto stripped_path = path.substr(0, path.size() - 1);
	stripped_path[0] = '/';
	auto first_pos = (path[0] == '<' ? 0 : 1);
	return std::filesystem::path{ &stripped_path[first_pos] };
}


auto slurp(const std::string& path) -> std::string
{
	if (std::ifstream ifs{ path, std::ios::binary }) {
		std::stringstream ss;
		ss << ifs.rdbuf();
		return ss.str();
	}
	else {
		throw std::runtime_error{ "Cannot load file: " + std::string{path} };
	}
}

auto do_preprocess(Session& session, std::filesystem::path filename, std::string source) -> Preprocess_Result
{
	Working_Directory_File_Guard wdfg{ filename };

	bool has_pragma_once;
	source = strip_pragma_once(source, has_pragma_once);

	if (session.visiting.count(filename.generic_string()) > 0) {
		if (has_pragma_once) {
			return {
				"",
				has_pragma_once,
			};
		}
		else {
			throw std::runtime_error{ "Cyclic inclusion: " + filename.generic_string() };
		}
	}
	session.visiting.emplace(filename.generic_string());

	source = strip_comments(source);

	auto include_directive_re = std::regex{ R"(#include\s*("[^"]+"|<[^>]*>))" };
	std::string const empty;
	std::cmatch match;
	auto pos = std::string::size_type(0);
	while (std::regex_search(source.data() + pos, match, include_directive_re)) {
		auto include_path = match.str(1);
		auto runtime_path = runtime_path_from_include_path(include_path);
		auto included_source = slurp(runtime_path.generic_string());

		auto should_skip = (session.should_skip.count(runtime_path.generic_string()) > 0);
		if (!should_skip) session.should_skip.emplace(runtime_path.generic_string());

		auto included_result = do_preprocess(session, std::move(runtime_path), std::move(included_source));
		auto& replacement = (
			included_result.has_pragma_once && should_skip
			? empty
			: included_result.content
			);

		auto match_pos = match[0].first - source.data();
		auto match_len = match[0].second - match[0].first;
		source.replace(match_pos, match_len, replacement);
		pos = match_pos + replacement.size();
	}

	session.visiting.erase(filename.generic_string());

	// Generate include guard if `#pragma once` is used
	// so that preprocessed pieces can be freely concatenated afterwards.
	if (has_pragma_once) {
		static auto non_identifier_re = std::regex{ R"([^a-zA-Z0-9]+)" };
		static auto underscore_at_ends_re = std::regex{ R"(^_+|_+$)" };

		auto path = filename.generic_string();

		auto path_safe = std::regex_replace(path, non_identifier_re, "_");
		path_safe = path_safe.substr(0, 128);       // macro preprocessor has length limit on names
		path_safe = std::regex_replace(path_safe, underscore_at_ends_re, "");

		auto path_hash = std::hash<std::string>{}(path);
		auto guard_macro = "NAMA_INCLUDE_GUARD_" + path_safe + "_" + std::to_string(path_hash);

		auto old_source = std::move(source);
		source.clear();         // Assuming moved string is empty is undefined behavior. Should clear() even after move.
		source += "#ifndef ";
		source += guard_macro;
		source += '\n';
		source += "#define ";
		source += guard_macro;
		source += '\n';
		source += old_source;
		if (source.back() != '\n') source += '\n';
		source += "#endif // ";
		source += guard_macro;
		source += '\n';
	}

	return {
		std::move(source),
		has_pragma_once,
	};
}

int main()
{
	auto source = slurp("a.glsl");
	Session session;
	auto  result = do_preprocess(session, { "a.glsl" }, source);
	std::cout << result.content << std::endl;
	std::cout << "Hello World!\n";
	return 0;
}

// Run program: Ctrl + F5 or Debug > Start Without Debugging menu
// Debug program: F5 or Debug > Start Debugging menu

// Tips for Getting Started: 
//   1. Use the Solution Explorer window to add/manage files
//   2. Use the Team Explorer window to connect to source control
//   3. Use the Output window to see build output and other messages
//   4. Use the Error List window to view errors
//   5. Go to Project > Add New Item to create new code files, or Project > Add Existing Item to add existing code files to the project
//   6. In the future, to open this project again, go to File > Open > Project and select the .sln file
