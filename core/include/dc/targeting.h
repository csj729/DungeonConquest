// 타겟 선정 (§3) — **오토 배틀의 핵심 손잡이.**
//
// 플레이어가 대상을 고르지 않으므로 이 규칙이 곧 밸런싱 다이얼이다.
//
// ## 우선순위는 if 체인이 아니라 데이터 정수다
//
// "보스 > 엘리트 > 잡몹"을 코드에 박으면 두 가지가 막힌다.
//
//   1. **보스·엘리트 동시 존재 시 QTE가 터진다.** 보스를 먼저 때리면 궁병대장이
//      보스전 75초 내내 살아 3초마다 조준을 완성한다 — 구간당 25회로 예산(3~5회)의
//      5배다. 엘리트를 먼저 죽이면 9.5초 만에 끝나 3회다
//   2. **엘리트끼리의 순서를 표현할 수 없다.** 주술사는 QTE 패턴이 없지만
//      소환으로 물량을 불리므로 궁병대장보다 먼저여야 한다. archetype 고정
//      순서로는 이 판단이 들어갈 자리가 없다
//
// 그래서 `data/monsters.json`의 `target_priority` 정수를 엔티티가 들고 다닌다.
// 순서를 바꾸는 일이 코드 수정이 아니라 숫자 하나가 되고,
// `tools/verify_targeting.py`가 그 숫자로 QTE 예산을 검산한다.
//
// ## 정렬 키 — 렉시코그래픽, 전부 정수
//
//     (실효 우선순위 내림차순, 거리제곱 오름차순, EntityId 오름차순)
//
// ## 실효 우선순위 — 거리로 깎는다
//
// 우선순위를 거리와 무관하게 두면 **닿을 수 없는 표적에 영구히 묶인다.** 실측에서
// 영웅이 구간 3~7의 71~88%를 엘리트 조준에 쓰고도 처치율이 0.4마리/초였다 —
// 스폰 반경 19.8타일 밖의 엘리트를 향해 걸어가는 동안 잡몹이 상한까지 쌓였다.
//
//     실효 = 우선순위 - (거리 - 영웅 사거리) × 감쇠      (사거리 안이면 감쇠 0)
//
// 사거리 안에 있는 한 데이터 우선순위가 그대로 산다 — 즉 **"닿는 적 중에서는
// 여전히 엘리트 우선"**이고, 감쇠는 닿지 않는 적만 밀어낸다.
//
// - **비교는 제곱 상태로** 한다 (§11). 제곱근은 감쇠량을 타일 단위로 환산할 때만
//   쓰이고(`isqrt64` — libm이 아니라 자체 정수 구현이다), 정렬 키에는 들어가지 않는다
// - 마지막 키가 EntityId라 **동점이 남지 않는다** — 완전 순서이므로 순회 순서에
//   의존하지 않고, 배열 레이아웃이 바뀌어도 같은 타겟이 나온다
// - 1×N 브루트포스다. N=512에서 SoA로 수 μs이므로 공간 분할이 필요 없다 (§11)
#ifndef DC_TARGETING_H
#define DC_TARGETING_H

#include <cstdint>

#include "entity_store.h"
#include "fixed.h"

namespace dc {

// 실효 우선순위 — 영웅 사거리를 넘어선 만큼 깎는다. **전부 정수 연산이다.**
// 나눗셈 한 번(ONE_RAW로 나누기)은 양수끼리라 반올림 방향이 명확하고,
// 제곱근은 거리 비교가 아니라 감쇠량 계산에만 쓰인다.
inline int32_t effectivePriority(int32_t priority, int64_t distSq,
                                 Fixed heroRange, int32_t falloffPerTile) {
    if (falloffPerTile <= 0) return priority;
    const int64_t d = static_cast<int64_t>(isqrt64(static_cast<uint64_t>(distSq)));
    const int64_t beyond = d - static_cast<int64_t>(heroRange.raw);
    if (beyond <= 0) return priority;          // 사거리 안이면 감쇠 없다
    const int64_t penalty = (beyond * falloffPerTile) / Fixed::ONE_RAW;
    return priority - static_cast<int32_t>(penalty);
}

// 자동 선정 — 데이터 우선순위 규칙. 없으면 invalid.
inline EntityId selectAutoTarget(const EntityStore& e, Fixed heroX, Fixed heroY,
                                 Fixed heroRange, int32_t falloffPerTile) {
    EntityId best      = EntityId::invalid();
    int32_t  bestPrio  = 0;
    int64_t  bestDist  = 0;

    const uint32_t n = e.count();
    for (uint32_t i = 0; i < n; ++i) {
        if (e.deadAt(i)) continue;   // 이번 틱에 죽은 것으로 표시된 행은 건너뛴다

        const int64_t d    = distanceSq(e.posX[i], e.posY[i], heroX, heroY);
        const int32_t prio = effectivePriority(e.targetPriority[i], d,
                                               heroRange, falloffPerTile);
        const EntityId id  = e.idAt(i);

        if (!best.valid()) { best = id; bestPrio = prio; bestDist = d; continue; }
        if (prio > bestPrio)                        { best = id; bestPrio = prio; bestDist = d; continue; }
        if (prio < bestPrio)                        continue;
        if (d < bestDist)                           { best = id; bestDist = d; continue; }
        if (d > bestDist)                           continue;
        if (id < best)                              { best = id; }   // 동점 → 완전 순서
    }
    return best;
}

// 수동 지정을 얹은 최종 선정.
//
// **수동 지정은 기본값을 고치는 장치가 아니라 그 위의 선택이다.** 기본 우선순위가
// 이미 최적(엘리트 우선)이므로 플레이어는 개입할 의무가 없고, 개입은 "방패병 24초를
// 건너뛰고 보스로" 같은 상황 판단일 때만 이득이 된다. 기본값이 손해면 수동 타게팅이
// 의무가 되는데, 오토 배틀러에서 의무적 개입은 피로다.
//
// - **지속된다.** 한 번 클릭이 곧 지시이고, 매 틱 다시 찍지 않는다
// - **대상이 죽으면 자동 해제된다.** stale 핸들은 EntityStore가 걸러주므로
//   따로 검사할 필요 없이 자동 우선순위로 복귀한다
// - 입력은 QTE·카드 선택과 같이 `(틱 번호, EntityId)`로 로그에 남는다 (§10).
//   EntityId가 결정론적 정수라 서버가 그대로 재생할 수 있다
inline EntityId selectTarget(const EntityStore& e, Fixed heroX, Fixed heroY,
                             EntityId manual, Fixed heroRange, int32_t falloffPerTile) {
    if (e.alive(manual)) return manual;   // 수동 지정에는 감쇠를 걸지 않는다 — 플레이어의 지시다
    return selectAutoTarget(e, heroX, heroY, heroRange, falloffPerTile);
}

// 광역기는 우선순위가 없다 (§3) — 범위 안의 모든 적을 때린다.
// dense 인덱스를 **오름차순으로** 채우므로 순회 순서가 배열 순서로 고정된다.
inline uint32_t collectInRadius(const EntityStore& e, Fixed heroX, Fixed heroY,
                                Fixed radius, uint32_t* out, uint32_t outMax) {
    const int64_t r2 = static_cast<int64_t>(radius.raw) * static_cast<int64_t>(radius.raw);
    uint32_t n = 0;
    const uint32_t cnt = e.count();
    for (uint32_t i = 0; i < cnt && n < outMax; ++i) {
        if (e.deadAt(i)) continue;
        if (distanceSq(e.posX[i], e.posY[i], heroX, heroY) <= r2) out[n++] = i;
    }
    return n;
}

}  // namespace dc

#endif  // DC_TARGETING_H
