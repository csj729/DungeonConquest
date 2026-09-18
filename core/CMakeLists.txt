# 시뮬레이션 코어. 지금은 헤더 전용이므로 INTERFACE 타깃이고,
# .cpp가 생기면 STATIC으로 승격한다.
add_library(dc_core INTERFACE)
target_include_directories(dc_core INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# 결정론 빌드 플래그.
#   -ffast-math 금지 (CLAUDE.md). 여기서는 반대로 **명시적으로 끈다** —
#   상위 프로젝트가 켜둔 경우를 대비한 방어다.
#   -fno-fast-math / -ffp-contract=off 는 코어에 float가 없어도 걸어둔다:
#   나중에 누가 float를 들여오면 조용히 깨지는 대신 최소한 컴파일러 재량은 막힌다.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(dc_core INTERFACE
        -fno-fast-math
        -ffp-contract=off
        -fno-strict-aliasing
    )
    # -fwrapv는 **일부러 쓰지 않는다.** 그걸 켜면 "이 컴파일러에서는 오버플로가
    # 안전"해져서 MSVC·IL2CPP 빌드에서만 갈라지는 코드를 통과시킨다.
    # 오버플로는 플래그가 아니라 코드로 막고(넓은 타입 승격), UBSan이 검사한다.
endif()

# 테스트 공통 설정
function(dc_add_test name src)
    add_executable(${name} ${src})
    target_link_libraries(${name} PRIVATE dc_core)

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${name} PRIVATE
            -Wall -Wextra -Wpedantic
            -Wconversion -Wsign-conversion   # 암묵적 폭·부호 변환은 결정론의 단골 구멍
            -Wshadow
            -Werror
        )
    endif()

    if(DC_SANITIZE STREQUAL "undefined")
        target_compile_options(${name} PRIVATE
            -fsanitize=undefined,integer-divide-by-zero,shift
            -fno-sanitize-recover=all -fno-omit-frame-pointer -g)
        target_link_options(${name} PRIVATE -fsanitize=undefined)
    elseif(DC_SANITIZE STREQUAL "address")
        target_compile_options(${name} PRIVATE
            -fsanitize=address -fno-omit-frame-pointer -g)
        target_link_options(${name} PRIVATE -fsanitize=address)
    endif()

    add_test(NAME ${name} COMMAND ${name})
endfunction()

dc_add_test(test_fixed     tests/test_fixed.cpp)
dc_add_test(test_rng       tests/test_rng.cpp)
dc_add_test(test_entity_id tests/test_entity_id.cpp)
dc_add_test(test_entity_store tests/test_entity_store.cpp)
dc_add_test(test_world      tests/test_world.cpp)
dc_add_test(test_checksum   tests/test_checksum.cpp)
dc_add_test(test_stat_block tests/test_stat_block.cpp)
dc_add_test(test_condition  tests/test_condition.cpp)

# 헤드리스 체크섬 러너. CI가 Debug/Release 출력을 비교하는 대상이며,
# 서버 리플레이 검증(headless run → checksum) 진입점의 원형이다.
add_executable(dc_checksum tools/dc_checksum.cpp)
target_link_libraries(dc_checksum PRIVATE dc_core)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(dc_checksum PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
