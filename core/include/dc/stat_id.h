// 스탯 식별자. `condition.h`와 `stat_block.h`가 서로를 필요로 하므로
// 공통 부분만 떼어낸다 (condition은 Stat을 읽고, stat_block은 Condition을 담는다).
#ifndef DC_STAT_ID_H
#define DC_STAT_ID_H

#include <cstdint>

#include "fixed.h"

namespace dc {

// 모디파이어 대상 스탯. **뒤에만 덧붙인다** — 값이 바뀌면 리플레이가 깨진다.
// 순서는 `data/stats.json`의 키 순서와 일치해야 하며
// `tools/verify_core_constants.py`가 대조한다.
enum class Stat : uint8_t {
    AttackPower   = 0,
    AttackSpeed   = 1,   // 초당 공격 횟수. 간격(틱)은 여기서 파생된다
    Armor         = 2,
    Range         = 3,
    CorruptionMax = 4,   // 옛 MaxHealth (§2에서 잠식 게이지로 통합)
    CritChance    = 5,
    CritMult      = 6,
    ProcRate      = 7,   // 통합 proc 발동률 (§3)
    MoveSpeed     = 8,
    // **뒤에만 덧붙인다** — 앞 값이 바뀌면 기존 리플레이가 전부 깨진다.
    AoeRadius     = 9,   // 광역기 반경. 아이템 광역 축이 여기로 들어온다
    CcPower       = 10,  // CC 게이지 충전 배율. 아이템 CC 축
    Count         = 11,
};

constexpr uint32_t STAT_COUNT = static_cast<uint32_t>(Stat::Count);
constexpr uint32_t statIndex(Stat s) { return static_cast<uint32_t>(s); }
constexpr uint16_t statBit(Stat s)   { return static_cast<uint16_t>(1u << statIndex(s)); }

// 스탯 비트마스크가 uint16_t 한 칸에 들어가야 한다 — 무효화 역인덱스가 이걸 쓴다.
static_assert(STAT_COUNT <= 16, "스탯이 16종을 넘으면 무효화 마스크 타입을 넓힐 것");

inline const char* statName(Stat s) {
    switch (s) {
        case Stat::AttackPower:   return "attack_power";
        case Stat::AttackSpeed:   return "attack_speed";
        case Stat::Armor:         return "armor";
        case Stat::Range:         return "range";
        case Stat::CorruptionMax: return "corruption_max";
        case Stat::CritChance:    return "crit_chance";
        case Stat::CritMult:      return "crit_mult";
        case Stat::ProcRate:      return "proc_rate";
        case Stat::MoveSpeed:     return "move_speed";
        case Stat::AoeRadius:     return "aoe_radius";
        case Stat::CcPower:       return "cc_power";
        case Stat::Count:         break;
    }
    return "?";
}

// 하한. **밸런스 다이얼이 아니라 불변식이다** — 나눗셈 분모가 0이 되거나 값이
// 음수로 뒤집히는 것을 막는다. `data/stats.json`에서 온다.
struct StatBounds {
    Fixed minValue{};
    Fixed minPctAddSum = Fixed::fromPermille(-1000);
};

}  // namespace dc

#endif  // DC_STAT_ID_H
