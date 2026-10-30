# Super Fighters — C++/SDL2 port (PC + PS Vita), stage 1: offline core

Native C++ rewrite of https://github.com/Ebomb09/SuperFighters-JS (Box2D instead of Matter.js).
Included: maps (.sfm), players, melee/guns/grenades/fire/explosions, one-way platforms, ladders,
weapon drops, round restart, 1–2 local players. Not yet: network multiplayer (stage 2).

## PC build (tested)
    sudo apt install build-essential cmake pkg-config libsdl2-dev libsdl2-image-dev libsdl2-mixer-dev
    cmake -S . -B build && cmake --build build -j
    ./build/superfighters                       # menu
    ./build/superfighters --map warehouse --players 2
    ./build/sf_test                              # headless gameplay tests (SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy)

Keys P1: arrows (Up = jump/climb) + A punch, S kick, D aim (D+A fire, D+S grenade), G grab/throw, C skill, F pick up.
P2: I/K/J/L + U/O/P/H. Enter = select, Esc/Start = pause.
Gamepads (SDL_GameController) use the Vita layout below (A = Cross, X = Square, B = Circle, Y = Triangle).

## PS Vita build
A ready `superfighters.vpk` is shipped next to this archive. It was **compiled but never run on a console**
(no Vita in the build sandbox). The SDK there was assembled by hand, because the official VitaSDK binaries
were not downloadable: Debian `arm-none-eabi` GCC 13.2 + vita-toolchain, vita-headers, vitasdk/newlib,
pthread-embedded and SDL2 2.33 / SDL2_image / SDL2_mixer (stb decoders) built from source. With the official
VitaSDK the same CMake files work unchanged:
    export VITASDK=/usr/local/vitasdk
    cmake -S . -B build-vita -DCMAKE_TOOLCHAIN_FILE=$VITASDK/share/vita.toolchain.cmake
    cmake --build build-vita          # -> build-vita/superfighters.vpk, install with VitaShell
Controls: left stick/D-pad move & climb, Cross jump, Square punch, Circle kick,
Triangle grenade (hold to cook, release to throw), L grab or throw melee, R fire (draws the gun by itself),
right stick aims while you keep walking, Up pick up, Select skill, Start pause.
Log file: ux0:data/superfighters/log.txt — please send it if something fails (crash on start, black screen, no sound...).
Untested spots: sceCtrl mapping, SDL Vita renderer, memory use, OGG decoding, C++ exceptions/RTTI with the Debian libstdc++.

## Known deviations from the browser version
Spawn markers are hidden; spawn order uses shuffle; camera snaps on the first frame; object ids never reused;
dealDamage ignores already-dead objects. Audio converted mp3 -> ogg (tools/convert_audio.sh).
Object definitions were extracted from the JS by tools/dump_defs.mjs into assets/objects.json.

## Maps
The maps are the original Superfighters levels: the original level bitmaps are drawn as the
background and invisible collision (walls, one-way ledges, ladders, slopes) is traced over them in
`tools/levels/build_levels.py` (run it to regenerate `assets/maps/*.sfm`). Map format additions:
`layers` (pictures, optional parallax), `bounds` (camera area), `deathY` (falling out), `acid` ([x0, x1, surface]: burns fighters, dissolves things),
and per-object `size`, `verts` (convex polygon) and `hidden`.
Levels: Warehouse, Backstreets, Factory, Sewers (acid), Rooftops, Containers.
The unfinished maps of the JS port are kept in `tools/levels/js_maps`.

## Game features (stage 1)
- Settings (`src/settings.cpp`, kept in `settings.json` in the data folder, also reachable from the pause menu): camera
  (all fighters / around me / wide view), zoom limit, music and sound volume, screen shake, health bars, effects.
- The main menu leads to the mercenary's base (`src/base.cpp`, art from `tools/make_base.py`): one building with
  wardrobe, armory, gym and shooting range, and a work panel on the right that shows the chosen room. Up / Down pick
  a room (the fighter walks there along the ladder), Left / Right its tab, Cross moves into the panel. The range is
  a real level (picture from `tools/levels/make_range.py`) with targets on rails, left through its door.
- Every round opens with a three second countdown: the fighters stand still, your own one is lit and marked.
- Superfighters Deluxe maps: `.sfdm` files put into `<data folder>/maps/` (`ux0:data/superfighters/maps` on the Vita) appear
  in the map list and are read as they are (`src/sfdm.cpp`): walls, ledges, ladders, spawns, weapons and common objects.
  They are drawn with our own tiles (`tools/make_sfd_tiles.py`); their C# scripts, joints and elevators do not run.
- English and Russian (Settings, `src/i18n.cpp`): the texts are English, `assets/config/lang_ru.json` translates them
  and a copy in `<data folder>/config/` overrides it. The Cyrillic letters of both fonts are in `tools/cyrillic.py`
  (`tools/make_font.py`, `tools/make_base.py`).
- Tokens from rounds buy perks and skills (one perk per weapon group, two fighter perks, one skill on Select).
  Daily events and contracts come from the date. Saved in `profile.json` in the player data folder
  (`ux0:data/superfighters`, `~/.superfighters` on PC).
- All of that lives in `assets/config/*.json`, see `assets/config/README.txt`; a copy in `<data folder>/config/`
  overrides them, no rebuild needed.
- Hits push: bullets knock fighters back by gun strength, shots recoil on the shooter, a rocket carries
  the fighter it hits on a random path before exploding. Falling objects hurt, hard landings daze,
  dead fighters collapse and stay on the map as corpses.
- Close combat: L grabs a fighter in reach (R throws them, 1.2 s or they break free) or, with nobody near,
  throws the melee weapon. Three hits in a row disarm; with nothing to disarm the third floors the target
  and shakes the striker's camera. Jump punch / kick and the dive hit harder the faster you go. Crouching
  ducks under bullets, flying into a wall hurts, fallen fighters drop what they carried.
- Living maps: crates and lamps on chains, glass that cuts rolling fighters, clubs made from wreckage,
  conveyors, spreading fire, chain explosions, crates with weapons and supply drops.
- Modes: free for all, teams, waves; bombs rain after a minute (sudden death, can be turned off).
- Menu: Singleplayer -> map (with preview) -> match setup (bots 0-7, difficulty, local players on PC, music) -> fight.
  Multiplayer is a placeholder for stage 2. Start pauses (resume / restart round / quit to menu).
- Bots (`src/bot.cpp`, `src/nav.cpp`): a navigation graph is built from the level collision on load
  (walk / jump / drop through ledges / walk off edges / ladders / exit doors); bots pick weapons up,
  shoot with reaction time and aim error per difficulty, brawl, throw grenades, break glass, avoid acid.
- Running jump = flat dive with one roll on landing; a fighter it reaches is carried along and thrown down. L lifts fighters, loose objects and corpses, R throws them; aiming with the right stick while walking (legs animate); crosshair;
  an empty gun is thrown when firing and knocks the enemy down; fall damage; emergency exit doors
  (run into the wall at the green exit sign; the held direction keeps working on the way out) move you between floors; loose objects can be pushed; glass panes and
  furniture break; HUD with health, weapons, ammo and round wins; background music.
