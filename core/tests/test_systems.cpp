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

    dctest::section("이동 — 몹 추격도 사거리까지만");
    {
        World w = makeWorld(2);
        SpawnDesc d = mob(Archetype::Trash, 0, Fixed(20), Fixed(0));
        d.approachSpeed = cfg.trash.approachSpeed;
        d.attackRange   = Fixed(1);
        const EntityId m = w.entities.spawn(d, 0, 2);
        const int32_t start = w.entities.posX[0].raw;

        // 이동은 combatRun이 아니라 movementRun이 한다 (전투와 이동을 분리했다).
        for (int i = 0; i < 400; ++i) { movementRun(w, cfg); w.beginTick(); w.endTick(); }
        const int32_t end = w.entities.posX[static_cast<uint32_t>(w.entities.denseOf(m))].raw;
        printf("    20타일 → %.3f타일 (사거리 1.0, 400틱)\n",
               static_cast<double>(end) / Fixed::ONE_RAW);
        CHECK(end < start);
        // **사거리를 지나치지 않는다.** 지나치면 경계에서 매 틱 앞뒤로 떨린다.
        CHECK(end <= Fixed(1).raw + 16);
        CHECK(end >= Fixed(1).raw - 16);
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

    dctest::section("이동 — 추격 후 정지 (도주·선회 없음)");
    {
        // 영웅은 타겟을 향해 걸어가 사거리에서 멈춘다. 그 이상도 이하도 없다.
        World w = makeWorld(21);
        SpawnDesc d = mob(Archetype::Trash, 0, Fixed(20), Fixed(0));
        d.approachSpeed = Fixed{};               // 몹은 가만히 둔다
        const EntityId m = w.entities.spawn(d, 0, 21);
        w.hero.target = m;

        const Fixed range = w.hero.stats.value(Stat::Range);
        for (int i = 0; i < 600; ++i) { movementRun(w, cfg); w.beginTick(); w.endTick(); }
        const Fixed dx = w.entities.posX[0] - w.hero.posX;
        printf("    타겟까지 %.3f타일 (사거리 %.1f)\n",
               static_cast<double>(dx.raw) / Fixed::ONE_RAW,
               static_cast<double>(range.raw) / Fixed::ONE_RAW);

        // **사거리를 지나치지 않는다** — 지나치면 경계에서 매 틱 떨린다.
        CHECK(dx.raw <= range.raw + 16);
        CHECK(dx.raw >= range.raw - 16);
        // 영웅이 타겟 쪽을 향한다 (연출이 읽는 값)
        CHECK(w.hero.facingX.raw > 0);

        // 도달한 뒤에는 움직이지 않는다.
        const int32_t settled = w.hero.posX.raw;
        for (int i = 0; i < 100; ++i) { movementRun(w, cfg); w.beginTick(); w.endTick(); }
        CHECK_EQ(w.hero.posX.raw, settled);
    }

    dctest::section("이동 — 타겟이 없으면 움직이지 않는다");
    {
        World w = makeWorld(22);
        const int32_t x0 = w.hero.posX.raw, y0 = w.hero.posY.raw;
        for (int i = 0; i < 100; ++i) { movementRun(w, cfg); w.beginTick(); w.endTick(); }
        CHECK_EQ(w.hero.posX.raw, x0);
        CHECK_EQ(w.hero.posY.raw, y0);
    }

    dctest::section("적 간 충돌 — 겹치지 않는다");
    {
        // 분리가 없으면 몹 전부가 영웅 한 점에 겹친다. 실측으로 확인한 결과이며
        // (동시 접촉 26마리 = 가정의 5배) 이 시스템이 그 패킹을 만든다.
        World w = makeWorld(31);
        SimScratch scratch;
        SpawnDesc d = mob(Archetype::Trash, 0, Fixed(0), Fixed(0));
        d.approachSpeed = Fixed{};
        for (int i = 0; i < 40; ++i) (void)w.entities.spawn(d, 0, 31);   // 전부 같은 좌표

        for (int i = 0; i < 200; ++i) separationRun(w, cfg, scratch);

        const int64_t mobSep  = (static_cast<int64_t>(cfg.separationMilli) * Fixed::ONE_RAW) / 1000;
        const int64_t heroSep = (static_cast<int64_t>(cfg.heroSeparationMilli) * Fixed::ONE_RAW) / 1000;
        int32_t tooClose = 0, insideHero = 0;
        for (uint32_t i = 0; i < w.entities.count(); ++i) {
            const int64_t hd = static_cast<int64_t>(isqrt64(static_cast<uint64_t>(
                distanceSq(w.entities.posX[i], w.entities.posY[i], w.hero.posX, w.hero.posY))));
            if (hd < heroSep - 64) ++insideHero;
            for (uint32_t j = i + 1; j < w.entities.count(); ++j) {
                const int64_t dd = static_cast<int64_t>(isqrt64(static_cast<uint64_t>(
                    distanceSq(w.entities.posX[i], w.entities.posY[i],
                               w.entities.posX[j], w.entities.posY[j]))));
                if (dd < mobSep - 64) ++tooClose;
            }
        }
        printf("    40마리를 한 점에 겹쳐 놓고 200틱 — 겹침 %d쌍, 영웅 이격 위반 %d\n",
               tooClose, insideHero);
        CHECK_EQ(tooClose, 0);
        CHECK_EQ(insideHero, 0);
    }

    dctest::section("적 간 충돌 — 영웅은 밀리지 않는다");
    {
        // 영웅이 밀리면 이동 AI의 결정이 뒤집힌다.
        World w = makeWorld(32);
        SimScratch scratch;
        SpawnDesc d = mob(Archetype::Trash, 0, Fixed(0), Fixed(0));
        d.approachSpeed = Fixed{};
        for (int i = 0; i < 20; ++i) (void)w.entities.spawn(d, 0, 32);
        const int32_t hx = w.hero.posX.raw, hy = w.hero.posY.raw;
        for (int i = 0; i < 100; ++i) separationRun(w, cfg, scratch);
        CHECK_EQ(w.hero.posX.raw, hx);
        CHECK_EQ(w.hero.posY.raw, hy);
    }

    dctest::section("균등 그리드 — counting sort가 전수를 담는다");
    {
        World w = makeWorld(33);
        SimScratch scratch;
        Rng r = Rng::derive(33, RngStream::Spawn);
        for (int i = 0; i < 300; ++i) {
            SpawnDesc d = mob(Archetype::Trash, 0,
                              Fixed::fromRaw(static_cast<int32_t>(r.range(400000)) - 200000),
                              Fixed::fromRaw(static_cast<int32_t>(r.range(400000)) - 200000));
            (void)w.entities.spawn(d, 0, 33);
        }
        scratch.grid.build(w.entities, Fixed::fromPermille(1500), w.hero.posX, w.hero.posY);

        // 모든 엔티티가 정확히 한 번씩 들어 있어야 한다 — 빠지면 충돌을 놓친다.
        int32_t seen[1024] = {0};
        for (uint32_t c = 0; c < UniformGrid::CELLS; ++c) {
            for (uint32_t s2 = scratch.grid.begin(c); s2 < scratch.grid.end(c); ++s2) {
                ++seen[scratch.grid.item(s2)];
                CHECK_EQ(scratch.grid.cellOf(scratch.grid.item(s2)), c);
            }
        }
        bool exactlyOnce = true;
        for (uint32_t i = 0; i < w.entities.count(); ++i) if (seen[i] != 1) exactlyOnce = false;
        CHECK(exactlyOnce);
        printf("    300기 격자 배치 — 전원 정확히 1회\n");
    }

    dctest::section("틱 루프 — 재현성");
    {
        auto play = [&](uint64_t seed, int32_t ticks) {
            World w = makeWorld(seed);
            static SimScratch scratch;
            for (int32_t i = 0; i < ticks; ++i) stepWorld(w, cfg, scratch);
            return w.checksum();
        };
        CHECK_EQU(play(20250921, 2000), play(20250921, 2000));
        CHECK(play(20250921, 2000) != play(20250922, 2000));
        printf("    2000틱 × 2회 일치\n");
    }

    dctest::section("구간 진행 — 게이지가 구간을 넘긴다");
    {
        World w = makeWorld(11);
        // 게이지를 직접 밀어 경계마다 구간이 정확히 한 칸씩 오르는지 본다.
        for (int32_t seg = 0; seg < cfg.segmentsPerMap; ++seg) {
            w.run.clearPoints = seg * cfg.clearPointsPerSegment;
            progressRun(w, cfg);
            CHECK_EQ(w.run.segmentIndex, seg);
            // 경계 직전은 아직 이전 구간이다
            w.run.clearPoints = (seg + 1) * cfg.clearPointsPerSegment - 1;
            progressRun(w, cfg);
            CHECK_EQ(w.run.segmentIndex, seg);
        }
        // 마지막 구간을 넘겨도 표 밖으로 나가지 않는다
        w.run.clearPoints = cfg.clearPointsPerSegment * 1000;
        progressRun(w, cfg);
        CHECK_EQ(w.run.segmentIndex, cfg.segmentsPerMap - 1);
        // **되돌아가지 않는다** — 게이지가 깎여도 구간은 유지된다
        w.run.clearPoints = 0;
        progressRun(w, cfg);
        CHECK_EQ(w.run.segmentIndex, cfg.segmentsPerMap - 1);

        // 램프가 실제로 살아 있는가 — 배치 표 첫 칸만 쓰이면 난이도가 평평해진다
        CHECK(cfg.batchFor(cfg.segmentsPerMap) > cfg.batchFor(1));
        printf("    구간 1 → %d: 배치 %d → %d · 상한 %d → %d\n",
               cfg.segmentsPerMap, cfg.batchFor(1), cfg.batchFor(cfg.segmentsPerMap),
               cfg.capFor(1), cfg.capFor(cfg.segmentsPerMap));
    }

    dctest::section("틱 루프 — 교착 금지 (분리가 이동보다 먼저다)");
    {
        // **회귀 테스트.** 분리를 이동 뒤에 두면 전투가 영구히 멎는다.
        //
        // 이동이 타겟을 사거리 안으로 들여놓아도 분리가 도로 밀어내고, 전투는
        // 밀려난 좌표를 본다 — 실측에서 영웅이 2.877타일까지 붙었는데(사거리 3.0)
        // 분리가 3.027로 되돌려 1200초 내내 처치가 0이었다. 잡몹 무리에 둘러싸인
        // 채 멀리 있는 엘리트를 쫓을 때 터지므로 **단위 테스트로는 안 잡히고
        // 긴 런에서만 드러난다.** 그래서 여기서 긴 런을 직접 돌린다.
        World w = makeWorld(20250921);
        static SimScratch scratch;
        int32_t prevKills = 0, worstGap = 0, gap = 0;
        for (int32_t i = 0; i < 6000; ++i) {     // 300초
            stepWorld(w, cfg, scratch);
            const int32_t kills = w.run.killedTrash + w.run.killedElite;
            if (kills > prevKills) { prevKills = kills; gap = 0; }
            else if (aliveCount(w.entities) > 0) {
                ++gap;
                if (gap > worstGap) worstGap = gap;
            }
        }
        // 상한을 60초(1200틱)로 잡는 근거: 정상 런의 최장 공백은 **엘리트 한 마리를
        // 때려잡는 시간**이다 (체력 100 · 방어 관통 후 초당 약 7 → 약 19초 = 380틱).
        // 우선순위가 영웅을 엘리트에 묶어두는 동안 잡몹 처치가 멎기 때문이다.
        // 교착은 그 스케일이 아니라 **영구**였다 — 24000틱을 돌려도 0이었다.
        // 그래서 정상 최장의 3배에 선을 긋는다: 실제 동작에는 닿지 않고
        // 교착은 확실히 걸린다.
        CHECK(worstGap < 1200);
        CHECK(prevKills > 0);
        // 게이지가 실제로 오르는가 — 교착의 최종 증상은 게이지 정지였다.
        CHECK(w.run.clearPoints > 100);
        printf("    300초 런 — 처치 %d · 게이지 %d · 적 생존 중 최장 무처치 공백 %d틱\n",
               prevKills, w.run.clearPoints, worstGap);
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
