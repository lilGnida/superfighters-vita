cmake_minimum_required(VERSION 3.13)

# --- PS Vita: configure with the VitaSDK toolchain ---------------------------------------
#   export VITASDK=/usr/local/vitasdk
#   cmake -S . -B build-vita -DCMAKE_TOOLCHAIN_FILE=$VITASDK/share/vita.toolchain.cmake
#   cmake --build build-vita        # produces superfighters.vpk
# --- PC (Linux/macOS/Windows with SDL2 dev packages) ----------------------------------------
#   cmake -S . -B build && cmake --build build && ./build/superfighters

project(superfighters CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# ---- Box2D 2.4 (vendored) --------------------------------------------------------------------
file(GLOB B2_SOURCES
  third_party/box2d/src/collision/*.cpp
  third_party/box2d/src/common/*.cpp
  third_party/box2d/src/dynamics/*.cpp
  third_party/box2d/src/rope/*.cpp)
add_library(box2d STATIC ${B2_SOURCES})
target_include_directories(box2d PUBLIC third_party/box2d/include)
target_include_directories(box2d PRIVATE third_party/box2d/src)
target_compile_options(box2d PRIVATE -w)

# ---- game ------------------------------------------------------------------------------------
add_library(sfcore STATIC
  src/platform.cpp
  src/physics.cpp
  src/registry.cpp
  src/obj.cpp
  src/player.cpp
  src/game.cpp
  src/bot.cpp
  src/nav.cpp
  src/profile.cpp
  src/settings.cpp
  src/i18n.cpp
  src/sfdm.cpp
  src/progress.cpp
  src/base.cpp)
target_include_directories(sfcore PUBLIC third_party src)
target_link_libraries(sfcore PUBLIC box2d)
target_compile_options(sfcore PRIVATE -Wall -Wextra -Wno-unused-parameter)

add_executable(superfighters src/main.cpp)
target_link_libraries(superfighters PRIVATE sfcore)
target_compile_options(superfighters PRIVATE -Wall -Wextra -Wno-unused-parameter)

# headless gameplay test (PC only)
if(NOT VITA)
  add_executable(sf_test tests/gametest.cpp)
  target_link_libraries(sf_test PRIVATE sfcore)
endif()

if(VITA)
  include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

  set(VITA_TITLEID "SFTR00001")
  set(VITA_APP_NAME "Super Fighters")

  # codec libraries exist in the official VitaSDK packages (vdpm); a minimal SDL build
  # with built-in stb decoders has none of them, so link only what is installed.
  set(SF_OPTIONAL_LIBS "")
  foreach(lib vorbisfile vorbis ogg mpg123 FLAC modplug xmp-lite opusfile opus webpdemux webpmux webp sharpyuv png jpeg z)
    find_library(SF_LIB_${lib} ${lib} PATHS $ENV{VITASDK}/arm-vita-eabi/lib NO_DEFAULT_PATH)
    if(SF_LIB_${lib})
      list(APPEND SF_OPTIONAL_LIBS ${lib})
    endif()
  endforeach()

  target_link_libraries(superfighters PRIVATE
    SDL2_image SDL2_mixer SDL2
    ${SF_OPTIONAL_LIBS}
    SceDisplay_stub SceCtrl_stub SceAudio_stub SceAudioIn_stub SceSysmodule_stub
    SceGxm_stub SceCommonDialog_stub SceTouch_stub SceHid_stub SceMotion_stub
    ScePower_stub SceAppMgr_stub SceAppUtil_stub SceProcessmgr_stub SceIofilemgr_stub
    m pthread)

  target_include_directories(sfcore PUBLIC $ENV{VITASDK}/arm-vita-eabi/include/SDL2)

  vita_create_self(superfighters.self superfighters)
  vita_create_vpk(superfighters.vpk ${VITA_TITLEID} superfighters.self
    VERSION "01.00"
    NAME ${VITA_APP_NAME}
    FILE ${CMAKE_SOURCE_DIR}/vita/sce_sys/icon0.png sce_sys/icon0.png
    FILE ${CMAKE_SOURCE_DIR}/assets assets)
else()
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
  pkg_check_modules(SDL2_IMAGE REQUIRED IMPORTED_TARGET SDL2_image)
  pkg_check_modules(SDL2_MIXER REQUIRED IMPORTED_TARGET SDL2_mixer)
  target_link_libraries(sfcore PUBLIC PkgConfig::SDL2 PkgConfig::SDL2_IMAGE PkgConfig::SDL2_MIXER)

  # keep the assets next to the binary
  add_custom_command(TARGET superfighters POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E create_symlink ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:superfighters>/assets)
endif()
