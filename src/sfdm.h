// Superfighters Deluxe maps (.sfdm) from the player's data folder: the game reads them as they are.
// Their layout becomes an ordinary map; what they look like is drawn from our own tiles, and their C# scripts do not run.
#pragma once

#include <string>

#include "platform.h"

namespace sfdm {

struct Loaded {
	std::string mapText;       // for GameOptions::mapText
	Texture* art = nullptr;    // the level picture; shared: valid until the next compose()
	float width = 0, height = 0;
	int players = 0, solids = 0;
};

// what the map contains, without any drawing
Loaded parse(const std::string& bytes, const std::string& title);
// the same with the picture
Loaded load(const std::string& bytes, const std::string& title);

std::string titleOf(const std::string& fileName);   // 4917_Hotel_II.sfdm -> Hotel II

}  // namespace sfdm
