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

# `data/*.json`의 위치. **도구와 테스트가 진짜 데이터를 읽는다** — 축소판
# 픽스처로 돌면 실제 파일이 로더를 통과하는지는 영영 모른다.
#
# 코어(`dc_core`)에는 걸지 않는다. 코어는 버퍼만 받고 파일을 모른다 —
# 경로를 아는 것은 호스트(도구·테스트) 쪽뿐이라는 레이어 분리가 여기서도 성립한다.
add_compile_definitions(DC_DATA_DIR="${CMAKE_SOURCE_DIR}/data")

# 새니타이저 플래그. **dc_add_test 밖에서도 걸 수 있어야 한다** — 한때 dc_abi와
# test_abi가 이 함수를 쓰지 않는다는 이유로 -DDC_SANITIZE=address를 줘도 플래그가
# 안 붙은 채 "통과"했다. CLAUDE.md는 시뮬 코어가 UBSan/ASan에서 통과할 것을
# 요구하는데, ABI 표면은 핸들 수명과 포인터 노출이 새로 생기는 자리라 특히 봐야 한다.
function(dc_apply_sanitizer target)
    if(DC_SANITIZE STREQUAL "undefined")
        target_compile_options(${target} PRIVATE
            -fsanitize=undefined,integer-divide-by-zero,shift
            -fno-sanitize-recover=all -fno-omit-frame-pointer -g)
        target_link_options(${target} PRIVATE -fsanitize=undefined)
    elseif(DC_SANITIZE STREQUAL "address")
        target_compile_options(${target} PRIVATE
            -fsanitize=address -fno-omit-frame-pointer -g)
        target_link_options(${target} PRIVATE -fsanitize=address)
    endif()
endfunction()

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

    dc_apply_sanitizer(${name})

    add_test(NAME ${name} COMMAND ${name})
endfunction()

dc_add_test(test_fixed     tests/test_fixed.cpp)
dc_add_test(test_json      tests/test_json.cpp)
dc_add_test(test_config_loader tests/test_config_loader.cpp)
dc_add_test(test_rng       tests/test_rng.cpp)
dc_add_test(test_entity_id tests/test_entity_id.cpp)
dc_add_test(test_entity_store tests/test_entity_store.cpp)
dc_add_test(test_world      tests/test_world.cpp)
dc_add_test(test_checksum   tests/test_checksum.cpp)
dc_add_test(test_stat_block tests/test_stat_block.cpp)
dc_add_test(test_condition  tests/test_condition.cpp)
dc_add_test(test_prd        tests/test_prd.cpp)
dc_add_test(test_inventory  tests/test_inventory.cpp)
dc_add_test(test_systems    tests/test_systems.cpp)
dc_add_test(test_qte        tests/test_qte.cpp)
dc_add_test(test_card       tests/test_card.cpp)

# ── C ABI 공유 라이브러리 ───────────────────────────────────────────────
# Unity(C#)와 검증 서버(C#)가 P/Invoke로 부르는 단 하나의 표면.
# **코어는 여전히 독립 INTERFACE 타깃이다** — 이 타깃이 코어에 의존하지 그 반대가
# 아니므로 레이어 분리가 빌드 단위로 강제된다 (CLAUDE.md).
add_library(dc_abi SHARED abi/dc_abi.cpp)
target_link_libraries(dc_abi PRIVATE dc_core)
target_include_directories(dc_abi PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/abi)
target_compile_definitions(dc_abi PRIVATE DC_ABI_BUILD)

# 기본 숨김. **표면에 있어야 할 것만 나가도록** 강제한다 — extern "C"를 빠뜨린
# 심볼이 실수로 내보내지면 C# 쪽에서 우연히 링크되고, 그게 ABI 계약을 조용히 넓힌다.
set_target_properties(dc_abi PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)
dc_apply_sanitizer(dc_abi)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(dc_abi PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror)
endif()

# ABI 계약 테스트 — **순수 C로 빌드한다.** 헤더가 C에서 읽히는지, 그리고 서버
# 진입점이 클라이언트 경로와 같은 체크섬을 내는지를 본다.
add_executable(test_abi tests/test_abi.c)
target_link_libraries(test_abi PRIVATE dc_abi)
set_target_properties(test_abi PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(test_abi PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
dc_apply_sanitizer(test_abi)
add_test(NAME test_abi COMMAND test_abi)

# 헤드리스 체크섬 러너. CI가 Debug/Release 출력을 비교하는 대상이며,
# 서버 리플레이 검증(headless run → checksum) 진입점의 원형이다.
add_executable(dc_checksum tools/dc_checksum.cpp)
target_link_libraries(dc_checksum PRIVATE dc_core)

# PRD 분산 측정 하네스. tools/prd_variance.py의 C++ 이식 —
# 모델이 아니라 **실제 출시 경로**(코어 Rng + q16 절삭)를 측정한다.
add_executable(dc_prd tools/dc_prd.cpp)
target_link_libraries(dc_prd PRIVATE dc_core)

# 조합식 조회 구조 재측정. tools/recipe_structure_bench.cpp를 실제 코어 타입으로.
add_executable(dc_recipe_bench tools/dc_recipe_bench.cpp)
target_link_libraries(dc_recipe_bench PRIVATE dc_core)

# 전장 구성 측정. balance_baseline.py의 미검증 모델 가정 3개를 실측으로 대체한다.
add_executable(dc_field tools/dc_field.cpp)
target_link_libraries(dc_field PRIVATE dc_core)

# 몬테카를로 밸런싱 하네스. 헤드리스 N판 → 클리어율 분포 (§14-11).
add_executable(dc_montecarlo tools/dc_montecarlo.cpp)
target_link_libraries(dc_montecarlo PRIVATE dc_core)

# 보스전 실측 하네스. verify_recovery.py 목표 6의 상수를 만든다 —
# 보스 등장 틱에 월드를 복제해 유입·구슬·흡혈을 갈라 잰다.
add_executable(dc_boss tools/dc_boss.cpp)
target_link_libraries(dc_boss PRIVATE dc_core)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(dc_checksum PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_compile_options(dc_boss PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_compile_options(dc_prd PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_compile_options(dc_recipe_bench PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_compile_options(dc_field PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_compile_options(dc_montecarlo PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
