// 카드 선택 정책 = **빌드**.
//
// 몬테카를로 하네스가 "빌드별 클리어율 분포"를 재려면(§14) 서로 다른 선택 정책이
// 있어야 한다. 무작위 선택은 "아무 빌드도 아닌 것"이라 분포가 나오지 않는다.
//
// **core/가 아니라 tools/에 둔다.** 정책은 플레이어의 행동이지 시뮬 규칙이 아니다 —
// 시뮬은 `(틱 번호, 선택지)` 입력만 받는다.
//
// ## 지금 갈리는 축은 스탯 카드뿐이다
//
// 각인·유물의 개별 효과가 아직 구현되지 않았으므로(§14-4 카드 단계의 명시적 범위),
// 그것들을 고르는 정책끼리는 결과가 같다. **실제로 갈리는 건 일반 등급 스탯 카드가
// 어느 스탯에 들어가느냐**다 — 화력(공격력·공속)이냐 생존(방어력·잠식 최대치)이냐.
// 각인 효과가 붙으면 정책을 늘리기만 하면 된다.
#ifndef DC_CARD_POLICY_H
#define DC_CARD_POLICY_H

#include <cstdint>

#include "../include/dc/card.h"
#include "../include/dc/stat_id.h"

namespace dc::dev {

enum class Policy : uint8_t {
    Random   = 0,   // 기준선 — 아무 빌드도 아닌 것
    Power    = 1,   // 화력 집중 (공격력 · 공속)
    Survival = 2,   // 생존 집중 (방어력 · 잠식 최대치)
    Greedy   = 3,   // 등급 최우선 — "높은 등급이 항상 좋다"는 가설
    Count    = 4,
};

inline const char* policyName(Policy p) {
    switch (p) {
        case Policy::Random:   return "무작위";
        case Policy::Power:    return "화력";
        case Policy::Survival: return "생존";
        case Policy::Greedy:   return "등급탐욕";
        case Policy::Count:    break;
    }
    return "?";
}

inline bool isPowerStat(uint16_t stat) {
    return stat == statIndex(Stat::AttackPower) || stat == statIndex(Stat::AttackSpeed);
}
inline bool isSurvivalStat(uint16_t stat) {
    return stat == statIndex(Stat::Armor) || stat == statIndex(Stat::CorruptionMax);
}

// 선호도 점수. 높을수록 먼저 고른다. **동점은 인덱스 오름차순**으로 갈라
// 정책이 결정적이도록 한다.
inline int32_t score(Policy p, const CardOffer& c) {
    switch (p) {
        case Policy::Greedy:
            return c.grade * 10;
        case Policy::Power:
            if (c.kind == CardKind::StatBoost) return isPowerStat(c.entryId) ? 100 : 10;
            return 50 + c.grade;          // 각인·유물은 중립 — 등급으로 가른다
        case Policy::Survival:
            if (c.kind == CardKind::StatBoost) return isSurvivalStat(c.entryId) ? 100 : 10;
            return 50 + c.grade;
        case Policy::Random:
        case Policy::Count:
            break;
    }
    return 0;
}

inline uint32_t choose(Policy p, const CardOfferSet& offer, Rng& rng) {
    if (offer.count == 0) return 0;
    if (p == Policy::Random) return rng.range(offer.count);

    uint32_t best = 0;
    int32_t  bestScore = score(p, offer.offers[0]);
    for (uint32_t i = 1; i < offer.count && i < MAX_CARDS_PER_LEVEL; ++i) {
        const int32_t sc = score(p, offer.offers[i]);
        if (sc > bestScore) { bestScore = sc; best = i; }
    }
    return best;
}

}  // namespace dc::dev

#endif  // DC_CARD_POLICY_H
