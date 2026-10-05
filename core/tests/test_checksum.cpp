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
            [](World& w) { dev::scriptTick(w, dev::data().cfg, dev::data().table); });

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
                dev::scriptTick(w, dev::data().cfg, dev::data().table);
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

    dctest::section("드라이버가 실제 조합 사다리를 탄다");
    {
        // **회귀 테스트.** 드라이버가 인벤토리를 축소 테이블(조합식 12개)로
        // 초기화하고 입력에는 실제 테이블을 넘기던 시절, 해시가 달라
        // `Inventory::matches`가 걸러내 **아이템 뽑기 카드 선택이 전부 조용히
        // 거부됐다.** 게다가 축소 테이블에는 특별함 이상 조합식이 없어
        // 사다리가 2단에서 막혀 있었다 (실측 등급 분포 0:53 · 1:79 · 2~4: 0).
        //
        // 증상이 크래시가 아니라 "고급 아이템이 안 나온다"라서, 체크섬이
        // 일치하는 동안에는 아무도 모른다.
        World w;
        w.init(1);
        dev::runScript(w, 1200);

        const RecipeTable& table = dev::data().table;
        const SimConfig&   cfg   = dev::data().cfg;

        // 인벤토리가 **실제** 테이블을 들고 있어야 입력이 받아들여진다
        CHECK(w.inventory.matches(table));

        int32_t total = 0;
        int32_t aboveCommon = 0, topTier = 0;
        for (uint32_t i = 0; i < cfg.itemTypeCount; ++i) {
            const int32_t c = w.inventory.count(static_cast<ItemId>(i));
            if (c <= 0) continue;
            total += c;
            if (cfg.itemTier[i] > 0) aboveCommon += c;
            if (cfg.itemTier[i] > topTier) topTier = cfg.itemTier[i];
        }
        CHECK(total > 0);
        CHECK(aboveCommon > 0);          // 조합이 실제로 일어났다
        // **사다리 끝까지 올라간다.** 축소 테이블 시절엔 1단이 상한이었다.
        CHECK_EQ(topTier, static_cast<int32_t>(cfg.itemTierCount) - 1);
        printf("    1200틱 후 %d개 보유 · 최고 등급 %d/%d · 흔함 초과 %d개\n",
               total, topTier, cfg.itemTierCount - 1, aboveCommon);
    }

    dctest::section("**고정 체크섬** — 시뮬 결과를 말없이 바꾸지 못하게 못 박는다");
    {
        // 설정 원천을 손으로 옮겨 적은 표(`dev_data.h`)에서 실제 JSON 로더로
        // 바꿀 때 이 절이 **실제로 차이를 잡았다.**
        //
        // dev_data.h의 영웅 스탯 하한이 `stats.json`과 달랐다 — 전 스탯에
        // `min_pct_add = -900`을 일괄로 적어 두었는데 데이터는 6개 스탯에
        // -1000을 말하고, `range`·`crit_mult`의 `min_value`(500 · 1000)는
        // 아예 빠져 있었다. 그 값만 되돌리면 전환 전 체크섬이 정확히
        // 복원되므로, **전환이 바꾼 것은 그 하한 하나뿐임이 증명된다.**
        //
        // 하한은 밸런스 다이얼이 아니라 불변식이다 (design.md §9) — 데이터가
        // 원천이고 C++가 그걸 다르게 들고 있었던 것이므로, 아래 값은 고친 뒤의
        // 것이다.
        //
        // 값에는 설정 지문(`World::dataHash_`)도 접혀 있다. 그래서 이 절은
        // **서버-클라 데이터 불일치 검사가 실제로 켜져 있는지**도 함께 지킨다 —
        // 지문을 체크섬에서 빼면 여기가 바로 빨간불이 된다.
        //
        // 두 번째로 이 절이 값을 바꾸게 만든 것은 드라이버의 축소 조합
        // 테이블을 걷어낸 변경이다 — 사다리가 2단에서 막혀 있던 것이 풀리면서
        // 보유 아이템이 달라졌다. 위 "조합 사다리" 절이 그 결과를 따로 못 박는다.
        //
        // 세 번째는 RL_FORGE(대장장이의 화로)다. **움직인 것은 설정 지문뿐이다** —
        // `cards.json`의 `forge_tier_rate_permille`과 `items.json`의 등급이 새로
        // 지문에 들어갔다. 세 변경(지문에 등급 추가 · 지문에 화로 확률 추가 ·
        // 화로 발동)을 하나씩 되돌려 확인했고, **앞의 둘만 되돌리면 이전 값이
        // 정확히 복원된다** — 즉 화로 자체는 이 런에서 한 번도 발동하지 않았다.
        // 전설 1.5% × 풀 9칸이라 1200틱 런이 화로를 뽑을 확률이 2% 남짓이기
        // 때문이다. 발동 경로는 `test_systems`의 RL_FORGE 두 절이 직접 못 박는다.
        //
        // 일곱 번째는 **광역 중심**이다. 대지 가르기가 "전방 범위"라는 설계대로
        // 아홉 번째는 균열에 지속 피해를 얹은 것이다(장판 둘 — 둔화 4.0타일 +
        // 피해 1.5타일). **또 설정 지문뿐이다.**
        //
        // 이번에는 옛 데이터로 돌려 볼 수 없었다 — 로더가 `dps_permille`를
        // 요구하게 되어 옛 cards.json이 **로드되지 않는다.** 대신 직전 검증에서
        // 적어 둔 영역값과 대조했고, 두 시드 모두 entities·hero·spawn·rng이
        // 동일하다. 균열 장판은 전설을 들고 있을 때만 깔리고, 이 런이 고유 각인을
        // 뽑지 않는다는 것은 아래 루프가 이미 검사한다.
        //
        // 여덟 번째는 직선 관통 기하 실측에 따른 수치 교정이다 — E_PIERCE
        // 75 → 165‰, 충격파 반폭 350 → 800. **또 설정 지문뿐이다.**
        // `dc_checksum`으로 영역별로 갈라 두 시드 모두 틱 0과 1200에서
        // entities·hero·spawn·rng이 완전히 동일함을 확인했다.
        //
        // E_PIERCE는 **공통** 카드인데 값이 2.2배 올랐는데도 entities가 한 비트도
        // 안 움직였다. 이 런이 그 카드를 집지 않기 때문이고, 아래에 단정으로
        // 넣어 두었다 — 골든 커버리지의 구멍이 또 하나 드러난 자리다.
        //
        // 일곱 번째는 원심력 상한 10 → 3이다. **또 설정 지문뿐이다.**
        // `dc_checksum <시드> 1200 1200`을 양쪽 데이터로 돌려 영역별로 갈라 봤고,
        // 두 시드 모두 틱 0과 1200에서 `entities`·`hero`·`spawn`·`rng`이
        // **완전히 동일**하다. `run`만 다른데 그 영역이 `dataHash_`를 먹는 자리다.
        // 아래 `legendHas` 루프가 이 런이 원심력을 뽑지 않음을 직접 검사하므로
        // 둘이 서로를 뒷받침한다.
        //
        // 영웅에서 타겟 방향 반경만큼 앞을 중심으로 삼는다 — 전에는 영웅 중심이라
        // 자기 타겟을 영영 때릴 수 없었다. 동작이 바뀌므로 값이 움직인다.
        // 실측: 엘리트 기절 틱 220 → 375, 클리어율 14.0% → 14.5%(노이즈 안).
        //
        // **여섯 번째는 처음으로 시뮬 동작이 바뀐 변경이다** (앞의 넷은 지문뿐이었다).
        // W_CLEAVE가 CC를 적용하고(잡몹 즉시 기절), 그로기가 이동까지 막고,
        // 그로기 카운트다운이 사거리 검사 앞으로 올라가고, Groggy 플래그가 내려간다.
        // 실측으로 런당 기절 틱이 218 → 1176으로 늘었다(그중 955가 잡몹).
        // 클리어율은 15.0% → 14.0%로 노이즈 안이다(n=200, SE 2.5%p).
        //
        // 다섯 번째는 고유 각인 2차(소용돌이·여진·균열 + 스킬 바인딩)다.
        // **움직인 것은 설정 지문과 체크섬 도메인뿐이다**:
        //   · `unique_engravings[].skill`이 지문에 들어갔다
        //   · **로더 순서가 바뀌었다** (skills가 cards보다 먼저여야 각인을 스킬에
        //     묶을 수 있다) → feed 순서가 바뀌므로 지문이 바뀐다
        //   · `ZoneState`가 spawn 영역 체크섬에 들어갔다
        // 시뮬 동작은 그대로다. 아래 루프가 **이 런이 고유 각인을 뽑지 않고 장판을
        // 깔지 않음을 직접 검사**하므로, 그 주장이 말이 아니라 검사로 남는다.
        //
        // 네 번째는 고유 각인 1차(처형·충격파·원심력)였다. **또 설정 지문뿐이다** —
        // `unique_engravings` 수치와 `boss_execute_immune`이 새로 지문에 들어갔다.
        // 효과를 셋 다 끈 빌드와 지문 feed만 뺀 빌드를 각각 돌려 확인했고,
        // **지문만 되돌리면 이전 값이 정확히 복원된다.** 즉 이 런은 세 각인을
        // 한 번도 뽑지 않는다(전설 1.5% × 풀 9칸).
        //
        // 그래서 발동 경로는 `test_legend`가 각인을 직접 쥐여 본다. 골든 체크섬이
        // 전설 효과를 못 보는 구조라는 뜻이고, 그게 그 파일이 따로 있는 이유다.
        //
        // 이 값이 깨지면 시뮬이 바뀐 것이다. **갱신하기 전에 왜 움직였는지
        // 답할 수 있어야 한다.**
        struct Golden { uint64_t seed; int32_t ticks; uint64_t total; };
        constexpr Golden kGolden[] = {
            {1u,        1200, 6792138982605955640ull},
            {20250918u, 1200, 12997662281239302898ull},
        };
        for (const Golden& g : kGolden) {
            World w;
            w.init(g.seed);
            dev::setup(w, dev::data().cfg, dev::data().table, dev::data().hero);
            for (int32_t t = 0; t < g.ticks; ++t) {
                dev::scriptTick(w, dev::data().cfg, dev::data().table);
            }
            CHECK_EQU(w.checksum(), g.total);

            // **"고유 각인이 안 뽑혀서 값이 안 변했다"를 말이 아니라 검사로 둔다.**
            // 위 주석이 매번 같은 주장을 반복하는데, 그 전제가 깨지는 날(드라이버
            // 정책이 바뀌거나 전설 확률이 오르면) 주석은 조용히 거짓이 된다.
            // 그러면 다음 사람이 체크섬이 움직인 이유를 잘못 짚는다.
            for (uint32_t u = legendIndexOf(LegendId::UniqueFirst); u < 9; ++u) {
                CHECK(!w.cards.legendHas(u));
            }
            CHECK_EQ(w.zones.count, 0u);     // 장판이 한 번도 깔리지 않았다

            // **E_PIERCE도 못 본다.** 값을 75 → 165‰로 2.2배 올렸는데 entities
            // 체크섬이 한 비트도 안 움직였다(dc_checksum 영역별 대조). 추측으로
            // 두지 않고 검사로 둔다 — 드라이버 정책이 바뀌어 이 카드를 집으면
            // 여기가 깨지고, 그러면 체크섬이 움직인 이유를 지문이 아니라 **효과**
            // 쪽에서 찾아야 한다는 신호가 된다.
            CHECK_EQ(w.cards.engrave[engraveIndex(EngraveId::Pierce)].raw, 0);
        }
        printf("    시드 %zu개 고정값 일치 · 고유 각인 미획득 · 장판 0개\n",
               sizeof(kGolden) / sizeof(kGolden[0]));
    }

    return dctest::summary("test_checksum");
}
