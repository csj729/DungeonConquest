// 코어의 **구조 상수**. 용량·레이아웃에 관한 값만 둔다.
//
// **밸런스 수치는 여기 오지 않는다.** 그건 `data/*.json`이 단일 진실 원천이고
// C++와 파이썬 검증 도구가 같은 파일을 읽는다 (CLAUDE.md). 여기 있는 값은
// "배열을 몇 칸 잡을 것인가"처럼 컴파일 타임에 정해져야 하는 것뿐이다.
//
// 구분 기준: **바꿨을 때 tools/run_all.py가 반응하면 밸런스 수치다.**
#ifndef DC_CONFIG_H
#define DC_CONFIG_H

#include <cstdint>

namespace dc::config {

// 틱 주파수. 데이터(progression.json의 tick_hz)와 **반드시 같아야 한다.**
// 컴파일 타임 상수로도 필요해서 중복을 감수하는 자리이며, 그 대가로
// `tools/verify_core_constants.py`가 두 값을 대조한다 (run_all.py에 포함).
constexpr int32_t TICK_HZ = 20;

// 엔티티 고정 할당량 (§11 물량 규모).
//   일반 구간 동시 30~60 · 피크 150 · 부하 테스트 목표 512 · 고정 할당 1024
// **동적 할당은 하지 않는다.** 할당자 동작은 플랫폼마다 다르고,
// 할당 실패 경로가 곧 분기 차이 = 서버-클라 불일치 경로다.
constexpr uint32_t MAX_ENTITIES = 1024;

// EntityId의 index 폭이 이걸 담을 수 있어야 한다 (12비트 = 4096칸).
static_assert(MAX_ENTITIES <= (1u << 12), "EntityId의 index 폭을 넘는다");

// 스폰 방향 수 (spawn.json의 directions와 일치). 4방향 균등 배분에 쓴다.
constexpr uint32_t SPAWN_DIRECTIONS = 4;

}  // namespace dc::config

#endif  // DC_CONFIG_H
