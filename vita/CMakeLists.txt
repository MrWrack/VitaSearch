cmake_minimum_required(VERSION 3.10)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
    message(FATAL_ERROR "Please define VITASDK to point to your SDK path")
  endif()
endif()

project(VitaSearch C)
include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

set(VITA_APP_NAME "VitaSearch")
set(VITA_TITLEID "VSRCH0001")
set(VITA_VERSION "00.99")

add_executable(${PROJECT_NAME}
  src/main.c
  src/net.c
  src/config.c
  src/spotify.c
  src/settings.c
  src/spotify_status.c
)

target_include_directories(${PROJECT_NAME} PRIVATE src)

target_link_libraries(${PROJECT_NAME}
  vita2d
  curl
  ssl
  crypto
  -Wl,--whole-archive
  pthread
  -Wl,--no-whole-archive
  png
  jpeg
  z
  m
  SceCtrl_stub
  SceTouch_stub
  SceDisplay_stub
  SceGxm_stub
  SceSysmodule_stub
  SceNet_stub
  SceNetCtl_stub
  SceHttp_stub
  SceSsl_stub
  SceCommonDialog_stub
  SceIme_stub
  SceAppUtil_stub
  SceAppMgr_stub
  ScePgf_stub
  ScePvf_stub
)

vita_create_self(${PROJECT_NAME}.self ${PROJECT_NAME} UNSAFE)

vita_create_vpk(${PROJECT_NAME}.vpk ${VITA_TITLEID} ${PROJECT_NAME}.self
  VERSION ${VITA_VERSION}
  NAME ${VITA_APP_NAME}
)
