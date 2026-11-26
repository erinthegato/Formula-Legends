# Formula Legends - PS Vita build (VitaSDK + vitaGL).
#   cmake -S vita -B build-vita && cmake --build build-vita -j4   ->  build-vita/FormulaLegends.vpk
# The same game code as the PSP build; only src/platform_vita.cpp is Vita-specific.
cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
    message(FATAL_ERROR "Please define VITASDK to point to your SDK path!")
  endif()
endif()

project(FormulaLegends CXX C)
include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

set(VITA_APP_NAME "Formula Legends")
set(VITA_TITLEID  "FLEG00001")
set(VITA_VERSION  "01.00")
set(VITA_MKSFOEX_FLAGS "${VITA_MKSFOEX_FLAGS} -d ATTRIBUTE2=12")   # extended memory (more RAM for the game)

set(ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS} -O2 -Wall -ffast-math -fno-strict-aliasing -mcpu=cortex-a9 -mfpu=neon")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -Wall -ffast-math -fno-strict-aliasing -mcpu=cortex-a9 -mfpu=neon -fno-exceptions -fno-rtti")

add_executable(${PROJECT_NAME}
  ${ROOT}/src/data.cpp ${ROOT}/src/track.cpp ${ROOT}/src/race.cpp ${ROOT}/src/racecontrol.cpp ${ROOT}/src/launch.cpp ${ROOT}/src/reliability.cpp
  ${ROOT}/src/champ.cpp ${ROOT}/src/career.cpp ${ROOT}/src/settings.cpp ${ROOT}/src/menu.cpp
  ${ROOT}/src/sound.cpp ${ROOT}/src/render.cpp ${ROOT}/src/render_fx.cpp ${ROOT}/src/clip.cpp
  ${ROOT}/src/game.cpp ${ROOT}/src/intro.cpp ${ROOT}/src/trophies.cpp ${ROOT}/src/setups.cpp ${ROOT}/src/platform_vita.cpp)
target_include_directories(${PROJECT_NAME} PRIVATE ${ROOT}/src)
target_compile_definitions(${PROJECT_NAME} PRIVATE PLATFORM_VITA=1)

# every fopen / remove goes through the path mapper in platform_vita.cpp
target_link_options(${PROJECT_NAME} PRIVATE -Wl,--wrap=fopen,--wrap=remove)
target_link_libraries(${PROJECT_NAME}
  vitaGL vitashark SceShaccCgExt mathneon taihen_stub
  SceShaccCg_stub SceKernelDmacMgr_stub SceGxm_stub SceDisplay_stub SceCommonDialog_stub
  SceAppMgr_stub SceAppUtil_stub SceIme_stub SceSysmodule_stub SceCtrl_stub SceTouch_stub
  SceMotion_stub ScePower_stub SceAudio_stub SceIofilemgr_stub
  z stdc++ pthread m c)

vita_create_self(eboot.bin ${PROJECT_NAME} UNSAFE)

# app0:DATA holds the PSP data (shared); app0:HD holds the Vita's higher-detail replacements.
# Packed by tools/make_vpk.py rather than vita_create_vpk: data that is already compressed goes in
# as it is (no unzipping while installing), and the music is left out. It comes in its own pack,
# FormulaLegends-music.zip (tools/make_music_pack.py), installed once to ux0:data/FormulaLegends/,
# so a game update installs in seconds.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
separate_arguments(SFO_FLAGS UNIX_COMMAND "${VITA_MKSFOEX_FLAGS}")
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/param.sfo
  COMMAND ${VITA_MKSFOEX} ${SFO_FLAGS} -s TITLE_ID=${VITA_TITLEID} -s APP_VER=${VITA_VERSION}
          "${VITA_APP_NAME}" ${CMAKE_CURRENT_BINARY_DIR}/param.sfo
  VERBATIM)
set(VPK_PAIRS
  ${CMAKE_CURRENT_SOURCE_DIR}/sce_sys/icon0.png=sce_sys/icon0.png
  ${CMAKE_CURRENT_SOURCE_DIR}/sce_sys/livearea=sce_sys/livearea
  ${ROOT}/DATA=DATA)
if(EXISTS ${ROOT}/HD)
  list(APPEND VPK_PAIRS ${ROOT}/HD=HD)
endif()
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.vpk
  COMMAND ${Python3_EXECUTABLE} ${ROOT}/tools/make_vpk.py ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.vpk
          ${CMAKE_CURRENT_BINARY_DIR}/eboot.bin.out ${CMAKE_CURRENT_BINARY_DIR}/param.sfo ${VPK_PAIRS}
          --skip "DATA/music/*.emu" --keep "DATA/music/intro.emu" --keep "DATA/music/hq_*.emu"
  DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/eboot.bin.out ${CMAKE_CURRENT_BINARY_DIR}/param.sfo ${ROOT}/tools/make_vpk.py
  VERBATIM)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}-music.zip
  COMMAND ${Python3_EXECUTABLE} ${ROOT}/tools/make_music_pack.py ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}-music.zip ${ROOT}/DATA/music
  DEPENDS ${ROOT}/tools/make_music_pack.py
  VERBATIM)
add_custom_target(${PROJECT_NAME}.vpk-target ALL
  DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.vpk ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}-music.zip)
# (eboot.bin.out is made by vita_create_self's target: eboot.bin-self, or eboot.bin before CMake 3.20)
foreach(t eboot.bin-self eboot.bin)
  if(TARGET ${t})
    add_dependencies(${PROJECT_NAME}.vpk-target ${t})
  endif()
endforeach()
