// Spawn / Targeting / Combat 통합 테스트 (§2·§3·§9).
//
// 지금까지 만든 것이 전부 여기서 처음 같이 돈다. 그래서 확인할 것도
// 개별 동작보다 **시스템 사이의 계약**이다 — 우선순위가 QTE 예산을 지키는가,
// 상한 유지가 발산하지 않는가, 죽음이 틱 끝에만 반영되는가.
#include <initializer_list>

#include "../include/dc/sim.h"
#include "../tools/dev_data.h"
#include "test_main.h"

using namespace dc;

static World makeWorld(uint64_t seed = 1) {
    World w;
    w.init(seed);
    dev::applyHeroBaseline(w);
    return w;
}

static SpawnDesc mob(Archetype a, int32_t prio, Fixed x, Fixed y, int32_t hp = 20) {
    SpawnDesc d;
    d.posX = x; d.posY = y;
    d.maxHp = Fixed(hp);
    d.archetype = a;
    d.targetPriority = prio;
    d.attackRange = Fixed(1);
    return d;
}

int main() {
    printf("test_systems\n");
    const SimConfig cfg = dev::devConfig();

    dctest::section("타게팅 — 우선순위가 거리를 이긴다");
    {
        World w = makeWorld();
        // 잡몹은 코앞(1타일), 엘리트는 멀리(10타일)
        const EntityId trash = w.entities.spawn(mob(Archetype::Trash, 0, Fixed(1), Fixed(0)), 0, 1);
        const EntityId elite = w.entities.spawn(mob(Archetype::Elite, 35, Fixed(10), Fixed(0)), 0, 1);
        CHECK(selectAutoTarget(w.entities, Fixed(0), Fixed(0)) == elite);

        // 보스가 있어도 엘리트가 먼저다 — 데이터 우선순위(엘리트 35 > 보스 10).
        // 보스를 먼저 치면 궁병대장이 보스전 내내 살아 QTE 25회 · 잠식 80%를 먹는다.
        const EntityId boss = w.entities.spawn(mob(Archetype::Boss, 10, Fixed(2), Fixed(0), 2100), 0, 1);
        CHECK(selectAutoTarget(w.entities, Fixed(0), Fixed(0)) == elite);

        // 엘리트가 죽으면 보스로 내려온다 (잡몹 0보다 높으므로).
        w.entities.markDead(elite);
        w.applyDeaths();
        CHECK(selectAutoTarget(w.entities, Fixed(0), Fixed(0)) == boss);
        w.entities.markDead(boss);
        w.applyDeaths();
        CHECK(selectAutoTarget(w.entities, Fixed(0), Fixed(0)) == trash);
    }

    dctest::section("타게팅 — 엘리트끼리의 순서도 데이터가 정한다");
    {
        // archetype 고정 순서로는 표현할 수 없는 판단:
        // 주술사는 QTE 패턴이 없지만 소환으로 물량을 불리므로 궁병대장보다 먼저다.
        World w = makeWorld();
        const EntityId archer = w.entities.spawn(mob(Archetype::Elite, 35, Fixed(1), Fixed(0), 120), 0, 1);
        const EntityId shaman = w.entities.spawn(mob(Archetype::Elite, 40, Fixed(9), Fixed(0), 90), 0, 1);
        CHECK(selectAutoTarget(w.entities, Fixed(0), Fixed(0)) == shaman);
        w.entities.markDead(shaman);
        w.applyDeaths();
        CHECK(selectAutoTarget(w.entities, Fixed(0), Fixed(0)) == archer);
    }

    dctest::section("타게팅 — 동점은 거리 → EntityId");
    {
        World w = makeWorld();
        const EntityId far  = w.entities.spawn(mob(Archetype::Trash, 0, Fixed(5), Fixed(0)), 0, 1);
        const EntityId near = w.entities.spawn(mob(Archetype::Trash, 0, Fixed(2), Fixed(0)), 0, 1);
        CHECK(selectAutoTarget(w.entities, Fixed(0), Fixed(0)) == near);
        (void)far;

        // 거리까지 같으면 EntityId 오름차순 — **동점이 남지 않는다.**
        World t = makeWorld();
        const EntityId a = t.entities.spawn(mob(Archetype::Trash, 0, Fixed(3), Fixed(0)), 0, 1);
        const EntityId b = t.entities.spawn(mob(Archetype::Trash, 0, Fixed(0), Fixed(3)), 0, 1);
        const EntityId c = t.entities.spawn(mob(Archetype::Trash, 0, Fixed(-3), Fixed(0)), 0, 1);
        const EntityId picked = selectAutoTarget(t.entities, Fixed(0), Fixed(0));
        CHECK(picked == a);
        CHECK(a < b && b < c);

        // 순회 순서에 의존하지 않는다 — 앞쪽이 죽어 배열이 밀려도 규칙이 같다.
        t.entities.markDead(a);
        t.applyDeaths();
        CHECK(selectAutoTarget(t.entities, Fixed(0), Fixed(0)) == b);
    }

    dctest::section("수동 타게팅 — 자동 우선순위를 덮는다");
    {
        World w = makeWorld();
        const EntityId trash = w.entities.spawn(mob(Archetype::Trash, 0, Fixed(1), Fixed(0)), 0, 1);
        const EntityId elite = w.entities.spawn(mob(Archetype::Elite, 35, Fixed(9), Fixed(0), 120), 0, 1);
        CHECK(selectTarget(w.entities, Fixed(0), Fixed(0), EntityId::invalid()) == elite);

        // 플레이어가 잡몹을 찍으면 그게 이긴다.
        CHECK(w.setManualTarget(trash));
        CHECK(selectTarget(w.entities, Fixed(0), Fixed(0), w.hero.manualTarget) == trash);

        // **지속된다** — 매 틱 다시 찍지 않아도 유지된다.
        for (int i = 0; i < 10; ++i) {
            CHECK(selectTarget(w.entities, Fixed(0), Fixed(0), w.hero.manualTarget) == trash);
        }

        // **대상이 죽으면 자동 해제** — stale 핸들이라 자동 우선순위로 복귀한다.
        w.entities.markDead(trash);
        w.applyDeaths();
        CHECK(selectTarget(w.entities, Fixed(0), Fixed(0), w.hero.manualTarget) == elite);

        // 이미 죽은 것은 찍을 수 없다.
        CHECK(!w.setManualTarget(trash));
        CHECK(!w.setManualTarget(EntityId::invalid()));

        // 명시적 해제
        CHECK(w.setManualTarget(elite));
        w.clearManualTarget();
        CHECK(!w.hero.manualTarget.valid());
    }

    dctest::section("수동 타게팅은 [상태] — 체크섬에 들어간다");
    {
        // 서버가 리플레이할 때 같은 지시를 재현해야 하므로 상태다.
        World a = makeWorld(7), b = makeWorld(7);
        for (World* w : {&a, &b}) (void)w->entities.spawn(mob(Archetype::Trash, 0, Fixed(1), Fixed(0)), 0, 7);
        CHECK_EQU(a.checksum(), b.checksum());
        CHECK(a.setManualTarget(a.entities.idAt(0)));
        CHECK(a.checksum() != b.checksum());
        CHECK(b.setManualTarget(b.entities.idAt(0)));
        CHECK_EQU(a.checksum(), b.checksum());
    }

    dctest::section("스폰 — 상한을 유지한다 (발산하지 않는다)");
    {
        World w = makeWorld(11);
        int32_t peak = 0;
        for (int i = 0; i < 4000; ++i) {
            spawnRun(w, cfg);
            w.beginTick();
            w.endTick();
            const int32_t a = static_cast<int32_t>(aliveCount(w.entities));
            if (a > peak) peak = a;
        }
        const int32_t cap = cfg.capFor(1);
        printf("    구간 1 상한 %d · 4000틱 최대 생존 %d\n", cap, peak);
        CHECK(peak <= cap);          // **상한을 넘지 않는다** — 발산 원천 차단
        CHECK(peak >= cap - 4);      // 그런데 상한 근처까지는 찬다
    }

    dctest::section("스폰 — 4방향 균등 · 반경 고정");
    {
        World w = makeWorld(3);
        for (int i = 0; i < 200; ++i) { spawnRun(w, cfg); w.beginTick(); w.endTick(); }

        int32_t dirCount[4] = {0, 0, 0, 0};
        const int64_t radius = (static_cast<int64_t>(cfg.spawnRadiusMilli) * Fixed::ONE_RAW) / 1000;
        bool radiusOk = true;
        for (uint32_t i = 0; i < w.entities.count(); ++i) {
            // raw가 8만대라 제곱이 int32를 넘는다 — int64로 비교한다
            const int64_t x = w.entities.posX[i].raw, y = w.entities.posY[i].raw;
            if (y > 0 && x * x < y * y)      ++dirCount[0];
            else if (x > 0)                  ++dirCount[1];
            else if (y < 0 && x * x < y * y) ++dirCount[2];
            else                             ++dirCount[3];
            // 스폰 직후가 아니라 추격 중이므로 반경 이하이기만 하면 된다
            if (distanceSq(w.entities.posX[i], w.entities.posY[i], Fixed{}, Fixed{})
                > radius * radius + radius) radiusOk = false;
        }
        printf("    방향 분포 N %d · E %d · S %d · W %d\n",
               dirCount[0], dirCount[1], dirCount[2], dirCount[3]);
        CHECK(radiusOk);
        for (int d = 0; d < 4; ++d) CHECK(dirCount[d] > 0);   // 한 방향으로 쏠리지 않는다
    }

    dctest::section("스폰 — 체력이 스폰 시점 구간으로 고정된다");
    {
        // 구간이 오른 뒤에 스폰된 몹만 체력이 높다. 이미 떠 있는 몹은 그대로다.
        CHECK_EQ(cfg.trashHpFor(1).raw, Fixed(20).raw);
        const int32_t hp8  = cfg.trashHpFor(8).raw;
        const int32_t hp24 = cfg.trashHpFor(24).raw;
        printf("    잡몹 체력 구간 1 %d → 8 %d → 24 %d (raw)\n", Fixed(20).raw, hp8, hp24);
        CHECK(hp8 > Fixed(20).raw);
        CHECK(hp24 > hp8);
        // 구간당 1.09배 → 24구간이면 약 7.3배
        CHECK(hp24 > Fixed(20).raw * 6);
        CHECK(hp24 < Fixed(20).raw * 9);
    }

    dctest::section("스폰 — 엘리트는 시간이 아니라 클리어 게이지에 걸린다");
    {
        World w = makeWorld(5);
        CHECK_EQ(w.spawn.nextEliteIndex, 0u);
        for (int i = 0; i < 500; ++i) { spawnRun(w, cfg); w.beginTick(); w.endTick(); }
        CHECK_EQ(w.spawn.nextEliteIndex, 0u);     // 시간이 지나도 안 나온다

        w.run.clearPoints = cfg.eliteSpawns[0].atClearPoints;
        spawnRun(w, cfg);
        CHECK_EQ(w.spawn.nextEliteIndex, 1u);     // 게이지가 차면 나온다
        bool found = false;
        for (uint32_t i = 0; i < w.entities.count(); ++i) {
            if (w.entities.archetype[i] == Archetype::Elite) found = true;
        }
        CHECK(found);
    }

    dctest::section("전투 — 방어력은 감산이 아니라 비율");
    {
        // K=100에서 armor=100이면 정확히 절반이 들어간다 (§9).
        CHECK_EQ(mitigate(Fixed(100), Fixed(100), 100).raw, Fixed(50).raw);
        CHECK_EQ(mitigate(Fixed(100), Fixed(0), 100).raw, Fixed(100).raw);
        // 100 × 100/(100+200) = 33.33
        CHECK(mitigate(Fixed(100), Fixed(200), 100).raw > Fixed(33).raw);
        CHECK(mitigate(Fixed(100), Fixed(200), 100).raw < Fixed(34).raw);
        // 음수 방어력은 0으로 클램프한다 (방깎 모디파이어가 존재하므로)
        CHECK_EQ(mitigate(Fixed(100), Fixed(-500), 100).raw, Fixed(100).raw);

        // **배율 성장과 무관하게 비율이 유지된다** — 감산이었다면 후반에 무의미해진다.
        const Fixed small = mitigate(Fixed(10), Fixed(100), 100);
        const Fixed big   = mitigate(Fixed(10000), Fixed(100), 100);
        CHECK_EQ(small.raw * 1000, big.raw);
    }

    dctest::section("전투 — 처치가 클리어 포인트로 이어진다");
    {
        World w = makeWorld(9);
        const EntityId t = w.entities.spawn(mob(Archetype::Trash, 0, Fixed(1), Fixed(0), 20), 0, 9);
        w.hero.target = t;
        int guard = 0;
        while (w.entities.alive(t) && guard++ < 2000) {
            combatRun(w, cfg);
            w.beginTick();
            w.endTick();
        }
        CHECK(!w.entities.alive(t));
        CHECK_EQ(w.run.killedTrash, 1);
        CHECK_EQ(w.run.clearPoints, cfg.trashPoints);
        printf("    잡몹 20HP 처치에 %d틱 (공격력 10 · 초당 1회)\n", guard);

        // 엘리트는 10점
        World e = makeWorld(9);
        const EntityId el = e.entities.spawn(mob(Archetype::Elite, 35, Fixed(1), Fixed(0), 20), 0, 9);
        e.hero.target = el;
        guard = 0;
        while (e.entities.alive(el) && guard++ < 2000) { combatRun(e, cfg); e.beginTick(); e.endTick(); }
        CHECK_EQ(e.run.killedElite, 1);
        CHECK_EQ(e.run.clearPoints, cfg.elitePoints);
    }

    dctest::section("전투 — 추격은 사거리까지만");
    {
        World w = makeWorld(2);
        SpawnDesc d = mob(Archetype::Trash, 0, Fixed(20), Fixed(0));
        d.approachSpeed = cfg.trash.approachSpeed;
        d.attackRange   = Fixed(1);
        const EntityId m = w.entities.spawn(d, 0, 2);
        const int32_t start = w.entities.posX[0].raw;

        for (int i = 0; i < 400; ++i) { combatRun(w, cfg); w.beginTick(); w.endTick(); }
        const int32_t end = w.entities.posX[w.entities.denseOf(m) < 0 ? 0
                                            : static_cast<uint32_t>(w.entities.denseOf(m))].raw;
        printf("    20타일 → %.2f타일 (400틱)\n", static_cast<double>(end) / Fixed::ONE_RAW);
        CHECK(end < start);
        CHECK(end <= Fixed(1).raw + 4096);   // 사거리 안까지만 오고 지나치지 않는다
        CHECK(end > 0);
    }

    dctest::section("전투 — 잠식은 피격 + 물량 두 축으로 찬다");
    {
        // 축 1: 피격
        World hit = makeWorld(4);
        SpawnDesc d = mob(Archetype::Trash, 0, Fixed(0), Fixed(0));
        d.attackDamage = Fixed(5);
        d.attackRange  = Fixed(2);
        (void)hit.entities.spawn(d, 0, 4);
        const int32_t before = hit.hero.corruption.raw;
        for (int i = 0; i < 100; ++i) { combatRun(hit, cfg); hit.beginTick(); hit.endTick(); }
        CHECK(hit.hero.corruption.raw > before);

        // 축 2: 물량 — 임계(20)를 넘으면 때리지 않아도 찬다.
        // **이게 "몬스터 수 상한 = 게임오버"를 게이지 하나로 흡수한 장치다.**
        World mass = makeWorld(4);
        SpawnDesc far_ = mob(Archetype::Trash, 0, Fixed(100), Fixed(0));
        far_.attackRange = Fixed(1);
        far_.approachSpeed = Fixed{};        // 접근하지 않으므로 피격은 0이다
        for (int i = 0; i < 30; ++i) (void)mass.entities.spawn(far_, 0, 4);
        const int32_t m0 = mass.hero.corruption.raw;
        for (int i = 0; i < 100; ++i) { combatRun(mass, cfg); mass.beginTick(); mass.endTick(); }
        printf("    피격 0인데 물량 30마리로 5초간 잠식 +%d raw\n", mass.hero.corruption.raw - m0);
        CHECK(mass.hero.corruption.raw > m0);

        // 임계 미만이면 물량 충전이 없다.
        World few = makeWorld(4);
        for (int i = 0; i < 10; ++i) (void)few.entities.spawn(far_, 0, 4);
        const int32_t f0 = few.hero.corruption.raw;
        for (int i = 0; i < 100; ++i) { combatRun(few, cfg); few.beginTick(); few.endTick(); }
        CHECK_EQ(few.hero.corruption.raw, f0);
    }

    dctest::section("틱 루프 — 재현성");
    {
        auto play = [&](uint64_t seed, int32_t ticks) {
            World w = makeWorld(seed);
            for (int32_t i = 0; i < ticks; ++i) stepWorld(w, cfg);
            return w.checksum();
        };
        CHECK_EQU(play(20250921, 2000), play(20250921, 2000));
        CHECK(play(20250921, 2000) != play(20250922, 2000));
        printf("    2000틱 × 2회 일치\n");
    }

    dctest::section("틱 루프 — 죽음은 틱 끝에만 반영된다");
    {
        World w = makeWorld(6);
        const EntityId a = w.entities.spawn(mob(Archetype::Trash, 0, Fixed(1), Fixed(0), 1), 0, 6);
        w.hero.target = a;
        combatRun(w, cfg);                       // 여기서 죽음이 표시되고
        CHECK_EQ(w.entities.count(), 1u);        // 행은 아직 남아 있다
        CHECK(!w.entities.alive(a));             // 그런데 시스템은 죽은 것으로 본다
        w.beginTick();
        w.endTick();                             // 틱 끝에 일괄 압축
        CHECK_EQ(w.entities.count(), 0u);
    }

    return dctest::summary("test_systems");
}
