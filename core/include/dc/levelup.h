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

inline void dealCards(World& w, const SimConfig& cfg) {
    w.cards.offer.clear();
    const uint32_t n = cfg.cardsPerLevel < MAX_CARDS_PER_LEVEL
                     ? cfg.cardsPerLevel : MAX_CARDS_PER_LEVEL;
    for (uint32_t i = 0; i < n; ++i) {
        w.cards.offer.offers[w.cards.offer.count++] = rollCard(w, cfg, w.cards.offer);
    }
}

// 레벨업 기본 성장 (§4) — **카드와 별개로 레벨 그 자체가 주는 몫이다.**
//
// 성장 총량은 레벨업당 +7%이고 그중 절반이 공속, 절반이 한 대 피해로 간다.
// 배분이 클리어 시간을 바꾸지는 않는다(처리량 = 공속 × 한 대 피해). 바꾸는 것은
// **잡몹 타수**이고, 타수가 치명타·공격력 성장이 오버킬로 버려지는 정도를 정한다.
//
// 누적 배율을 `PercentAdd`로 싣는다. 이전 레벨분을 빼고 새 레벨분을 더하는
// 방식이라 **레벨이 여러 칸 한 번에 올라도 한 번에 정확히 맞는다** —
// 레벨마다 곱하면 고정소수점 반올림이 레벨 수만큼 누적된다.
inline void applyLevelGrowth(World& w, const SimConfig& cfg, int32_t levelBefore) {
    const Fixed oldSpeed = Fixed::fromPermille(cfg.growthSpeedFor(levelBefore) - 1000);
    const Fixed oldDmg   = Fixed::fromPermille(cfg.growthDamageFor(levelBefore) - 1000);
    const Fixed newSpeed = Fixed::fromPermille(cfg.growthSpeedFor(w.hero.level) - 1000);
    const Fixed newDmg   = Fixed::fromPermille(cfg.growthDamageFor(w.hero.level) - 1000);
    w.hero.stats.removePctAdd(Stat::AttackSpeed, oldSpeed);
    w.hero.stats.addPctAdd(Stat::AttackSpeed, newSpeed);
    w.hero.stats.removePctAdd(Stat::AttackPower, oldDmg);
    w.hero.stats.addPctAdd(Stat::AttackPower, newDmg);
}

// 경험치를 넣고, 필요분을 넘으면 레벨업 + 카드 제시.
// **연속 레벨업도 한 번에 한 화면씩** 처리한다 — 밀린 수는 pendingLevelUps에 쌓인다.
inline void gainExp(World& w, const SimConfig& cfg, int64_t amount) {
    if (amount <= 0) return;
    w.hero.exp += amount;
    const int32_t levelBefore = w.hero.level;
    while (w.hero.exp >= cfg.needFor(w.hero.level)) {
        w.hero.exp -= cfg.needFor(w.hero.level);
        ++w.hero.level;
        ++w.cards.pendingLevelUps;
    }
    if (w.hero.level != levelBefore) applyLevelGrowth(w, cfg, levelBefore);
    if (w.cards.pendingLevelUps > 0 && !w.cards.offer.open()) dealCards(w, cfg);
}

// 선택 적용. 인덱스가 범위를 벗어나면 거부한다 (입력 로그가 오염됐을 수 있다).
inline bool chooseCard(World& w, const SimConfig& cfg, uint32_t index) {
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
    dealCards(w, cfg);
    return true;
}

}  // namespace dc

#endif  // DC_LEVELUP_H
