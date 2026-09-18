# DungeonConquest — 최상위 빌드.
#
# 시뮬 코어는 독립 타깃으로 빌드되며 Unity·서버 어느 쪽에도 의존하지 않는다.
# 레이어 분리는 리뷰가 아니라 **빌드 단위로 강제한다** (CLAUDE.md).
cmake_minimum_required(VERSION 3.16)
project(DungeonConquest LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)      # GNU 확장 끄기 — 툴체인 간 동작 차이를 줄인다

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# 새니타이저 선택: -DDC_SANITIZE=undefined | address | none
# CLAUDE.md — "시뮬 코어는 UBSan / ASan 빌드에서 테스트를 통과해야 한다."
set(DC_SANITIZE "none" CACHE STRING "undefined | address | none")

enable_testing()
add_subdirectory(core)
