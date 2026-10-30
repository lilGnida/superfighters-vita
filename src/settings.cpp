#include "settings.h"

#include <algorithm>

#include "i18n.h"
#include "json.hpp"
#include "platform.h"

using json = nlohmann::json;

namespace {
const char* FILE_NAME = "settings.json";
}

Settings& Settings::get() {
	static Settings s;
	return s;
}

void Settings::load() {
	json j = json::parse(platform::readUserFile(FILE_NAME), nullptr, false);
	if (j.is_discarded() || !j.is_object()) return;
	camera = std::max(0, std::min(2, j.value("camera", camera)));
	zoomLimit = std::max(0, std::min(2, j.value("zoomLimit", zoomLimit)));
	music = std::max(0, std::min(10, j.value("music", music)));
	sound = std::max(0, std::min(10, j.value("sound", sound)));
	shake = j.value("shake", shake);
	bars = j.value("bars", bars);
	fullEffects = j.value("fullEffects", fullEffects);
	language = std::max(0, std::min((int)i18n::LANGUAGE_COUNT - 1, j.value("language", language)));
}

void Settings::save() const {
	json j;
	j["camera"] = camera;
	j["zoomLimit"] = zoomLimit;
	j["music"] = music;
	j["sound"] = sound;
	j["shake"] = shake;
	j["bars"] = bars;
	j["fullEffects"] = fullEffects;
	j["language"] = language;
	platform::writeUserFile(FILE_NAME, j.dump(1));
}

void Settings::apply() const {
	platform::setVolumes(music / 10.0f, sound / 10.0f);
	i18n::setLanguage(language);
}

float Settings::maxZoom() const {
	static const float limits[] = {2.0f, 2.75f, 3.5f};
	return limits[zoomLimit];
}
