// 조건부 모디파이어의 조건 (§9).
//
// ## 순환 의존을 타입 수준에서 막는다
//
// §9가 금지하는 구조: `MaxHealth`(현 `CorruptionMax`)에 체력 임계 조건을 거는 것.
// `임계 조건 → 현재 비율 → corruption / corruptionMax → CorruptionMax`로 고리가
// 닫힌다. 값이 자기 자신의 조건에 되먹임되는 구조다.
//
// 런타임 검사만 두면 데이터 작성자가 실수했을 때 **조용히 무한루프나 진동**이
// 된다. 그래서 두 겹으로 막는다:
//
//   1. **평가가 재귀할 수 없게 만든다.** 조건 평가는 `ConditionContext`라는
//      평범한 스냅샷 구조체만 읽는다. `StatBlock`으로 가는 포인터가 아예 없으므로
//      "조건 평가 중 스탯을 다시 계산한다"는 코드가 컴파일되지 않는다
//   2. **의미상 진동도 막는다.** 재귀가 없어도, 모디파이어가 자기 조건의
//      분모를 바꾸면 틱마다 켜졌다 꺼졌다 한다. `conditionDependsOn()`이
//      그 조합을 등록 시점에 거부한다
//
// 1이 사고를, 2가 설계 실수를 막는다. 히스테리시스는 **경계 근처의 떨림**을
// 막는 별개 장치이며, 이 순환 문제를 대신하지 못한다.
#ifndef DC_CONDITION_H
#define DC_CONDITION_H

#include <cstdint>

#include "fixed.h"
#include "stat_id.h"

namespace dc {

// 모디파이어 연산 종류 (§9). **뒤에만 덧붙인다** — 값이 바뀌면 리플레이가 깨진다.
enum class ModOp : uint8_t {
    Flat        = 0,
    PercentAdd  = 1,   // 기본값
    PercentMult = 2,   // 지수적으로 폭발하므로 의도적으로 아껴 쓴다
    Override    = 3,
    Count       = 4,
};

enum class ConditionKind : uint8_t {
    Always              = 0,
    CorruptionThreshold = 1,   // 잠식 비율 임계 — 히스테리시스 필수
    TargetArchetype     = 2,   // 타겟이 특정 분류일 때
    TimeWindow          = 3,   // 만료 틱 전까지
    Count               = 4,
};

constexpr uint32_t CONDITION_KIND_COUNT = static_cast<uint32_t>(ConditionKind::Count);
constexpr uint16_t conditionKindBit(ConditionKind k) {
    return static_cast<uint16_t>(1u << static_cast<uint32_t>(k));
}

inline const char* conditionKindName(ConditionKind k) {
    switch (k) {
        case ConditionKind::Always:              return "always";
        case ConditionKind::CorruptionThreshold: return "corruption_threshold";
        case ConditionKind::TargetArchetype:     return "target_archetype";
        case ConditionKind::TimeWindow:          return "time_window";
        case ConditionKind::Count:               break;
    }
    return "?";
}

// **이 조건이 어떤 스탯을 읽는가.** 읽지 않으면 Stat::Count.
//
// 같은 스탯에 이 조건을 거는 것이 곧 순환이다. 표 하나로 정의해두고 등록 시점에
// 대조한다 — 조건 종류를 추가하면 여기도 같이 채워야 컴파일이 통과한다.
constexpr Stat conditionDependsOn(ConditionKind k) {
    switch (k) {
        case ConditionKind::Always:              return Stat::Count;
        case ConditionKind::CorruptionThreshold: return Stat::CorruptionMax;  // 비율의 분모
        case ConditionKind::TargetArchetype:     return Stat::Count;
        case ConditionKind::TimeWindow:          return Stat::Count;
        case ConditionKind::Count:               return Stat::Count;
    }
    return Stat::Count;
}

// 스탯 s에 조건 k를 걸 수 있는가. 코드로 만드는 모디파이어는 static_assert로,
// 데이터로 오는 모디파이어는 등록 함수의 반환값으로 걸린다.
constexpr bool conditionAllowedOn(ConditionKind k, Stat s) {
    return conditionDependsOn(k) != s;
}

struct Condition {
    ConditionKind kind = ConditionKind::Always;

    // CorruptionThreshold: paramA = 활성 임계(permille), paramB = 비활성 임계(permille).
    //   잠식 비율 >= paramA 이면 켜지고, < paramB 이면 꺼진다. 그 사이는 이전 상태 유지.
    //   **paramB <= paramA 여야 한다** (히스테리시스). 등록 시 검사한다.
    // TargetArchetype:     paramA = Archetype 값
    // TimeWindow:          paramA = 만료 틱 (이 틱 전까지 활성)
    int32_t paramA = 0;
    int32_t paramB = 0;
};

// 조건 평가에 필요한 값의 **스냅샷**. StatBlock으로 가는 참조가 없다는 것이
// 이 구조체의 요점이다 — 평가가 스탯 계산으로 되돌아갈 수 없다.
struct ConditionContext {
    int32_t tick               = 0;
    int32_t corruptionPermille = 0;      // corruption / corruptionMax, 0~1000+
    uint8_t targetArchetype    = 0xFF;   // 0xFF = 타겟 없음
};

// 순수 함수. wasActive는 히스테리시스 때문에 필요하다 —
// 경계 사이 구간에서는 이전 상태가 곧 현재 상태다.
constexpr bool evaluateCondition(const Condition& c, const ConditionContext& ctx,
                                 bool wasActive) {
    switch (c.kind) {
        case ConditionKind::Always:
            return true;

        case ConditionKind::CorruptionThreshold:
            // 켜는 선과 끄는 선을 벌려 경계에서의 떨림을 막는다.
            // 떨림은 그 자체로 성능 문제가 아니라 **매 틱 재계산 + 연출 깜빡임**이다.
            if (ctx.corruptionPermille >= c.paramA) return true;
            if (ctx.corruptionPermille <  c.paramB) return false;
            return wasActive;

        case ConditionKind::TargetArchetype:
            return ctx.targetArchetype != 0xFF
                && static_cast<int32_t>(ctx.targetArchetype) == c.paramA;

        case ConditionKind::TimeWindow:
            return ctx.tick < c.paramA;

        case ConditionKind::Count:
            break;
    }
    return false;
}

}  // namespace dc

#endif  // DC_CONDITION_H
