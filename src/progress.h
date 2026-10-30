// Perks, skills, tokens and daily events. Everything comes from assets/config/*.json;
// see assets/config/README.txt.
#pragma once

#include <map>
#include <string>
#include <vector>

struct Perk {
	std::string id, name, desc, group, branch;   // group: weapon perk, branch: fighter perk
	int cost = 0;
	std::map<std::string, float> effects;
};

struct Skill {
	std::string id, name, desc, type;
	int cost = 0, cooldown = 0, duration = 0;
	float amount = 0, radius = 0;
};

struct WeaponGroup {
	std::string id, name;
	std::vector<std::string> weapons;
};

struct Branch {
	std::string id, name;
};

struct DailyEvent {
	std::string id, name, desc, map;
	float multiplier = 1;
	int mode = -1;   // -1 = any
};

struct Contract {
	std::string id, name, stat;
	int target = 1, reward = 0;
};

struct Rank {
	std::string name;
	int tokens = 0;
};

class Progress {
public:
	static Progress& get();   // reads the configuration on first use

	std::vector<Perk> perks;
	std::vector<Skill> skills;
	std::vector<WeaponGroup> groups;
	std::vector<Branch> branches;
	std::vector<DailyEvent> events;
	std::vector<Contract> contracts;
	std::vector<Rank> ranks;
	int passiveSlots = 2, dailyEvents = 2, dailyContracts = 3;
	std::map<std::string, float> tokens;
	std::vector<float> difficulty;

	const Perk* perk(const std::string& id) const;
	const Skill* skill(const std::string& id) const;
	const std::string& groupOf(const std::string& weaponKey) const;
	const Rank& rank(int earned) const;

	// today's picks, the same all day
	std::vector<const DailyEvent*> todaysEvents() const;
	std::vector<const Contract*> todaysContracts() const;
	float multiplierFor(const std::string& map, int mode) const;

	// after a round: pays the player and advances the contracts; returns the tokens earned
	int award(const std::map<std::string, int>& stats, const std::string& map, int mode, int difficultyLevel);

private:
	void load();
};
