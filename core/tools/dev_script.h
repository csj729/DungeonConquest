// **임시 틱 드라이버.** Spawn / Targeting / Combat이 붙으면 이 파일은 삭제된다.
//
// 체크섬과 재현성 하네스는 "상태가 실제로 움직이는 틱"이 있어야 검증할 수 있는데
// 아직 시스템이 없다. 그래서 스폰·난수 소비·삭제·게이지 충전을 섞은 각본으로
// 대신한다. 게임 규칙과는 아무 관계가 없다 — 상태를 골고루 휘젓는 것이 목적이다.
//
// **core/ 안이 아니라 tools/ 안에 두는 이유**: 시뮬 코어에 들어가면 삭제 시점을
// 놓친다. 여기 있으면 디렉터리 하나만 지우면 된다.
#ifndef DC_DEV_SCRIPT_H
#define DC_DEV_SCRIPT_H

#include "../include/dc/item.h"
#include "../include/dc/world.h"

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

// 런 시작 설정. 데이터 로더가 붙으면 hero.json / stats.json이 이 자리를 채운다.
inline void setup(World& w) {
    w.hero.stats.setBase(Stat::CorruptionMax, Fixed(1250));
    w.hero.stats.setBase(Stat::AttackPower,   Fixed(10));
    w.hero.stats.setBase(Stat::AttackSpeed,   Fixed(1));
    for (uint32_t i = 0; i < STAT_COUNT; ++i) {
        w.hero.stats.setBounds(static_cast<Stat>(i),
                               StatBounds{Fixed{}, Fixed::fromPermille(-900)});
    }
    // 잠식 최대치는 하한이 있어야 비율 계산의 분모가 0이 되지 않는다.
    w.hero.stats.setBounds(Stat::CorruptionMax,
                           StatBounds{Fixed(1), Fixed::fromPermille(-900)});
    w.inventory.init(devRecipes());
    w.refreshAllConditions();
}

// 한 틱. 시스템이 들어올 자리마다 상태를 건드린다.
inline void scriptTick(World& w) {
    // Spawn 자리
    const uint32_t n = w.rngSpawn.range(3);
    for (uint32_t i = 0; i < n; ++i) {
        SpawnDesc d;
        d.posX      = Fixed::fromRaw(static_cast<int32_t>(w.rngSpawn.nextU32() & 0xFFFF));
        d.posY      = Fixed::fromRaw(static_cast<int32_t>(w.rngSpawn.nextU32() & 0xFFFF));
        d.maxHp     = Fixed(20);
        d.archetype = w.rngSpawn.chancePermille(80) ? Archetype::Elite : Archetype::Trash;
        if (d.archetype == Archetype::Elite) d.ccGaugeMax = Fixed(100);
        if (!w.entities.spawn(d, w.tickCount(), w.masterSeed()).valid()) break;
        ++w.spawn.spawnedTotal;
    }

    // Combat 자리 — 피해 누적과 처치
    if (w.entities.count() > 0) {
        const uint32_t victim = w.rngCombat.range(w.entities.count());
        w.entities.damageTaken[victim] = w.entities.damageTaken[victim] + Fixed(7);
        (void)w.hero.proc.roll(w.rngCombat, 2112);   // data/hero.json: proc_prd_c_q16
        if (w.entities.damageTaken[victim].raw >= w.entities.maxHp[victim].raw) {
            if (w.entities.markDead(w.entities.idAt(victim))) {
                if (w.entities.archetype[victim] == Archetype::Elite) {
                    ++w.run.killedElite;
                    w.run.clearPoints += 10;
                } else {
                    ++w.run.killedTrash;
                    w.run.clearPoints += 1;
                }
            }
        }
        w.hero.target = w.entities.idAt(victim);
    }

    // 모디파이어 자리 — 아이템·각인이 스탯을 만지는 경로를 체크섬이 덮도록
    if (w.rngCards.chancePermille(40)) {
        const uint32_t src = w.allocSourceId();
        const Stat s = static_cast<Stat>(w.rngCards.range(STAT_COUNT));
        const Fixed pm = Fixed::fromPermille(static_cast<int32_t>(w.rngCards.range(200)));
        switch (w.rngCards.range(5)) {
            case 0:
                w.hero.stats.addFlat(s, Fixed::fromRaw(static_cast<int32_t>(w.rngCards.range(4096))));
                break;
            case 1:
                w.hero.stats.addPctAdd(s, pm);
                break;
            case 2:
                (void)w.hero.stats.addMult(s, src, pm);
                break;
            case 3: {
                // 조건부 — 잠식 임계 (히스테리시스)
                Condition c;
                c.kind   = ConditionKind::CorruptionThreshold;
                c.paramA = 500 + static_cast<int32_t>(w.rngCards.range(300));
                c.paramB = c.paramA - static_cast<int32_t>(w.rngCards.range(100));
                (void)w.hero.stats.addConditional(s, src, ModOp::PercentAdd, pm, c,
                                                  w.conditionContext());
                break;
            }
            default: {
                // 조건부 — 시간 제한 버프
                Condition c;
                c.kind   = ConditionKind::TimeWindow;
                c.paramA = w.tickCount() + 20 + static_cast<int32_t>(w.rngCards.range(200));
                (void)w.hero.stats.addConditional(s, src, ModOp::Flat, pm, c,
                                                  w.conditionContext());
                break;
            }
        }
    }

    // 오래된 모디파이어를 가끔 떼어낸다 — 착탈 경로도 체크섬이 덮도록
    if (w.rngItems.chancePermille(20)) {
        const Stat s = static_cast<Stat>(w.rngItems.range(STAT_COUNT));
        const uint32_t src = 1 + w.rngItems.range(w.peekSourceId());
        if (!w.hero.stats.removeConditional(s, src)) (void)w.hero.stats.removeMult(s, src);
    }

    // 아이템 자리 — 뽑기(흔함만, §5)와 조합. 체크섬이 인벤 경로를 덮도록.
    {
        const RecipeTable& rt = devRecipes();
        if (w.rngItems.chancePermille(300)) {
            (void)w.inventory.add(rt, static_cast<ItemId>(w.rngItems.range(9)), 1);
        }
        // 조합 가능한 것 중 **가장 앞선 인덱스**를 만든다 — 순서를 고정한다.
        for (uint32_t r = 0; r < rt.recipeCount(); ++r) {
            if (w.inventory.craftable(r)) { (void)w.inventory.craft(rt, r); break; }
        }
    }

    // 잠식 게이지 자리 — 물량 충전. 값이 움직였으니 임계 조건을 다시 본다.
    w.hero.corruption = w.hero.corruption
                      + Fixed::fromRaw(static_cast<int32_t>(w.entities.count()));
    if (w.hero.corruption.raw > w.hero.corruptionMax().raw) w.hero.corruption = Fixed{};
    w.notifyCorruptionChanged();
    w.notifyTargetChanged();

    // 진행 자리
    if (w.run.clearPoints >= 60) {
        w.run.clearPoints = 0;
        if (++w.run.segmentIndex >= 8) { w.run.segmentIndex = 0; ++w.run.mapIndex; }
    }

    w.tick();
}

inline void runScript(World& w, int32_t ticks) {
    if (w.tickCount() == 0) setup(w);
    for (int32_t t = 0; t < ticks; ++t) scriptTick(w);
}

}  // namespace dc::dev

#endif  // DC_DEV_SCRIPT_H
