// 조건부 모디파이어 테스트 (§9).
//
// 네 가지를 본다:
//   1. **순환 의존이 등록 시점에 막히는가** — 런타임 진동이 아니라 false로
//   2. **히스테리시스가 경계 떨림을 없애는가**
//   3. **전수 평가를 안 하는가** — 관계없는 이벤트는 아무것도 건드리지 않는다
//   4. **조건이 켜졌다 꺼져도 적용 순서가 유지되는가**
#include <initializer_list>

#include "../include/dc/condition.h"
#include "../include/dc/stat_block.h"
#include "../include/dc/world.h"
#include "test_main.h"

using namespace dc;

static StatBlock makeBlock(Fixed base) {
    Fixed bases[STAT_COUNT];
    StatBounds bounds[STAT_COUNT];
    for (uint32_t i = 0; i < STAT_COUNT; ++i) { bases[i] = base; bounds[i] = StatBounds{}; }
    StatBlock b;
    b.init(bases, bounds);
    return b;
}

static Condition corruptionCond(int32_t onAt, int32_t offAt) {
    Condition c;
    c.kind   = ConditionKind::CorruptionThreshold;
    c.paramA = onAt;
    c.paramB = offAt;
    return c;
}

static Condition timeCond(int32_t expireTick) {
    Condition c;
    c.kind   = ConditionKind::TimeWindow;
    c.paramA = expireTick;
    return c;
}

static uint16_t maskOf(ConditionKind k) { return conditionKindBit(k); }

int main() {
    printf("test_condition\n");

    dctest::section("순환 의존 — 타입·데이터 두 겹으로 막는다");
    {
        // 1겹: 평가가 재귀할 수 없다. ConditionContext에는 StatBlock으로 가는
        // 참조가 없으므로 "조건 평가 중 스탯 재계산"이 컴파일되지 않는다.
        // (구조로 막은 것이라 런타임 테스트가 아니라 아래 static_assert가 증거다)
        static_assert(conditionDependsOn(ConditionKind::CorruptionThreshold)
                          == Stat::CorruptionMax, "");
        static_assert(!conditionAllowedOn(ConditionKind::CorruptionThreshold,
                                          Stat::CorruptionMax), "");
        static_assert(conditionAllowedOn(ConditionKind::CorruptionThreshold,
                                         Stat::AttackPower), "");
        static_assert(conditionAllowedOn(ConditionKind::TimeWindow, Stat::CorruptionMax), "");

        // 2겹: 의미상 진동하는 조합을 등록 시점에 거부한다.
        StatBlock b = makeBlock(Fixed(100));
        ConditionContext ctx;

        // **금지** — 잠식 임계 조건이 CorruptionMax를 바꾸면 자기 분모를 바꾼다.
        CHECK(!b.addConditional(Stat::CorruptionMax, 1, ModOp::PercentAdd,
                                Fixed::fromPermille(500), corruptionCond(500, 400), ctx));
        CHECK_EQ(b.conditionalCount(Stat::CorruptionMax), 0u);

        // 같은 조건을 다른 스탯에 거는 것은 허용된다 (고리가 닫히지 않는다).
        CHECK(b.addConditional(Stat::AttackPower, 1, ModOp::PercentAdd,
                               Fixed::fromPermille(500), corruptionCond(500, 400), ctx));
        // 시간 조건은 CorruptionMax에도 걸 수 있다 — 스탯을 읽지 않으므로.
        CHECK(b.addConditional(Stat::CorruptionMax, 2, ModOp::Flat,
                               Fixed(50), timeCond(100), ctx));
    }

    dctest::section("등록 거부 사유");
    {
        StatBlock b = makeBlock(Fixed(100));
        ConditionContext ctx;

        // 히스테리시스 역전 — 끄는 선이 켜는 선보다 위면 경계에서 매 틱 토글한다.
        CHECK(!b.addConditional(Stat::AttackPower, 1, ModOp::Flat, Fixed(1),
                                corruptionCond(400, 500), ctx));
        // 같으면 허용 (히스테리시스 폭 0 = 단순 임계)
        CHECK(b.addConditional(Stat::AttackPower, 1, ModOp::Flat, Fixed(1),
                               corruptionCond(500, 500), ctx));

        CHECK(!b.addConditional(Stat::AttackPower, 0, ModOp::Flat, Fixed(1),
                                Condition{}, ctx));            // sourceId 0 예약
        CHECK(!b.addConditional(Stat::AttackPower, 1, ModOp::Flat, Fixed(1),
                                Condition{}, ctx));            // sourceId 중복
        CHECK(!b.removeConditional(Stat::AttackPower, 999));   // 없는 것

        // 용량 초과는 조용히 버리지 않는다.
        uint32_t added = 1;
        for (uint32_t i = 2; i <= 20; ++i) {
            if (b.addConditional(Stat::AttackPower, i, ModOp::Flat, Fixed(1),
                                 Condition{}, ctx)) ++added;
        }
        CHECK_EQ(added, 8u);
    }

    dctest::section("히스테리시스 — 경계에서 떨지 않는다");
    {
        StatBlock b = makeBlock(Fixed(100));
        ConditionContext ctx;
        ctx.corruptionPermille = 0;

        // 잠식 70% 이상에서 켜지고, 60% 미만에서 꺼진다 (궁지 보너스).
        CHECK(b.addConditional(Stat::AttackPower, 1, ModOp::PercentAdd,
                               Fixed::fromPermille(500), corruptionCond(700, 600), ctx));
        CHECK(!b.conditionalActive(Stat::AttackPower, 1));
        CHECK_EQ(b.value(Stat::AttackPower).raw, Fixed(100).raw);

        auto at = [&](int32_t pm) {
            ctx.corruptionPermille = pm;
            return b.refreshConditions(ctx, maskOf(ConditionKind::CorruptionThreshold));
        };

        CHECK_EQ(at(699), 0u);                                  // 아직 안 켜짐
        CHECK(!b.conditionalActive(Stat::AttackPower, 1));
        CHECK(at(700) != 0u);                                   // 켜짐
        CHECK(b.conditionalActive(Stat::AttackPower, 1));
        CHECK(b.value(Stat::AttackPower).raw > Fixed(140).raw);

        // **여기가 핵심** — 켜는 선 아래로 내려가도 끄는 선 위면 유지된다.
        CHECK_EQ(at(650), 0u);
        CHECK(b.conditionalActive(Stat::AttackPower, 1));
        CHECK_EQ(at(600), 0u);
        CHECK(b.conditionalActive(Stat::AttackPower, 1));
        CHECK(at(599) != 0u);                                   // 끄는 선 아래 → 꺼짐
        CHECK(!b.conditionalActive(Stat::AttackPower, 1));

        // 히스테리시스가 없었다면 700 근처를 오갈 때 매번 뒤집힌다.
        // 실제로 밴드 안(600~699)을 100번 오가도 토글이 0회여야 한다.
        at(700);
        int toggles = 0;
        for (int i = 0; i < 100; ++i) {
            if (at(600 + (i % 100)) != 0u) ++toggles;
        }
        CHECK_EQ(toggles, 0);
        printf("    밴드 안 100회 왕복에서 토글 %d회\n", toggles);
    }

    dctest::section("시간 조건 — 만료 틱까지 아무 일도 안 한다");
    {
        World w;
        w.init(1);
        w.hero.stats.setBase(Stat::AttackPower, Fixed(100));
        w.hero.stats.setBase(Stat::CorruptionMax, Fixed(1000));

        ConditionContext ctx = w.conditionContext();
        CHECK_EQ(w.hero.stats.nextExpiryTick(), StatBlock::NO_EXPIRY);

        // 60틱(3초)짜리 버프.
        CHECK(w.hero.stats.addConditional(Stat::AttackPower, w.allocSourceId(),
                                          ModOp::PercentAdd, Fixed::fromPermille(1000),
                                          timeCond(60), ctx));
        CHECK(w.hero.stats.conditionalActive(Stat::AttackPower, 1));
        CHECK_EQ(w.hero.stats.nextExpiryTick(), 60);
        CHECK(w.hero.stats.value(Stat::AttackPower).raw > Fixed(190).raw);

        // 59틱까지는 살아 있고, 매 틱 하는 일은 int 비교 하나뿐이다.
        for (int i = 0; i < 59; ++i) w.tick();
        CHECK_EQ(w.tickCount(), 59);
        CHECK(w.hero.stats.conditionalActive(Stat::AttackPower, 1));

        w.tick();   // 60틱 → 만료
        CHECK_EQ(w.tickCount(), 60);
        CHECK(!w.hero.stats.conditionalActive(Stat::AttackPower, 1));
        CHECK_EQ(w.hero.stats.value(Stat::AttackPower).raw, Fixed(100).raw);

        // 만료 후에는 후보에서 빠지므로 다시 NO_EXPIRY로 돌아간다.
        CHECK_EQ(w.hero.stats.nextExpiryTick(), StatBlock::NO_EXPIRY);
    }

    dctest::section("만료가 여럿이면 가장 이른 것부터");
    {
        World w;
        w.init(2);
        ConditionContext ctx = w.conditionContext();
        // 일부러 뒤섞어 넣는다.
        w.hero.stats.addConditional(Stat::AttackPower, 10, ModOp::Flat, Fixed(1), timeCond(80), ctx);
        w.hero.stats.addConditional(Stat::AttackPower, 11, ModOp::Flat, Fixed(2), timeCond(20), ctx);
        w.hero.stats.addConditional(Stat::Armor,       12, ModOp::Flat, Fixed(4), timeCond(50), ctx);
        CHECK_EQ(w.hero.stats.nextExpiryTick(), 20);

        while (w.tickCount() < 20) w.tick();
        CHECK(!w.hero.stats.conditionalActive(Stat::AttackPower, 11));
        CHECK(w.hero.stats.conditionalActive(Stat::AttackPower, 10));
        CHECK_EQ(w.hero.stats.nextExpiryTick(), 50);

        while (w.tickCount() < 50) w.tick();
        CHECK(!w.hero.stats.conditionalActive(Stat::Armor, 12));
        CHECK_EQ(w.hero.stats.nextExpiryTick(), 80);

        while (w.tickCount() < 80) w.tick();
        CHECK(!w.hero.stats.conditionalActive(Stat::AttackPower, 10));
        CHECK_EQ(w.hero.stats.nextExpiryTick(), StatBlock::NO_EXPIRY);
    }

    dctest::section("전수 평가를 하지 않는다");
    {
        // 관계없는 이벤트는 아무것도 건드리지 않아야 한다 (§9 "매 틱 전수 평가 금지").
        StatBlock b = makeBlock(Fixed(100));
        ConditionContext ctx;
        ctx.corruptionPermille = 900;

        // 조건부가 하나도 없으면 어떤 이벤트든 O(1)로 끝난다.
        CHECK_EQ(b.refreshConditions(ctx, maskOf(ConditionKind::CorruptionThreshold)), 0u);
        CHECK_EQ(b.statsUsingKind(ConditionKind::CorruptionThreshold), 0u);

        b.addConditional(Stat::AttackPower, 1, ModOp::Flat, Fixed(5),
                         corruptionCond(500, 400), ctx);
        CHECK_EQ(b.statsUsingKind(ConditionKind::CorruptionThreshold),
                 statBit(Stat::AttackPower));

        // 타겟이 바뀌어도 잠식 조건은 건드리지 않는다.
        ctx.targetArchetype = 1;
        CHECK_EQ(b.refreshConditions(ctx, maskOf(ConditionKind::TargetArchetype)), 0u);
        CHECK(b.conditionalActive(Stat::AttackPower, 1));   // 그대로

        // 제거하면 역인덱스도 같이 비워진다.
        CHECK(b.removeConditional(Stat::AttackPower, 1));
        CHECK_EQ(b.statsUsingKind(ConditionKind::CorruptionThreshold), 0u);
    }

    dctest::section("타겟 조건");
    {
        World w;
        w.init(3);
        w.hero.stats.setBase(Stat::AttackPower, Fixed(100));

        Condition c;
        c.kind   = ConditionKind::TargetArchetype;
        c.paramA = static_cast<int32_t>(Archetype::Elite);
        CHECK(w.hero.stats.addConditional(Stat::AttackPower, w.allocSourceId(),
                                          ModOp::PercentAdd, Fixed::fromPermille(300),
                                          c, w.conditionContext()));
        CHECK(!w.hero.stats.conditionalActive(Stat::AttackPower, 1));   // 타겟 없음

        SpawnDesc trash;  trash.archetype = Archetype::Trash;
        SpawnDesc elite;  elite.archetype = Archetype::Elite;
        const EntityId t = w.entities.spawn(trash, 0, 3);
        const EntityId e = w.entities.spawn(elite, 0, 3);

        w.hero.target = t;
        w.notifyTargetChanged();
        CHECK(!w.hero.stats.conditionalActive(Stat::AttackPower, 1));

        w.hero.target = e;
        CHECK(w.notifyTargetChanged() != 0u);
        CHECK(w.hero.stats.conditionalActive(Stat::AttackPower, 1));
        CHECK(w.hero.stats.value(Stat::AttackPower).raw > Fixed(125).raw);

        // 타겟이 죽어 stale이 되면 조건이 꺼져야 한다.
        w.entities.markDead(e);
        w.applyDeaths();
        CHECK(w.notifyTargetChanged() != 0u);
        CHECK(!w.hero.stats.conditionalActive(Stat::AttackPower, 1));
    }

    dctest::section("조건부가 켜져도 적용 순서는 유지된다");
    {
        // 조건부 PercentMult가 무조건 PercentMult 사이에 sourceId 순서대로
        // 끼어들어야 한다. 순서가 흔들리면 켜졌다 꺼질 때마다 값이 미묘하게 달라진다.
        StatBlock a = makeBlock(Fixed(1000));
        StatBlock b = makeBlock(Fixed(1000));
        ConditionContext on;
        on.corruptionPermille = 900;

        // a: 2번만 조건부 (켜진 상태)
        a.addMult(Stat::AttackPower, 1, Fixed::fromPermille(377));
        a.addConditional(Stat::AttackPower, 2, ModOp::PercentMult,
                         Fixed::fromPermille(-211), corruptionCond(500, 400), on);
        a.addMult(Stat::AttackPower, 3, Fixed::fromPermille(131));

        // b: 전부 무조건
        b.addMult(Stat::AttackPower, 1, Fixed::fromPermille(377));
        b.addMult(Stat::AttackPower, 2, Fixed::fromPermille(-211));
        b.addMult(Stat::AttackPower, 3, Fixed::fromPermille(131));

        CHECK(a.conditionalActive(Stat::AttackPower, 2));
        CHECK_EQ(a.value(Stat::AttackPower).raw, b.value(Stat::AttackPower).raw);
        printf("    조건부 끼어든 값 = 전부 무조건인 값: %d\n", a.value(Stat::AttackPower).raw);

        // 껐다 켜도 정확히 제자리로 온다.
        const int32_t onValue = a.value(Stat::AttackPower).raw;
        ConditionContext off;
        off.corruptionPermille = 0;
        for (int i = 0; i < 50; ++i) {
            a.refreshConditions(off, maskOf(ConditionKind::CorruptionThreshold));
            a.refreshConditions(on,  maskOf(ConditionKind::CorruptionThreshold));
        }
        CHECK_EQ(a.value(Stat::AttackPower).raw, onValue);

        // 꺼진 상태는 그 모디파이어만 빠진 값과 같아야 한다.
        a.refreshConditions(off, maskOf(ConditionKind::CorruptionThreshold));
        StatBlock c = makeBlock(Fixed(1000));
        c.addMult(Stat::AttackPower, 1, Fixed::fromPermille(377));
        c.addMult(Stat::AttackPower, 3, Fixed::fromPermille(131));
        CHECK_EQ(a.value(Stat::AttackPower).raw, c.value(Stat::AttackPower).raw);
    }

    dctest::section("조건부 Override");
    {
        StatBlock b = makeBlock(Fixed(500));
        ConditionContext on;
        on.corruptionPermille = 900;

        b.addOverride(Stat::MoveSpeed, 10, Fixed(7));
        // sourceId가 더 작은 조건부 Override가 이긴다 — 켜져 있을 때만.
        CHECK(b.addConditional(Stat::MoveSpeed, 5, ModOp::Override, Fixed(3),
                               corruptionCond(500, 400), on));
        CHECK_EQ(b.value(Stat::MoveSpeed).raw, Fixed(3).raw);

        ConditionContext off;
        b.refreshConditions(off, maskOf(ConditionKind::CorruptionThreshold));
        CHECK_EQ(b.value(Stat::MoveSpeed).raw, Fixed(7).raw);   // 무조건 쪽으로 복귀
    }

    dctest::section("체크섬 — active는 [상태], 역인덱스는 [파생]");
    {
        World a, b;
        a.init(999);
        b.init(999);
        for (World* w : {&a, &b}) {
            w->hero.stats.setBase(Stat::CorruptionMax, Fixed(1000));
            w->hero.stats.addConditional(Stat::AttackPower, w->allocSourceId(),
                                         ModOp::Flat, Fixed(50),
                                         corruptionCond(500, 400), w->conditionContext());
        }
        CHECK_EQU(a.checksum(), b.checksum());

        // **active가 뒤집히면 해시가 갈려야 한다** — 히스테리시스 상태가
        // 빠지면 경계 밴드 안에서 서버와 클라가 다른 값을 쓰게 된다.
        a.hero.corruption = Fixed(600);
        CHECK(a.notifyCorruptionChanged() != 0u);
        CHECK(a.checksum() != b.checksum());

        b.hero.corruption = Fixed(600);
        b.notifyCorruptionChanged();
        CHECK_EQU(a.checksum(), b.checksum());

        // 밴드 안에서 **경로가 다르면 상태도 다르다.** 같은 잠식 비율인데
        // 한쪽은 위에서 내려왔고 한쪽은 아래에서 올라왔으면 active가 갈린다.
        World up, down;
        up.init(1);
        down.init(1);
        for (World* w : {&up, &down}) {
            w->hero.stats.setBase(Stat::CorruptionMax, Fixed(1000));
            w->hero.stats.addConditional(Stat::AttackPower, w->allocSourceId(),
                                         ModOp::Flat, Fixed(50),
                                         corruptionCond(700, 600), w->conditionContext());
        }
        up.hero.corruption = Fixed(800);   up.notifyCorruptionChanged();     // 위에서
        up.hero.corruption = Fixed(650);   up.notifyCorruptionChanged();
        down.hero.corruption = Fixed(650); down.notifyCorruptionChanged();   // 아래에서

        CHECK(up.hero.stats.conditionalActive(Stat::AttackPower, 1));
        CHECK(!down.hero.stats.conditionalActive(Stat::AttackPower, 1));
        CHECK(up.checksum() != down.checksum());
        printf("    같은 잠식 65%%인데 경로가 달라 active가 갈린다 — 체크섬도 갈림\n");
    }

    dctest::section("스냅샷 복원 후 조건 상태 유지");
    {
        World w;
        w.init(77);
        w.hero.stats.setBase(Stat::CorruptionMax, Fixed(1000));
        w.hero.stats.addConditional(Stat::AttackPower, w.allocSourceId(), ModOp::Flat,
                                    Fixed(50), corruptionCond(700, 600), w.conditionContext());
        w.hero.corruption = Fixed(800);
        w.notifyCorruptionChanged();
        w.hero.corruption = Fixed(650);
        w.notifyCorruptionChanged();
        CHECK(w.hero.stats.conditionalActive(Stat::AttackPower, 1));

        World restored = w;   // memcpy 상당
        CHECK_EQU(restored.checksum(), w.checksum());
        CHECK(restored.hero.stats.conditionalActive(Stat::AttackPower, 1));

        // [파생]인 역인덱스는 복원 후에도 유효해야 한다 (같이 복사되므로).
        CHECK_EQ(restored.hero.stats.statsUsingKind(ConditionKind::CorruptionThreshold),
                 statBit(Stat::AttackPower));

        // refreshAllConditions를 불러도 히스테리시스 상태가 보존된다 —
        // 밴드 안이므로 이전 상태가 곧 현재 상태다.
        restored.refreshAllConditions();
        CHECK(restored.hero.stats.conditionalActive(Stat::AttackPower, 1));
        CHECK_EQU(restored.checksum(), w.checksum());
    }

    return dctest::summary("test_condition");
}
