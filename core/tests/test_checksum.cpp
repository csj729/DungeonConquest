// checksum() 테스트.
//
// 체크섬은 **계측 장비다.** 장비가 틀리면 그 위의 모든 검증이 무의미하므로,
// 여기서 확인할 것은 두 방향 모두다:
//   거짓 음성 — 상태가 바뀌었는데 해시가 같다  (빠뜨린 필드)
//   거짓 양성 — 상태가 같은데 해시가 다르다    (파생값을 넣었거나 순서 의존)
#include "../include/dc/checksum.h"
#include "../include/dc/replay.h"
#include "../include/dc/snapshot.h"
#include "../include/dc/world.h"
#include "../tools/dev_script.h"
#include "test_main.h"

using namespace dc;

int main() {
    printf("test_checksum\n");

    dctest::section("Hasher 기본 성질");
    {
        Hasher a, b;
        CHECK_EQU(a.value(), b.value());          // 같은 출발점
        a.feed(uint64_t{1});
        CHECK(a.value() != b.value());

        // 순서에 민감해야 한다. 순서가 섞여도 같은 해시가 나오면
        // "엔티티 순서가 뒤바뀐 런"을 잡지 못한다.
        Hasher x, y;
        x.feed(int32_t{1}); x.feed(int32_t{2});
        y.feed(int32_t{2}); y.feed(int32_t{1});
        CHECK(x.value() != y.value());

        // 좁은 타입은 부호 확장하지 않는다 — int32 -1과 int64 -1이 달라야 한다.
        Hasher p, q;
        p.feed(int32_t{-1});
        q.feed(int64_t{-1});
        CHECK(p.value() != q.value());

        // 0을 넣은 것과 아무것도 안 넣은 것이 구별돼야 한다.
        Hasher z, empty;
        z.feed(uint64_t{0});
        CHECK(z.value() != empty.value());

        // 타입 어댑터가 실제로 값을 전달하는지.
        Hasher f1, f2;
        f1.feed(Fixed::fromRaw(7));
        f2.feed(Fixed::fromRaw(8));
        CHECK(f1.value() != f2.value());
        Hasher e1, e2;
        e1.feed(EntityId::make(1, 1));
        e2.feed(EntityId::make(1, 2));
        CHECK(e1.value() != e2.value());
    }

    dctest::section("total은 부분들의 함수");
    {
        // **"total은 갈렸는데 부분은 전부 같다"가 구조적으로 불가능해야 한다.**
        // 그래야 분기 지점을 항상 특정할 수 있다.
        Checksums a, b;
        a.entities = 11; a.hero = 22; a.run = 33; a.spawn = 44; a.rng = 55;
        b = a;
        a.seal(100, 7);
        b.seal(100, 7);
        CHECK_EQU(a.total, b.total);

        b.hero = 23;
        b.seal(100, 7);
        CHECK(a.total != b.total);
        CHECK(a != b);

        // 틱 번호와 시드도 total에 들어간다 — 같은 상태가 다른 틱에 있으면 다르다.
        Checksums c = a;
        c.seal(101, 7);
        CHECK(a.total != c.total);
        Checksums d = a;
        d.seal(100, 8);
        CHECK(a.total != d.total);
    }

    dctest::section("분기 영역 특정");
    {
        Checksums a, b;
        a.seal(0, 0);
        b = a;
        CHECK(firstDivergentDomain(a, b) == nullptr);

        b.hero = 1; b.seal(0, 0);
        CHECK(firstDivergentDomain(a, b) != nullptr);
        CHECK_EQ(__builtin_strcmp(firstDivergentDomain(a, b), "hero"), 0);

        Checksums c = a;
        c.entities = 9; c.seal(0, 0);
        CHECK_EQ(__builtin_strcmp(firstDivergentDomain(a, c), "entities"), 0);
    }

    dctest::section("거짓 음성 — 모든 상태 필드가 해시에 잡히는가");
    {
        // World의 필드를 하나씩 건드려 체크섬이 반드시 움직이는지 확인한다.
        // **필드를 추가하고 hashInto()에 넣는 걸 잊으면 여기서 걸린다.**
        World base;
        base.init(2024);
        dev::runScript(base, 120);          // 엔티티가 실제로 들어있는 상태에서
        const uint64_t ref = base.checksum();

        struct Probe { const char* name; void (*mutate)(World&); };
        const Probe probes[] = {
            {"hero.posX",        [](World& w){ w.hero.posX = Fixed(1); }},
            {"hero.posY",        [](World& w){ w.hero.posY = Fixed(1); }},
            {"hero.corruption",  [](World& w){ w.hero.corruption = w.hero.corruption + Fixed::fromRaw(1); }},
            {"hero.corruptMax",  [](World& w){ w.hero.stats.setBase(Stat::CorruptionMax, Fixed(999)); }},
            {"hero.level",       [](World& w){ w.hero.level += 1; }},
            {"hero.exp",         [](World& w){ w.hero.exp += 1; }},
            {"hero.atkCooldown", [](World& w){ w.hero.attackCooldown += 1; }},
            {"hero.qteCooldown", [](World& w){ w.hero.qteCooldown += 1; }},
            {"hero.proc.trials", [](World& w){ w.hero.proc.trials += 1; }},
            {"hero.target",      [](World& w){ w.hero.target = EntityId::make(9, 9); }},
            {"run.mapIndex",     [](World& w){ w.run.mapIndex += 1; }},
            {"run.segmentIndex", [](World& w){ w.run.segmentIndex += 1; }},
            {"run.clearPoints",  [](World& w){ w.run.clearPoints += 1; }},
            {"run.killedTrash",  [](World& w){ w.run.killedTrash += 1; }},
            {"run.killedElite",  [](World& w){ w.run.killedElite += 1; }},
            {"run.bossAlive",    [](World& w){ w.run.bossAlive = !w.run.bossAlive; }},
            {"spawn.nextTick",   [](World& w){ w.spawn.nextSpawnTick += 1; }},
            {"spawn.dirCursor",  [](World& w){ w.spawn.directionCursor += 1; }},
            {"spawn.total",      [](World& w){ w.spawn.spawnedTotal += 1; }},
            {"rngSpawn",         [](World& w){ (void)w.rngSpawn.nextU64(); }},
            {"rngCombat",        [](World& w){ (void)w.rngCombat.nextU64(); }},
            {"rngCards",         [](World& w){ (void)w.rngCards.nextU64(); }},
            {"rngItems",         [](World& w){ (void)w.rngItems.nextU64(); }},
            {"rngEvents",        [](World& w){ (void)w.rngEvents.nextU64(); }},
            {"ent.posX",         [](World& w){ w.entities.posX[0] = Fixed(5); }},
            {"ent.posY",         [](World& w){ w.entities.posY[0] = Fixed(5); }},
            {"ent.archetype",    [](World& w){ w.entities.archetype[0] = Archetype::Boss; }},
            {"ent.flags",        [](World& w){ w.entities.flags[0] ^= EntityFlag::Ranged; }},
            {"ent.maxHp",        [](World& w){ w.entities.maxHp[0] = Fixed(777); }},
            {"ent.damageTaken",  [](World& w){ w.entities.damageTaken[0] = Fixed(3); }},
            {"ent.armor",        [](World& w){ w.entities.armor[0] = Fixed(3); }},
            {"ent.attackDamage", [](World& w){ w.entities.attackDamage[0] = Fixed(3); }},
            {"ent.approachSpd",  [](World& w){ w.entities.approachSpeed[0] = Fixed(3); }},
            {"ent.atkCooldown",  [](World& w){ w.entities.attackCooldown[0] += 1; }},
            {"ent.windupLeft",   [](World& w){ w.entities.windupLeft[0] += 1; }},
            {"ent.patternIndex", [](World& w){ w.entities.patternIndex[0] += 1; }},
            {"ent.patternCd",    [](World& w){ w.entities.patternCooldown[0] += 1; }},
            {"ent.ccGauge",      [](World& w){ w.entities.ccGauge[0] = Fixed(1); }},
            {"ent.ccGaugeMax",   [](World& w){ w.entities.ccGaugeMax[0] = Fixed(1); }},
            {"ent.ccTrigger",    [](World& w){ w.entities.ccTriggerCount[0] += 1; }},
            {"ent.typeId",       [](World& w){ w.entities.typeId[0] += 1; }},
            {"ent.spawnTick",    [](World& w){ w.entities.spawnTick[0] += 1; }},
            {"ent.rngState",     [](World& w){ w.entities.rngState[0] ^= 1ull; }},
            {"ent.markDead",     [](World& w){ w.entities.markDead(w.entities.idAt(0)); }},
            {"ent.spawn",        [](World& w){ SpawnDesc d; (void)w.entities.spawn(d, 0, 1); }},
        };

        int missed = 0;
        for (const Probe& p : probes) {
            World w = base;
            p.mutate(w);
            if (w.checksum() == ref) { ++missed; printf("    누락: %s\n", p.name); }
        }
        CHECK_EQ(missed, 0);
        printf("    상태 필드 %zu개 전부 해시에 반영됨\n", sizeof(probes) / sizeof(probes[0]));
    }

    dctest::section("틱 중 삭제 호출 순서는 결과에 영향이 없다");
    {
        // 안정 압축의 부수 효과: compact()는 **dense 순서로** 슬롯을 반납하므로
        // 같은 틱 안에서 markDead를 어떤 순서로 부르든 자유 목록까지 동일해진다.
        // swap-remove였다면 성립하지 않는다. 시스템 순서를 바꿔도 안전하다는 뜻이라
        // 명시적으로 고정해둔다.
        World a, b;
        a.init(5);
        b.init(5);
        for (int i = 0; i < 6; ++i) {
            SpawnDesc d;
            d.maxHp = Fixed(i + 1);
            (void)a.entities.spawn(d, 0, 5);
            (void)b.entities.spawn(d, 0, 5);
        }
        a.entities.markDead(a.entities.idAt(1));
        a.entities.markDead(a.entities.idAt(4));
        b.entities.markDead(b.entities.idAt(4));   // 역순 호출
        b.entities.markDead(b.entities.idAt(1));
        a.applyDeaths();
        b.applyDeaths();
        CHECK_EQU(a.checksum(), b.checksum());
        CHECK(a.entities.spawn(SpawnDesc{}, 0, 5) == b.entities.spawn(SpawnDesc{}, 0, 5));
    }

    dctest::section("거짓 음성 — 자유 슬롯 목록도 상태다");
    {
        // 자유 슬롯의 순서·세대는 **다음 스폰이 받을 EntityId**를 정한다.
        // 해시에서 빠지면 분기를 한 틱 늦게 잡는다.
        //
        // 살아있는 엔티티는 완전히 같지만 자유 목록만 다른 두 World를 만든다.
        //   a: 3기 스폰 → dense 2 처치     → 자유 목록 top = 슬롯 2
        //   b: 4기 스폰 → dense 2,3 처치   → 자유 목록 top = 슬롯 3
        World a, b;
        a.init(5);
        b.init(5);
        SpawnDesc keep;
        keep.maxHp = Fixed(20);

        for (int i = 0; i < 3; ++i) (void)a.entities.spawn(keep, 0, 5);
        for (int i = 0; i < 4; ++i) (void)b.entities.spawn(keep, 0, 5);
        a.entities.markDead(a.entities.idAt(2));
        b.entities.markDead(b.entities.idAt(2));
        b.entities.markDead(b.entities.idAt(3));
        a.applyDeaths();
        b.applyDeaths();

        // 살아남은 행은 완전히 동일하다 — 수·ID·필드 전부.
        CHECK_EQ(a.entities.count(), b.entities.count());
        bool rowsIdentical = (a.entities.count() == b.entities.count());
        for (uint32_t i = 0; i < a.entities.count(); ++i) {
            if (!(a.entities.idAt(i) == b.entities.idAt(i))) rowsIdentical = false;
            if (a.entities.maxHp[i].raw != b.entities.maxHp[i].raw) rowsIdentical = false;
        }
        CHECK(rowsIdentical);

        // 그런데 다음 스폰은 다른 슬롯을 받는다.
        World a2 = a, b2 = b;
        const EntityId na = a2.entities.spawn(keep, 0, 5);
        const EntityId nb = b2.entities.spawn(keep, 0, 5);
        CHECK(na != nb);
        printf("    살아있는 행은 동일, 다음 스폰 ID는 a=%u/%u b=%u/%u\n",
               na.index(), na.generation(), nb.index(), nb.generation());

        // **따라서 체크섬도 이미 갈라져 있어야 한다** — 한 틱 늦게가 아니라 지금.
        CHECK(a.checksum() != b.checksum());
        CHECK(a.checksums().entities != b.checksums().entities);
        CHECK_EQU(a.checksums().hero, b.checksums().hero);     // 다른 영역은 동일
    }

    dctest::section("거짓 양성 — 같은 상태는 같은 해시");
    {
        // 서로 다른 경로로 같은 상태에 도달하면 해시가 같아야 한다.
        World a, b;
        a.init(31337);
        b.init(31337);
        dev::runScript(a, 500);
        dev::runScript(b, 250);
        dev::runScript(b, 250);
        CHECK_EQU(a.checksum(), b.checksum());

        // 압축을 한 번 더 불러도(죽은 게 없으므로 무동작) 해시는 그대로다.
        const uint64_t before = a.checksum();
        a.applyDeaths();
        a.applyDeaths();
        CHECK_EQU(a.checksum(), before);
    }

    dctest::section("재현성 하네스");
    {
        // CLAUDE.md의 "같은 시드 N회 실행 → 매 틱 checksum() 일치" 그 자체.
        constexpr int32_t TICKS = 1200;
        static Checksums scratch[TICKS + 1];
        const DivergenceReport rep = verifyReproducible(
            0xA5A5A5A5, TICKS, 5, scratch, TICKS + 1,
            [](World& w) { dev::scriptTick(w); });

        CHECK(rep.ok);
        if (!rep.ok) {
            printf("    분기: 틱 %d · %d회차 · 영역 %s\n", rep.tick, rep.run,
                   rep.domain ? rep.domain : "?");
        }
        CHECK_EQ(rep.ticksRun, TICKS);
        printf("    %d틱 × 5회 일치, 최종 체크섬 %llu\n", TICKS,
               static_cast<unsigned long long>(rep.finalChecksum));
    }

    dctest::section("하네스가 분기를 실제로 잡는가");
    {
        // 하네스 자체를 검증한다. **일부러 갈라놓고** 최초 틱·영역이 맞는지 본다.
        // 이게 없으면 "항상 ok를 돌려주는 하네스"도 통과한다.
        constexpr int32_t TICKS = 200;
        static Checksums scratch[TICKS + 1];
        static int32_t callCount = 0;
        callCount = 0;

        const DivergenceReport rep = verifyReproducible(
            777, TICKS, 2, scratch, TICKS + 1,
            [](World& w) {
                dev::scriptTick(w);
                // 2회차의 143틱에서만 hero를 한 번 건드린다.
                ++callCount;
                if (callCount == TICKS + 143) w.hero.level += 1;
            });

        CHECK(!rep.ok);
        CHECK_EQ(rep.tick, 143);       // **최초로 갈라진 틱**
        CHECK_EQ(rep.run, 1);
        CHECK(rep.domain != nullptr);
        CHECK_EQ(__builtin_strcmp(rep.domain, "hero"), 0);
        CHECK(rep.expected.hero != rep.actual.hero);
        CHECK(rep.expected.entities == rep.actual.entities);   // 다른 영역은 멀쩡
        printf("    잡아냄: 틱 %d · %d회차 · 영역 %s\n", rep.tick, rep.run, rep.domain);
    }

    dctest::section("스냅샷 덤프/로드");
    {
        static uint8_t buf[snapshotSize()];
        World w;
        w.init(1234);
        dev::runScript(w, 300);
        const uint64_t at300 = w.checksum();

        CHECK_EQ(snapshotSave(w, buf, sizeof(buf)), snapshotSize());
        CHECK_EQ(snapshotSave(w, buf, 4), 0u);      // 버퍼가 작으면 거부

        dev::runScript(w, 300);
        const uint64_t at600 = w.checksum();
        CHECK(at600 != at300);

        World r;
        r.init(0);
        CHECK_EQ(static_cast<int32_t>(snapshotLoad(r, buf, sizeof(buf))),
                 static_cast<int32_t>(SnapshotStatus::Ok));
        CHECK_EQU(r.checksum(), at300);
        CHECK_EQ(r.tickCount(), 300);

        // 이어 돌리면 같은 궤적.
        dev::runScript(r, 300);
        CHECK_EQU(r.checksum(), at600);
    }

    dctest::section("스냅샷 거부 조건");
    {
        static uint8_t buf[snapshotSize()];
        World w;
        w.init(9);
        dev::runScript(w, 50);
        snapshotSave(w, buf, sizeof(buf));

        World r;
        r.init(0);
        const uint64_t untouched = r.checksum();

        auto corrupt = [&](uint32_t offset, uint8_t xorMask) {
            static uint8_t tmp[snapshotSize()];
            for (uint32_t i = 0; i < snapshotSize(); ++i) tmp[i] = buf[i];
            tmp[offset] = static_cast<uint8_t>(tmp[offset] ^ xorMask);
            return snapshotLoad(r, tmp, snapshotSize());
        };

        CHECK_EQ(static_cast<int32_t>(corrupt(0, 0xFF)),
                 static_cast<int32_t>(SnapshotStatus::BadMagic));
        CHECK_EQ(static_cast<int32_t>(corrupt(4, 0xFF)),
                 static_cast<int32_t>(SnapshotStatus::BadVersion));
        CHECK_EQ(static_cast<int32_t>(corrupt(8, 0xFF)),
                 static_cast<int32_t>(SnapshotStatus::SizeMismatch));
        // 본문 한 바이트를 뒤집으면 체크섬이 잡는다.
        CHECK_EQ(static_cast<int32_t>(corrupt(sizeof(SnapshotHeader) + 8, 0x01)),
                 static_cast<int32_t>(SnapshotStatus::ChecksumMismatch));
        CHECK_EQ(static_cast<int32_t>(snapshotLoad(r, buf, 8)),
                 static_cast<int32_t>(SnapshotStatus::TooSmall));

        // **실패했으면 대상 World를 건드리지 않았어야 한다.**
        CHECK_EQU(r.checksum(), untouched);
    }

    return dctest::summary("test_checksum");
}
