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
#include "../include/dc/sim_config.h"
#include "dev_items.h"
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
//
// ## 점수 단위는 위력 permille이다
//
// 전에는 카드 종류마다 임의의 상수(100 / 50 + 등급)를 썼다. 그래서 일반 등급
// 공격력 카드(+0.9%)가 영웅 등급 각인(+6%)을 이기는 선택이 나왔고, 아이템 뽑기
// 칸은 아예 0점이라 한 번도 안 골랐다.
//
// 지금은 **전부 같은 단위로 비교**한다 — 성장 카드는 등급 예산, 아이템 뽑기는
// 흔함 등급의 위력이다. 그러면 설계가 의도한 순서가 그대로 나온다:
//
//     일반 9 · 고급 15 · 희귀 30 · **흔함 아이템 52** · 영웅 60 · 전설 150
//
// 즉 3장 중 영웅 이상이 없으면 아이템을 고른다. 그게 위력 배분 70:30을 만든다.
inline int32_t cardPower(const SimConfig& cfg, const CardOffer& c) {
    if (c.kind == CardKind::ItemDraw) return DEV_TIER_POWER[0];   // 흔함
    return c.grade < MAX_CARD_GRADES ? cfg.cardGradeBudget[c.grade] : 0;
}

inline int32_t score(Policy p, const SimConfig& cfg, const CardOffer& c) {
    const int32_t base = cardPower(cfg, c);
    switch (p) {
        case Policy::Greedy:
            return base;                  // 위력만 본다 — 역할을 가리지 않는다
        case Policy::Power:
            // 역할 가중치. 스탯 카드만 역할이 갈리고, 나머지는 중립이다 —
            // 각인·유물의 **효과별 궁합**은 아직 안 본다 (그건 다음 단계다).
            if (c.kind == CardKind::StatBoost) {
                return isPowerStat(c.entryId) ? base * 2 : base / 2;
            }
            return base;
        case Policy::Survival:
            if (c.kind == CardKind::StatBoost) {
                return isSurvivalStat(c.entryId) ? base * 2 : base / 2;
            }
            return base;
        case Policy::Random:
        case Policy::Count:
            break;
    }
    return 0;
}

inline uint32_t choose(Policy p, const SimConfig& cfg, const CardOfferSet& offer, Rng& rng) {
    if (offer.count == 0) return 0;
    if (p == Policy::Random) return rng.range(offer.count);

    uint32_t best = 0;
    int32_t  bestScore = score(p, cfg, offer.offers[0]);
    for (uint32_t i = 1; i < offer.count && i < MAX_CARDS_PER_LEVEL; ++i) {
        const int32_t sc = score(p, cfg, offer.offers[i]);
        if (sc > bestScore) { bestScore = sc; best = i; }
    }
    return best;
}

}  // namespace dc::dev

#endif  // DC_CARD_POLICY_H
