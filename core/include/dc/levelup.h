// 레벨업과 카드 추첨 (§4).
//
// 카드 3장은 **비복원 추출**이고(같은 화면 중복 금지), 전설은 **획득 비트마스크**로
// 풀이 줄어든다. 고갈되면 그 자리의 등급을 한 단계 내린다 — 9종을 전부 먹는 판은
// 0.075%라 거의 안 밟지만, 규칙이 없으면 그 판에서 터진다.
#ifndef DC_LEVELUP_H
#define DC_LEVELUP_H

#include <cstdint>

#include "card.h"
#include "sim_config.h"
#include "items_apply.h"
#include "world.h"

namespace dc {

// 살아있는 후보를 배열 순서로 세어 k번째를 고른다 (§4 결정론).
// **기각 재시도를 쓰지 않는 이유**: 난수 소비 횟수가 후보 상태에 따라 달라져
// 같은 시드에서도 뒤가 흔들린다.
inline int32_t pickNth(const bool* alive, uint32_t n, uint32_t k) {
    for (uint32_t i = 0; i < n; ++i) {
        if (!alive[i]) continue;
        if (k == 0) return static_cast<int32_t>(i);
        --k;
    }
    return -1;
}

// 등급 하나를 뽑는다. 전설 풀이 비었으면 영웅으로 강등한다.
inline uint8_t rollGrade(World& w, const SimConfig& cfg) {
    int32_t weights[MAX_CARD_GRADES];
    for (uint32_t i = 0; i < MAX_CARD_GRADES; ++i) weights[i] = cfg.cardGradeRate[i];
    uint8_t g = static_cast<uint8_t>(w.rngCards.weighted(weights, MAX_CARD_GRADES));
    if (g == 4 && w.cards.legendLeft(cfg.legendPoolSize) == 0) g = 3;   // 고갈 폴백
    return g;
}

// 카드 한 장. 이미 제시된 것과 겹치지 않게 고른다.
inline CardOffer rollCard(World& w, const SimConfig& cfg, const CardOfferSet& taken) {
    CardOffer c;
    c.grade = rollGrade(w, cfg);

    if (c.grade == 4) {
        // 전설 — 미획득 + 이번 화면 미제시인 것만 후보다.
        bool alive[MAX_LEGEND_POOL];
        uint32_t n = 0;
        for (uint32_t i = 0; i < cfg.legendPoolSize && i < MAX_LEGEND_POOL; ++i) {
            alive[i] = !w.cards.legendHas(i)
                    && !taken.contains(CardKind::Legend, static_cast<uint16_t>(i));
            if (alive[i]) ++n;
        }
        if (n > 0) {
            const int32_t pick = pickNth(alive, cfg.legendPoolSize, w.rngCards.range(n));
            if (pick >= 0) {
                c.kind    = CardKind::Legend;
                c.entryId = static_cast<uint16_t>(pick);
                c.value   = Fixed::fromPermille(cfg.cardGradeStep[4]);
                return c;
            }
        }
        c.grade = 3;   // 이번 화면에 전설이 이미 있으면 강등
    }

    if (c.grade == 0) {
        // 일반 — 기본 스탯. **위력 증가분이 곧 등급 예산이다** (§4).
        // 어떤 스탯인지가 빌드 축을 가른다. 같은 화면 중복은 여기도 적용된다.
        c.kind = CardKind::StatBoost;
        c.value = Fixed::fromPermille(cfg.cardGradeBudget[0]);
        if (cfg.statCardPoolSize == 0) { c.entryId = 0; return c; }
        bool alive[8];
        uint32_t n = 0;
        for (uint32_t i = 0; i < cfg.statCardPoolSize && i < 8; ++i) {
            alive[i] = !taken.contains(CardKind::StatBoost, cfg.statCardPool[i]);
            if (alive[i]) ++n;
        }
        if (n == 0) { c.entryId = cfg.statCardPool[0]; return c; }
        const int32_t pick = pickNth(alive, cfg.statCardPoolSize, w.rngCards.range(n));
        c.entryId = cfg.statCardPool[pick >= 0 ? static_cast<uint32_t>(pick) : 0];
        return c;
    }

    // 고급~영웅 — 공통 각인과 유물을 한 풀로 놓고 배열 순서로 뽑는다.
    // **등급은 종류가 아니라 수치 티어다** — 같은 각인이 등급만 달리 나온다.
    bool alive[MAX_ENGRAVINGS + MAX_RELICS];
    const uint32_t total = cfg.engraveCount + cfg.relicCount;
    uint32_t n = 0;
    for (uint32_t i = 0; i < total; ++i) {
        const bool isEngrave = i < cfg.engraveCount;
        const uint16_t id = static_cast<uint16_t>(isEngrave ? i : i - cfg.engraveCount);
        alive[i] = !taken.contains(isEngrave ? CardKind::Engraving : CardKind::Relic, id);
        if (alive[i]) ++n;
    }
    if (n == 0) { c.kind = CardKind::StatBoost; c.value = Fixed::fromPermille(cfg.cardGradeBudget[c.grade]); return c; }

    const int32_t pick = pickNth(alive, total, w.rngCards.range(n));
    const bool isEngrave = static_cast<uint32_t>(pick) < cfg.engraveCount;
    const uint32_t pickU = static_cast<uint32_t>(pick);
    const uint16_t id = static_cast<uint16_t>(isEngrave ? pickU : pickU - cfg.engraveCount);
    const int32_t base = isEngrave ? cfg.engraveBase[id] : cfg.relicBase[id];
    c.kind    = isEngrave ? CardKind::Engraving : CardKind::Relic;
    c.entryId = id;
    // 수치 = 고급 기준값 × 등급 비. 신규 부착이든 누적이든 같은 값을 더한다.
    c.value   = Fixed::fromPermille(base) * Fixed::fromPermille(cfg.cardGradeStep[c.grade]);
    return c;
}

// 흔함 아이템 하나를 뽑는다. **맨 왼쪽 고정 칸**이 쓰는 경로다 (§4).
//
// 결과는 항상 흔함이다 — "왼쪽은 도박, 오른쪽은 확정"이라는 대비에서 도박은
// 등급이 아니라 **무엇이 나오느냐**다. 조합 재료가 맞아떨어지느냐가 판을 가른다.
inline CardOffer rollItemDraw(World& w, const SimConfig& cfg) {
    CardOffer c;
    c.kind  = CardKind::ItemDraw;
    c.grade = 0;                       // 흔함은 등급 없이 일반 취급 (§4)
    if (cfg.commonPoolSize == 0) { c.kind = CardKind::None; return c; }
    c.entryId = cfg.commonPool[w.rngItems.range(cfg.commonPoolSize)];
    return c;
}

inline void dealCards(World& w, const SimConfig& cfg) {
    w.cards.offer.clear();
    const uint32_t n = cfg.cardsPerLevel < MAX_CARDS_PER_LEVEL
                     ? cfg.cardsPerLevel : MAX_CARDS_PER_LEVEL;

    // **0번은 아이템 뽑기로 고정**이다. 위치를 고정해야 "왼쪽은 도박, 오른쪽은
    // 확정"이라는 대비가 유지된다 (§4). 리롤 대상도 오른쪽 3장뿐이다.
    if (cfg.commonPoolSize > 0) {
        w.cards.offer.offers[w.cards.offer.count++] = rollItemDraw(w, cfg);
    }
    for (uint32_t i = w.cards.offer.count; i < n; ++i) {
        w.cards.offer.offers[w.cards.offer.count++] = rollCard(w, cfg, w.cards.offer);
    }
}

// 경험치를 넣고, 필요분을 넘으면 레벨업 + 카드 제시.
// **연속 레벨업도 한 번에 한 화면씩** 처리한다 — 밀린 수는 pendingLevelUps에 쌓인다.
inline void gainExp(World& w, const SimConfig& cfg, int64_t amount) {
    if (amount <= 0) return;
    // R_GREED(탐욕의 주머니) — 경험치 획득 증가.
    //
    // **전투와 경쟁하지 않는 유일한 축이다** (§4). 다른 유물이 전투력을 올린다면
    // 이쪽은 카드를 더 자주 뽑게 해 **선택지 품질**을 산다. 그래서 단독으로는
    // 약해 보여도 뽑은 카드가 좋으면 복리로 돌아온다.
    //
    // 배율은 `Fixed`가 아니라 정수로 곱한다 — 경험치는 고정소수점 범위(±524,288)를
    // 훨씬 넘는 int64 누적값이라 Fixed로 옮기면 넘친다.
    {
        const Fixed greed = w.cards.relic[relicIndex(RelicId::Greed)];
        if (greed.raw > 0 && amount > 0) {
            amount += (amount * greed.raw) / Fixed::ONE_RAW;
        }
    }
    w.hero.exp += amount;
    while (w.hero.exp >= cfg.needFor(w.hero.level)) {
        w.hero.exp -= cfg.needFor(w.hero.level);
        ++w.hero.level;
        ++w.cards.pendingLevelUps;
    }
    if (w.cards.pendingLevelUps > 0 && !w.cards.offer.open()) dealCards(w, cfg);
}

// 선택 적용. 인덱스가 범위를 벗어나면 거부한다 (입력 로그가 오염됐을 수 있다).
// **RecipeTable을 받는다.** 아이템 뽑기 칸이 인벤토리에 넣어야 하고, 인벤토리는
// 어느 테이블 기준인지 알아야 캐시가 맞다 (Inventory::matches).
inline bool chooseCard(World& w, const SimConfig& cfg, const RecipeTable& table,
                       uint32_t index) {
    if (!w.cards.offer.open() || index >= w.cards.offer.count
        || index >= MAX_CARDS_PER_LEVEL) return false;
    const CardOffer c = w.cards.offer.offers[index];

    switch (c.kind) {
        case CardKind::StatBoost:
            // PercentAdd는 누적합이라 순서 무관이다 (§9).
            if (c.entryId < STAT_COUNT) {
                w.hero.stats.addPctAdd(static_cast<Stat>(c.entryId), c.value);
            }
            break;
        case CardKind::Engraving:
            if (c.entryId < MAX_ENGRAVINGS) w.cards.engrave[c.entryId] += c.value;
            break;
        case CardKind::Relic:
            if (c.entryId < MAX_RELICS) w.cards.relic[c.entryId] += c.value;
            break;
        case CardKind::Legend:
            // **전설은 중첩 자체가 밸런스를 깬다** (§4) — 풀에서 빼고 다시 안 나온다.
            if (c.entryId < MAX_LEGEND_POOL) w.cards.legendTake(c.entryId);
            break;
        case CardKind::ItemDraw:
            // **위력의 주력이 여기서 들어온다.** 흔함 1개를 인벤토리에 넣고
            // 스탯 기여분을 다시 접는다 — 조합으로 상위 등급이 되면 그때 또 접힌다.
            if (!w.inventory.add(table, c.entryId)) return false;
            refreshItemStats(w, cfg);
            break;
        case CardKind::None:
        case CardKind::Count:
            return false;
    }
    if (c.grade < MAX_CARD_GRADES) ++w.cards.takenByGrade[c.grade];

    w.cards.offer.clear();
    if (w.cards.pendingLevelUps > 0) --w.cards.pendingLevelUps;
    if (w.cards.pendingLevelUps > 0) dealCards(w, cfg);
    return true;
}

// 리롤 — **성장 카드 3장을 통째로** 다시 굴린다 (§4).
inline bool rerollCards(World& w, const SimConfig& cfg) {
    if (!w.cards.offer.open()) return false;
    ++w.cards.rerolls;
    // **오른쪽 성장 카드만 다시 굴린다** (§4). 맨 왼쪽 아이템 뽑기 칸은 고정이라
    // 리롤 대상이 아니다 — 어차피 흔함만 나오므로 굴릴 이유도 없다.
    const bool hasItemSlot = w.cards.offer.count > 0
                          && w.cards.offer.offers[0].kind == CardKind::ItemDraw;
    if (hasItemSlot) {
        const CardOffer keep = w.cards.offer.offers[0];
        const uint32_t n = cfg.cardsPerLevel < MAX_CARDS_PER_LEVEL
                         ? cfg.cardsPerLevel : MAX_CARDS_PER_LEVEL;
        w.cards.offer.clear();
        w.cards.offer.offers[w.cards.offer.count++] = keep;
        for (uint32_t i = 1; i < n; ++i) {
            w.cards.offer.offers[w.cards.offer.count++] = rollCard(w, cfg, w.cards.offer);
        }
        return true;
    }
    dealCards(w, cfg);
    return true;
}

}  // namespace dc

#endif  // DC_LEVELUP_H
