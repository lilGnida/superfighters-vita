#include "profile.h"

#include <algorithm>

#include "json.hpp"
#include "platform.h"
#include "progress.h"

using json = nlohmann::json;

namespace {
const char* FILE_NAME = "profile.json";
}

std::map<std::string, std::string> FighterProfile::toGameProfile() const {
	std::map<std::string, std::string> out{{"name", name}};
	for (auto& [slot, item] : apparel)
		if (!item.empty()) out[slot] = item;
	return out;
}

std::vector<std::string> FighterProfile::loadoutPerks() const {
	std::vector<std::string> out = passives;
	for (auto& kv : weaponPerks) out.push_back(kv.second);
	return out;
}

bool FighterProfile::buy(const std::string& id, int cost) {
	if (unlocked.count(id)) return true;
	if (tokens < cost) return false;
	tokens -= cost;
	unlocked.insert(id);
	save();
	return true;
}

void FighterProfile::togglePerk(const std::string& id) {
	Progress& pr = Progress::get();
	if (const Perk* p = pr.perk(id)) {
		if (!p->group.empty()) {
			if (weaponPerks.count(p->group) && weaponPerks[p->group] == id)
				weaponPerks.erase(p->group);
			else
				weaponPerks[p->group] = id;
		} else {
			auto it = std::find(passives.begin(), passives.end(), id);
			if (it != passives.end()) {
				passives.erase(it);
			} else {
				if ((int)passives.size() >= pr.passiveSlots) passives.erase(passives.begin());
				passives.push_back(id);
			}
		}
	} else if (pr.skill(id)) {
		skill = skill == id ? "" : id;
	}
	save();
}

void FighterProfile::newDay(int date) {
	if (dailyDate == date) return;
	dailyDate = date;
	dailyProgress.clear();
	dailyDone.clear();
	save();
}

FighterProfile& FighterProfile::local() {
	static FighterProfile p;
	static bool loaded = false;
	if (!loaded) {
		loaded = true;
		json j = json::parse(platform::readUserFile(FILE_NAME), nullptr, false);
		if (j.is_object()) {
			p.name = j.value("name", p.name);
			if (j.contains("apparel") && j["apparel"].is_object())
				for (auto& [slot, item] : j["apparel"].items())
					if (item.is_string()) p.apparel[slot] = item.get<std::string>();
			p.tokens = j.value("tokens", 0);
			p.earned = j.value("earned", 0);
			p.skill = j.value("skill", std::string());
			p.dailyDate = j.value("dailyDate", 0);
			if (j.contains("unlocked")) p.unlocked = j["unlocked"].get<std::set<std::string>>();
			if (j.contains("weaponPerks")) p.weaponPerks = j["weaponPerks"].get<std::map<std::string, std::string>>();
			if (j.contains("passives")) p.passives = j["passives"].get<std::vector<std::string>>();
			if (j.contains("stats")) p.stats = j["stats"].get<std::map<std::string, int>>();
			if (j.contains("dailyProgress")) p.dailyProgress = j["dailyProgress"].get<std::map<std::string, int>>();
			if (j.contains("dailyDone")) p.dailyDone = j["dailyDone"].get<std::set<std::string>>();
		}
	}
	return p;
}

void FighterProfile::save() const {
	json j;
	j["name"] = name;
	j["apparel"] = apparel;
	j["tokens"] = tokens;
	j["earned"] = earned;
	j["unlocked"] = unlocked;
	j["weaponPerks"] = weaponPerks;
	j["passives"] = passives;
	j["skill"] = skill;
	j["stats"] = stats;
	j["dailyDate"] = dailyDate;
	j["dailyProgress"] = dailyProgress;
	j["dailyDone"] = dailyDone;
	platform::writeUserFile(FILE_NAME, j.dump(1));
}
