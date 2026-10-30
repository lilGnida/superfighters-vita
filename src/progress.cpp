#include "progress.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <random>

#include "json.hpp"
#include "platform.h"
#include "profile.h"

using json = nlohmann::json;

namespace {

// a copy in the player's data folder wins; broken files are reported, not fatal
json readConfig(const std::string& name) {
	std::string text = platform::readUserFile("config/" + name);
	if (!text.empty()) {
		json j = json::parse(text, nullptr, false);
		if (!j.is_discarded()) return j;
		platform::log("config/%s in the data folder has a JSON error, using the built-in one", name.c_str());
	}
	json j = json::parse(platform::readFile("config/" + name), nullptr, false);
	if (j.is_discarded()) {
		platform::log("assets/config/%s has a JSON error", name.c_str());
		return json::object();
	}
	return j;
}

int today() {
	std::time_t now = std::time(nullptr);
	std::tm* t = std::localtime(&now);
	return (t->tm_year + 1900) * 10000 + (t->tm_mon + 1) * 100 + t->tm_mday;
}

// n different indices out of count, fixed by the date
std::vector<int> dailyPick(int count, int n, int salt) {
	std::vector<int> all(count);
	for (int i = 0; i < count; i++) all[i] = i;
	std::mt19937 rng((unsigned)today() * 2654435761u + (unsigned)salt);
	for (int i = count - 1; i > 0; i--) std::swap(all[i], all[rng() % (i + 1)]);
	all.resize(std::min(n, count));
	return all;
}

}  // namespace

Progress& Progress::get() {
	static Progress p;
	static bool loaded = false;
	if (!loaded) {
		loaded = true;
		p.load();
	}
	return p;
}

void Progress::load() {
	json j = readConfig("perks.json");
	passiveSlots = j.value("passiveSlots", 2);
	if (j.contains("weaponGroups"))
		for (auto& g : j["weaponGroups"]) {
			WeaponGroup w;
			w.id = g.value("id", "");
			w.name = g.value("name", w.id);
			if (g.contains("weapons")) w.weapons = g["weapons"].get<std::vector<std::string>>();
			groups.push_back(w);
		}
	if (j.contains("branches"))
		for (auto& b : j["branches"]) branches.push_back({b.value("id", ""), b.value("name", "")});
	if (j.contains("perks"))
		for (auto& e : j["perks"]) {
			Perk p;
			p.id = e.value("id", "");
			p.name = e.value("name", p.id);
			p.desc = e.value("desc", "");
			p.group = e.value("group", "");
			p.branch = e.value("branch", "");
			p.cost = e.value("cost", 0);
			if (e.contains("effects"))
				for (auto& [k, v] : e["effects"].items())
					if (v.is_number()) p.effects[k] = v.get<float>();
			perks.push_back(p);
		}

	j = readConfig("skills.json");
	if (j.contains("skills"))
		for (auto& e : j["skills"]) {
			Skill s;
			s.id = e.value("id", "");
			s.name = e.value("name", s.id);
			s.desc = e.value("desc", "");
			s.type = e.value("type", "");
			s.cost = e.value("cost", 0);
			s.cooldown = e.value("cooldown", 1200);
			s.duration = e.value("duration", 0);
			s.amount = e.value("amount", 0.f);
			s.radius = e.value("radius", 0.f);
			skills.push_back(s);
		}

	j = readConfig("progress.json");
	if (j.contains("tokens"))
		for (auto& [k, v] : j["tokens"].items()) tokens[k] = v.get<float>();
	if (j.contains("difficulty")) difficulty = j["difficulty"].get<std::vector<float>>();
	if (j.contains("ranks"))
		for (auto& r : j["ranks"]) ranks.push_back({r.value("name", ""), r.value("tokens", 0)});
	dailyEvents = j.value("dailyEvents", 2);
	dailyContracts = j.value("dailyContracts", 3);
	if (ranks.empty()) ranks.push_back({"Recruit", 0});

	j = readConfig("daily.json");
	if (j.contains("events"))
		for (auto& e : j["events"]) {
			DailyEvent d;
			d.id = e.value("id", "");
			d.name = e.value("name", d.id);
			d.desc = e.value("desc", "");
			d.map = e.value("map", "");
			d.multiplier = e.value("multiplier", 1.f);
			d.mode = e.value("mode", -1);
			events.push_back(d);
		}
	if (j.contains("contracts"))
		for (auto& e : j["contracts"]) {
			Contract c;
			c.id = e.value("id", "");
			c.name = e.value("name", c.id);
			c.stat = e.value("stat", "");
			c.target = e.value("target", 1);
			c.reward = e.value("reward", 0);
			contracts.push_back(c);
		}
}

const Perk* Progress::perk(const std::string& id) const {
	for (auto& p : perks)
		if (p.id == id) return &p;
	return nullptr;
}

const Skill* Progress::skill(const std::string& id) const {
	for (auto& s : skills)
		if (s.id == id) return &s;
	return nullptr;
}

const std::string& Progress::groupOf(const std::string& weaponKey) const {
	static const std::string none;
	for (auto& g : groups)
		if (std::find(g.weapons.begin(), g.weapons.end(), weaponKey) != g.weapons.end()) return g.id;
	return none;
}

const Rank& Progress::rank(int earned) const {
	const Rank* best = &ranks[0];
	for (auto& r : ranks)
		if (earned >= r.tokens) best = &r;
	return *best;
}

std::vector<const DailyEvent*> Progress::todaysEvents() const {
	std::vector<const DailyEvent*> out;
	for (int i : dailyPick((int)events.size(), dailyEvents, 1)) out.push_back(&events[i]);
	return out;
}

std::vector<const Contract*> Progress::todaysContracts() const {
	std::vector<const Contract*> out;
	for (int i : dailyPick((int)contracts.size(), dailyContracts, 2)) out.push_back(&contracts[i]);
	return out;
}

float Progress::multiplierFor(const std::string& map, int mode) const {
	float m = 1;
	for (const DailyEvent* e : todaysEvents())
		if ((e->map.empty() || e->map == map) && (e->mode < 0 || e->mode == mode)) m *= e->multiplier;
	return m;
}

int Progress::award(const std::map<std::string, int>& stats, const std::string& map, int mode, int difficultyLevel) {
	FighterProfile& me = FighterProfile::local();
	me.newDay(today());

	auto count = [&](const char* k) {
		auto it = stats.find(k);
		return it == stats.end() ? 0 : it->second;
	};
	auto rate = [&](const char* k) {
		auto it = tokens.find(k);
		return it == tokens.end() ? 0.f : it->second;
	};
	float base = rate("round") + count("kill") * rate("kill") + count("win") * rate("win") + count("wave") * rate("wave");
	float level = difficultyLevel >= 0 && difficultyLevel < (int)difficulty.size() ? difficulty[difficultyLevel] : 1;
	int total = (int)std::lround(base * level * multiplierFor(map, mode));

	for (const Contract* c : todaysContracts()) {
		if (me.dailyDone.count(c->id)) continue;
		int& done = me.dailyProgress[c->id];
		done += count(c->stat.c_str());
		if (done >= c->target) {
			me.dailyDone.insert(c->id);
			total += c->reward;
		}
	}

	for (auto& kv : stats) me.stats[kv.first] += kv.second;
	me.tokens += total;
	me.earned += total;
	me.save();
	return total;
}
