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
#include "../include/dc/inventory.h"
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

// ── 조합 정책 ─────────────────────────────────────────────────
//
// **여기서 처음으로 빌드 경로가 갈린다.** 카드 선택은 주어진 3장 중 고르는 것이라
// 운이 크지만, 조합은 같은 재료로 **어느 축을 탈지** 플레이어가 정한다 — §5 다경로
// 설계가 존재하는 이유이고, "철저한 빌드 설계"의 재미가 사는 자리다.
//
// 정책마다 선호 축이 다르다. 같은 흔함 더미를 받아도 화력형은 화력 경로로,
// 생존형은 생존 경로로 올라간다.
inline int32_t axisPreference(Policy p, uint8_t axis) {
    // 0 화력 · 1 CC · 2 광역 · 3 생존
    switch (p) {
        case Policy::Power:    return axis == 0 ? 3 : (axis == 2 ? 2 : 1);   // 화력 > 광역
        case Policy::Survival: return axis == 3 ? 3 : (axis == 1 ? 2 : 1);   // 생존 > CC
        case Policy::Greedy:   return 1;                                     // 축을 가리지 않는다
        case Policy::Random:
        case Policy::Count:    break;
    }
    return 1;
}

// 지금 만들 수 있는 조합식 중 하나를 고른다. 없으면 -1.
//
// ## 축을 고집하는 것이 곧 빌드다
//
// 처음엔 "등급이 축보다 먼저"로 두고 만들 수 있으면 다 만들게 했다. 그랬더니
// **화력형과 생존형이 똑같은 아이템을 들고 끝났다** (공격력 +32.3% 대 +31.0%) —
// 재료가 들어오는 족족 아무 조합이나 실행하니 축을 고를 여지 자체가 없었다.
//
// 실제 플레이어는 그렇게 하지 않는다. 화력 빌드를 타는 사람은 생존 조합이
// 가능해도 **재료를 아껴 화력 경로를 기다린다.** 그 판단이 §5 다경로 설계가
// 존재하는 이유이고, 여기가 "철저한 빌드 설계"의 자리다.
//
// 그래서 화력형·생존형은 **자기 축(과 곁가지 보너스 축)만** 조합한다.
// 등급탐욕은 축을 가리지 않고 최고 등급만 본다 — 세 정책의 차이가 여기서 난다.
//
// **재료를 아끼는 데는 대가가 있다.** 축을 고집하면 조합 횟수가 줄어 총 위력이
// 낮을 수 있다. 그게 손해인지 이득인지는 하네스가 재야 할 값이지 미리 정할 값이 아니다.
inline bool acceptsAxis(Policy p, uint8_t axis) {
    // 0 화력 · 1 CC · 2 광역 · 3 생존
    switch (p) {
        case Policy::Power:    return axis == 0 || axis == 2;   // 화력 + 광역(보너스)
        case Policy::Survival: return axis == 3 || axis == 1;   // 생존 + CC(보너스)
        case Policy::Greedy:
        case Policy::Random:
        case Policy::Count:    break;
    }
    return true;
}

inline int32_t chooseCraft(Policy p, const Inventory& inv, const RecipeTable& table,
                           Rng& rng) {
    int32_t best = -1;
    int32_t bestScore = -1;
    uint32_t ties = 0;
    for (uint32_t r = 0; r < table.recipeCount(); ++r) {
        if (!inv.craftable(r)) continue;
        const ItemId result = table.recipe(r).result;
        if (result >= DEV_ITEM_COUNT) continue;
        const uint8_t axis = DEV_ITEM_AXIS[result];
        if (!acceptsAxis(p, axis)) continue;          // **축이 아니면 재료를 아낀다**
        // 받아들인 축 안에서는 등급이 전부다 — 사다리가 등급당 2.5배다.
        const int32_t score = p == Policy::Random
            ? 0
            : DEV_ITEM_TIER[result] * 10 + axisPreference(p, axis);
        if (score > bestScore) { bestScore = score; best = static_cast<int32_t>(r); ties = 1; }
        else if (score == bestScore) {
            // **동점은 난수로 가른다** — 인덱스 순으로 고정하면 조합식 정의 순서가
            // 빌드를 정해버려 정책 차이가 묻힌다. 저수지 표집이라 소비가 한 번씩이다.
            ++ties;
            if (rng.range(ties) == 0) best = static_cast<int32_t>(r);
        }
    }
    return best;
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
