// Player options, kept in the data folder (settings.json).
#pragma once

enum CameraMode { CAMERA_FIGHTERS, CAMERA_NEAR_ME, CAMERA_WIDE };

struct Settings {
	int camera = CAMERA_FIGHTERS;
	int zoomLimit = 1;           // 0 far, 1 medium, 2 close
	int music = 7, sound = 10;   // 0..10
	bool shake = true;
	bool bars = true;            // health bars over the fighters
	bool fullEffects = true;     // off: fewer smoke and flame sprites
	int language = 0;            // i18n::Language

	static Settings& get();
	void load();
	void save() const;
	void apply() const;          // volumes, language
	float maxZoom() const;
	int maxParticles() const { return fullEffects ? 48 : 20; }
};
