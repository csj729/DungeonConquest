// **임시 틱 드라이버.** 아직 시스템이 없는 영역(카드·아이템 획득)만 흉내낸다.
//
// Spawn / Targeting / Combat이 붙으면서 이 파일의 대부분이 없어졌다 —
// 이제 실제 틱은 `stepWorld()`가 돌리고, 여기 남은 건 §14-9 이후에 구현될
// 카드 선택·아이템 획득 경로를 체크섬이 덮도록 무작위로 흔드는 부분뿐이다.
//
// **core/ 안이 아니라 tools/ 안에 두는 이유**: 시뮬 코어에 들어가면 삭제 시점을
// 놓친다. 여기 있으면 디렉터리 하나만 지우면 된다.
#ifndef DC_DEV_SCRIPT_H
#define DC_DEV_SCRIPT_H

#include "../include/dc/item.h"
#include "../include/dc/sim.h"
#include "../include/dc/world.h"
#include "data_files.h"

namespace dc::dev {

// 런 시작 설정. 설정과 조합 테이블은 `data/*.json`에서 온다.
//
// 한때 여기서 인벤토리를 **축소 테이블(조합식 12개)로 다시 초기화**했다.
// 그러면 인벤토리가 든 해시와 `applyInput`에 넘기는 실제 테이블의 해시가
// 달라 `Inventory::matches`가 걸러내고, **아이템 뽑기 카드 선택이 전부
// 조용히 거부됐다** — 체크섬 드라이버가 그 경로를 한 번도 지나지 않은 것이다.
// 로더가 붙으면서 축소 테이블 자체가 필요 없어졌다.
inline void setup(World& w, const SimConfig& cfg, const RecipeTable& table,
                  const HeroBaseline& hero) {
    w.bindDataHash(cfg.dataHash);
    w.inventory.init(table);
    w.hero.stats.init(hero.bases, hero.bounds);
    w.refreshAllConditions();
}

// 한 틱 — 실제 시스템 + 아직 없는 시스템의 자리만 흔든다.
inline void scriptTick(World& w, const SimConfig& cfg, const RecipeTable& table) {
    // 레벨업 카드 선택 — 실제 시스템. 프레젠테이션이 붙기 전까지 무작위로 고른다.
    // **선택 자체가 빌드를 만드는 지점**이므로 몬테카를로 하네스는 여기에 정책을 꽂는다.
    if (w.cards.offer.open()) {
        InputEvent e;
        e.tick  = w.tickCount();
        e.kind  = InputKind::CardChoice;
        e.value = w.rngEvents.range(w.cards.offer.count);
        (void)applyInput(w, cfg, table, e);
    }

    // 아직 시스템이 없는 자리 — 카드 밖의 모디파이어 획득 경로를 흔든다
    if (w.rngCards.chancePermille(10)) {
        const uint32_t src = w.allocSourceId();
        const Stat s  = static_cast<Stat>(w.rngCards.range(STAT_COUNT));
        const Fixed pm = Fixed::fromPermille(static_cast<int32_t>(w.rngCards.range(200)));
        switch (w.rngCards.range(5)) {
            case 0: w.hero.stats.addFlat(s, Fixed::fromRaw(static_cast<int32_t>(w.rngCards.range(4096)))); break;
            case 1: w.hero.stats.addPctAdd(s, pm); break;
            case 2: (void)w.hero.stats.addMult(s, src, pm); break;
            case 3: {
                Condition c;
                c.kind   = ConditionKind::CorruptionThreshold;
                c.paramA = 500 + static_cast<int32_t>(w.rngCards.range(300));
                c.paramB = c.paramA - static_cast<int32_t>(w.rngCards.range(100));
                (void)w.hero.stats.addConditional(s, src, ModOp::PercentAdd, pm, c, w.conditionContext());
                break;
            }
            default: {
                Condition c;
                c.kind   = ConditionKind::TimeWindow;
                c.paramA = w.tickCount() + 20 + static_cast<int32_t>(w.rngCards.range(200));
                (void)w.hero.stats.addConditional(s, src, ModOp::Flat, pm, c, w.conditionContext());
                break;
            }
        }
    }

    // 아이템 자리 (§5) — **뽑기는 흔함만 나온다.** 풀도 데이터에서 온다:
    // 한때 `range(9)`로 흔함 수를 코드에 박아 두었는데, 그러면 items.json에
    // 흔함을 더해도 드라이버는 앞 9종만 뽑는다.
    if (cfg.commonPoolSize > 0 && w.rngItems.chancePermille(300)) {
        const uint32_t pick = w.rngItems.range(cfg.commonPoolSize);
        (void)w.inventory.add(table, cfg.commonPool[pick], 1);
    }
    for (uint32_t r = 0; r < table.recipeCount(); ++r) {
        if (w.inventory.craftable(r)) { (void)w.inventory.craft(table, r); break; }
    }

    // 수동 타게팅 자리 (§3) — 플레이어 입력이 붙기 전까지 가끔 지시를 흉내낸다.
    // 입력 로그가 생기면 (틱 번호, EntityId)로 기록된다.
    if (w.rngEvents.chancePermille(5) && w.entities.count() > 0) {
        (void)w.setManualTarget(w.entities.idAt(w.rngEvents.range(w.entities.count())));
    }

    // QTE 입력 자리 (§3) — 프레젠테이션이 등급을 매기기 전까지 무작위로 흉내낸다.
    // 입력 로그가 붙으면 (틱 번호, 등급)으로 기록된다.
    if (w.hero.qte.open() && w.tickCount() == w.hero.qte.perfectTo) {
        InputEvent e;
        e.tick  = w.tickCount();
        e.kind  = InputKind::QteGrade;
        e.value = w.rngEvents.range(3);
        (void)w.applyInput(e);
    }

    // 잠식이 가득 차면 다시 시작한다 (게임오버 처리는 §14-9 이후)
    if (w.hero.dead()) {
        w.hero.corruption = Fixed{};
        w.notifyCorruptionChanged();
    }

    static SimScratch scratch;      // [파생] — 매 틱 재구축되므로 World 밖에 둔다
    stepWorld(w, cfg, table, scratch);              // ← 실제 틱 루프
}

inline void runScript(World& w, int32_t ticks) {
    if (w.tickCount() == 0) setup(w, data().cfg, data().table, data().hero);
    for (int32_t t = 0; t < ticks; ++t) scriptTick(w, data().cfg, data().table);
}

}  // namespace dc::dev

#endif  // DC_DEV_SCRIPT_H
