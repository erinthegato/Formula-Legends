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
  ${ROOT}/src/data.cpp ${ROOT}/src/track.cpp ${ROOT}/src/race.cpp ${ROOT}/src/racecontrol.cpp
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
  stdc++ pthread m c)

vita_create_self(eboot.bin ${PROJECT_NAME} UNSAFE)

# app0:DATA holds the PSP data (shared); app0:HD holds the Vita's higher-detail replacements
set(VPK_FILES
  FILE ${CMAKE_CURRENT_SOURCE_DIR}/sce_sys/icon0.png sce_sys/icon0.png
  FILE ${CMAKE_CURRENT_SOURCE_DIR}/sce_sys/livearea/contents/bg.png sce_sys/livearea/contents/bg.png
  FILE ${CMAKE_CURRENT_SOURCE_DIR}/sce_sys/livearea/contents/startup.png sce_sys/livearea/contents/startup.png
  FILE ${CMAKE_CURRENT_SOURCE_DIR}/sce_sys/livearea/contents/template.xml sce_sys/livearea/contents/template.xml
  FILE ${ROOT}/DATA DATA)
if(EXISTS ${ROOT}/HD)
  list(APPEND VPK_FILES FILE ${ROOT}/HD HD)
endif()
vita_create_vpk(${PROJECT_NAME}.vpk ${VITA_TITLEID} eboot.bin
  VERSION ${VITA_VERSION}
  NAME ${VITA_APP_NAME}
  ${VPK_FILES})
