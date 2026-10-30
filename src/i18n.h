// Languages: the game's texts are English, tr() looks them up in assets/config/lang_<language>.json
// (a copy in <data folder>/config/ overrides it). What is not in the table stays English.
#pragma once

#include <string>
#include <vector>

namespace i18n {

enum Language { ENGLISH, RUSSIAN, LANGUAGE_COUNT };

void setLanguage(int language);
int language();
const char* languageName(int language);   // in that language

std::string upper(std::string utf8);
std::vector<std::string> letters(const std::string& utf8);

}  // namespace i18n

std::string tr(const std::string& english);
std::string trf(const std::string& english, const std::string& arg);   // with %s in it
