#include "i18n.h"

#include <cctype>
#include <map>

#include "json.hpp"
#include "platform.h"

using json = nlohmann::json;

namespace {
int g_language = i18n::ENGLISH;
std::map<std::string, std::string> g_table;
}

namespace i18n {
namespace {

void loadTable(const char* file) {
	g_table.clear();
	std::string text = platform::readUserFile(std::string("config/") + file);
	if (text.empty()) text = platform::readFile(std::string("config/") + file);
	json j = json::parse(text, nullptr, false);
	if (!j.is_object()) return;
	for (auto& kv : j.items())
		if (kv.value().is_string()) g_table[kv.key()] = kv.value().get<std::string>();
}

}  // namespace

void setLanguage(int language) {
	if (language == g_language) return;
	g_language = language;
	if (language == RUSSIAN)
		loadTable("lang_ru.json");
	else
		g_table.clear();
}

int language() { return g_language; }

const char* languageName(int language) { return language == RUSSIAN ? "Русский" : "English"; }

std::string upper(std::string s) {
	for (size_t i = 0; i < s.size(); i++) {
		unsigned char c = s[i];
		if (c < 0x80) {
			s[i] = (char)toupper(c);
		} else if (c == 0xD0 && i + 1 < s.size()) {     // а..п
			unsigned char d = s[i + 1];
			if (d >= 0xB0 && d <= 0xBF) s[i + 1] = (char)(d - 0x20);
			i++;
		} else if (c == 0xD1 && i + 1 < s.size()) {     // р..я, ё
			unsigned char d = s[i + 1];
			if (d >= 0x80 && d <= 0x8F) {
				s[i] = (char)0xD0;
				s[i + 1] = (char)(d + 0x20);
			} else if (d == 0x91) {
				s[i] = (char)0xD0;
				s[i + 1] = (char)0x81;
			}
			i++;
		}
	}
	return s;
}

std::vector<std::string> letters(const std::string& s) {
	std::vector<std::string> out;
	for (char c : s) {
		if (((unsigned char)c & 0xC0) == 0x80 && !out.empty())
			out.back() += c;
		else
			out.push_back(std::string(1, c));
	}
	return out;
}

}  // namespace i18n

std::string tr(const std::string& english) {
	auto it = g_table.find(english);
	return it == g_table.end() ? english : it->second;
}

std::string trf(const std::string& english, const std::string& arg) {
	std::string s = tr(english);
	size_t at = s.find("%s");
	if (at != std::string::npos) s.replace(at, 2, arg);
	return s;
}
