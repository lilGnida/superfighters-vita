// The mercenary's base: one building with all its rooms, reached from the main menu. The work panel on
// the right always shows the chosen room; the fighter walks there through the building on their own.
#pragma once

#include <string>

#include "platform.h"

enum class BaseAction { None, Range, Leave };

class BaseScreen {
public:
	BaseAction update(const MenuInput& in);   // one frame: input and drawing
	void comeBackFromRange();
	void openRoom(int room);                  // straight to a room (test option)
	std::string rangeWeapon;                  // what to take into the range

	int selectedRoom() const { return room; }
	int selectedTab() const { return tabs[room]; }
	bool panelActive() const { return focused; }
	float fighterX() const { return x; }
	float fighterY() const { return y; }

private:
	void updateBuilding(const MenuInput& in);
	void updatePanel(const MenuInput& in);
	void walkFighter();
	void selectRoom(int r);
	void selectTab(int dir);
	void resetList();
	void perkAction(const std::string& id, int cost);
	void draw();
	void drawBuilding();
	void drawTop();
	void drawOutfit();
	void drawContracts();
	void drawArsenal();
	void drawPerks();
	void drawSkills();
	void drawRange();
	void drawKeys();

	int room = 0;
	int tabs[4] = {0, 0, 0, 0};    // the tab left open in each room
	bool focused = false;          // the input goes to the panel
	int cursor = 0, listIndex = 0;
	bool perkColumn = false;       // armory: the weapon's perks instead of the weapons
	std::string pendingBuy, message;

	float x = 120, y = 64;         // fighter's feet, in picture pixels
	int facing = 1;
	bool walking = false, climbing = false;
	bool toRange = false;          // goes in on arrival

	int frame = 0;
	BaseAction action = BaseAction::None;
};
