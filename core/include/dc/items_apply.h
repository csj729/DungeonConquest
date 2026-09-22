// 아이템 → 스탯 반영 (§5) — **위력의 주력이 여기서 전투로 들어온다.**
//
// 한 판 성장의 70%를 아이템 조합이 담당한다 (design.md §4). 그동안 인벤토리는
// World에 있으면서도 시뮬 어디서도 읽히지 않아 아이템의 전투 효과가 0이었다.
//
// ## 왜 매 틱이 아니라 변경 시에만 계산하는가
//
// 보유 아이템은 레벨업(뽑기)과 조합에서만 바뀐다 — 초당 20회가 아니라 판당 수십 회다.
// 그런데 스탯은 매 틱 여러 번 읽힌다. 그래서 **변경 시 한 번 접어 `PercentAdd`로
// 싣는다.** 이전 기여분을 빼고 새 기여분을 더하는 방식이라 아이템이 몇 개가 되든
// 고정소수점 반올림이 누적되지 않는다 (레벨업 성장에서 쓴 것과 같은 규칙).
//
// ## 합이지 곱이 아니다
//
// 같은 아이템 2개가 곱으로 뛰면 조합 사다리("상위 1개 > 하위 2개")가 무너진다 —
// 하위를 쌓는 쪽이 항상 이기게 되어 조합할 이유가 사라진다.
#ifndef DC_ITEMS_APPLY_H
#define DC_ITEMS_APPLY_H

#include <cstdint>

#include "sim_config.h"
#include "world.h"

namespace dc {

// 보유 아이템이 만드는 스탯 기여분(permille)을 스탯별로 합산한다.
inline void itemStatTotals(const World& w, const SimConfig& cfg,
                           int32_t* outPermille, int32_t* outSlowAura) {
    for (uint32_t s = 0; s < STAT_COUNT; ++s) outPermille[s] = 0;
    *outSlowAura = 0;

    const uint32_t n = cfg.itemTypeCount < DC_MAX_ITEM_TYPES_CFG
                     ? cfg.itemTypeCount : DC_MAX_ITEM_TYPES_CFG;
    for (uint32_t id = 0; id < n; ++id) {
        const int32_t held = w.inventory.count(static_cast<ItemId>(id));
        if (held <= 0) continue;
        for (uint32_t s = 0; s < STAT_COUNT; ++s) {
            outPermille[s] += cfg.itemStats[id][s] * held;
        }
        *outSlowAura += cfg.itemSlowAura[id] * held;
    }
}

// 인벤토리가 바뀐 뒤 부른다. **이전 기여분을 빼고 새 기여분을 더한다.**
inline void refreshItemStats(World& w, const SimConfig& cfg) {
    int32_t now[STAT_COUNT];
    int32_t slow = 0;
    itemStatTotals(w, cfg, now, &slow);

    for (uint32_t s = 0; s < STAT_COUNT; ++s) {
        const int32_t prev = w.itemStatApplied[s];
        if (now[s] == prev) continue;
        const Stat st = static_cast<Stat>(s);
        w.hero.stats.removePctAdd(st, Fixed::fromPermille(prev));
        w.hero.stats.addPctAdd(st, Fixed::fromPermille(now[s]));
        w.itemStatApplied[s] = now[s];
    }
    w.hero.slowAura = slow;
}

}  // namespace dc

#endif  // DC_ITEMS_APPLY_H
