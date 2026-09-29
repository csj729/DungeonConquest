# DungeonConquest — 최상위 빌드.
#
# 시뮬 코어는 독립 타깃으로 빌드되며 Unity·서버 어느 쪽에도 의존하지 않는다.
# 레이어 분리는 리뷰가 아니라 **빌드 단위로 강제한다** (CLAUDE.md).
cmake_minimum_required(VERSION 3.16)
# **C도 켠다.** ABI 계약 테스트를 순수 C로 빌드해야 "헤더가 정말 C에서 읽히는가"가
# 주장이 아니라 빌드 결과가 된다 — C++로 테스트하면 헤더에 C++ 타입이 섞여도 통과한다.
project(DungeonConquest LANGUAGES CXX C)

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
