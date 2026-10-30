// The local player's fighter: look, unlocks and loadout, saved between sessions.
#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

struct FighterProfile {
	std::string name = "Player";
	std::map<std::string, std::string> apparel;   // slot -> item ("" / missing = none)

	int tokens = 0, earned = 0;                   // to spend / ever earned (sets the rank)
	std::set<std::string> unlocked;               // perks and skills paid for
	std::map<std::string, std::string> weaponPerks;   // weapon group -> perk
	std::vector<std::string> passives;
	std::string skill;

	std::map<std::string, int> stats;             // lifetime totals
	int dailyDate = 0;                            // day the contracts below belong to
	std::map<std::string, int> dailyProgress;     // contract -> count
	std::set<std::string> dailyDone;

	// game profile map: {"name": ..., slot: item, ...}
	std::map<std::string, std::string> toGameProfile() const;

	std::vector<std::string> loadoutPerks() const;   // everything equipped
	bool buy(const std::string& id, int cost);       // false: not enough tokens
	void togglePerk(const std::string& id);          // equip / unequip, respecting the slots
	void newDay(int date);                           // contracts restart

	static FighterProfile& local();   // loaded on first use
	void save() const;
};
