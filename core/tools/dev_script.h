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
#include "dev_data.h"

namespace dc::dev {

// 임시 조합식 테이블. 데이터 로더가 붙으면 data/items.json이 이 자리를 채운다.
// 흔함 9종 + 조합 결과 12종 = 21종, 조합식 12개 — 수직 슬라이스의 축소판이다.
inline const RecipeTable& devRecipes() {
    static RecipeTable table = [] {
        RecipeData r[12];
        auto mk = [](ItemId res, ItemId a, ItemId b, ItemId c) {
            RecipeData d;
            d.result = res;
            d.ingredients[0] = a; d.ingredients[1] = b;
            d.count = 2;
            if (c != ITEM_NONE) { d.ingredients[2] = c; d.count = 3; }
            return d;
        };
        // 흔함 0~8 → 안흔함 9~14 → 상위 15~20
        r[0]  = mk(9,  0, 1, ITEM_NONE);
        r[1]  = mk(10, 0, 2, ITEM_NONE);
        r[2]  = mk(11, 1, 4, ITEM_NONE);
        r[3]  = mk(12, 2, 3, ITEM_NONE);
        r[4]  = mk(13, 3, 5, ITEM_NONE);
        r[5]  = mk(14, 6, 7, ITEM_NONE);
        r[6]  = mk(15, 9, 10, ITEM_NONE);
        r[7]  = mk(16, 11, 12, ITEM_NONE);
        r[8]  = mk(17, 13, 14, ITEM_NONE);
        r[9]  = mk(18, 9, 9, 9);          // 동일 등급 3연성
        r[10] = mk(19, 15, 16, ITEM_NONE);
        r[11] = mk(20, 17, 18, 8);
        RecipeTable t;
        (void)t.build(r, 12, 21);
        return t;
    }();
    return table;
}

// 런 시작 설정. 진짜 데이터 로더가 붙으면 dev_data.h와 함께 사라진다.
inline void setup(World& w) {
    applyHeroBaseline(w);
    w.inventory.init(devRecipes());
}

inline const SimConfig& devSimConfig() {
    static const SimConfig cfg = devConfig();
    return cfg;
}

// 한 틱 — 실제 시스템 + 아직 없는 시스템의 자리만 흔든다.
inline void scriptTick(World& w) {
    // 카드 자리 (§4) — 레벨업 카드 선택이 붙기 전까지 모디파이어를 무작위로 얹는다
    if (w.rngCards.chancePermille(40)) {
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

    // 아이템 자리 (§5) — 뽑기는 흔함만 나온다
    {
        const RecipeTable& rt = devRecipes();
        if (w.rngItems.chancePermille(300)) {
            (void)w.inventory.add(rt, static_cast<ItemId>(w.rngItems.range(9)), 1);
        }
        for (uint32_t r = 0; r < rt.recipeCount(); ++r) {
            if (w.inventory.craftable(r)) { (void)w.inventory.craft(rt, r); break; }
        }
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
    stepWorld(w, devSimConfig(), scratch);   // ← 실제 틱 루프
}

inline void runScript(World& w, int32_t ticks) {
    if (w.tickCount() == 0) setup(w);
    for (int32_t t = 0; t < ticks; ++t) scriptTick(w);
}

}  // namespace dc::dev

#endif  // DC_DEV_SCRIPT_H
