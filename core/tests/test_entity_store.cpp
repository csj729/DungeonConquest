// EntityStore 테스트 — 핸들 안정성과 압축의 정합성.
//
// 여기서 잡으려는 버그는 하나로 요약된다:
// **행이 이동했는데 무언가가 따라가지 않는 것.** 핸들 테이블이든 SoA 필드든
// 하나만 빠뜨려도 증상이 "가끔 엉뚱한 몹이 맞는다"라서 리플레이로만 잡힌다.
#include "../include/dc/entity_store.h"
#include "test_main.h"

using namespace dc;

// 각 엔티티에 고유한 지문을 심어둔다. 압축 후 지문이 ID와 맞는지로
// "SoA 필드가 행과 함께 이동했는가"를 전수 확인한다.
static SpawnDesc descFor(int32_t k) {
    SpawnDesc d;
    d.posX           = Fixed::fromRaw(k * 1000 + 1);
    d.posY           = Fixed::fromRaw(k * 1000 + 2);
    d.maxHp          = Fixed::fromRaw(k * 1000 + 3);
    d.armor          = Fixed::fromRaw(k * 1000 + 4);
    d.attackDamage   = Fixed::fromRaw(k * 1000 + 5);
    d.approachSpeed  = Fixed::fromRaw(k * 1000 + 6);
    d.ccGaugeMax     = Fixed::fromRaw(k * 1000 + 7);
    d.attackCooldownTicks = k * 10 + 1;
    d.windupTicks         = k * 10 + 2;
    d.typeId         = static_cast<uint16_t>(k + 1);
    d.archetype      = (k % 3 == 0) ? Archetype::Elite : Archetype::Trash;
    d.flags          = (k % 2 == 0) ? EntityFlag::Ranged : EntityFlag::None;
    return d;
}

static bool fingerprintOk(const EntityStore& s, uint32_t dense, int32_t k) {
    return s.posX[dense].raw           == k * 1000 + 1
        && s.posY[dense].raw           == k * 1000 + 2
        && s.maxHp[dense].raw          == k * 1000 + 3
        && s.armor[dense].raw          == k * 1000 + 4
        && s.attackDamage[dense].raw   == k * 1000 + 5
        && s.approachSpeed[dense].raw  == k * 1000 + 6
        && s.ccGaugeMax[dense].raw     == k * 1000 + 7
        && s.attackCooldown[dense]     == k * 10 + 1
        && s.windupLeft[dense]         == k * 10 + 2
        && s.typeId[dense]             == static_cast<uint16_t>(k + 1)
        && s.archetype[dense]          == ((k % 3 == 0) ? Archetype::Elite : Archetype::Trash)
        && s.flags[dense]              == ((k % 2 == 0) ? EntityFlag::Ranged : EntityFlag::None);
}

int main() {
    printf("test_entity_store\n");

    dctest::section("스폰 · 조회");
    {
        EntityStore s;
        s.init();
        CHECK_EQ(s.count(), 0u);
        CHECK_EQ(s.freeSlots(), EntityStore::CAPACITY);

        const EntityId a = s.spawn(descFor(0), 0, 12345);
        const EntityId b = s.spawn(descFor(1), 0, 12345);
        CHECK(a.valid() && b.valid());
        CHECK(a != b);
        CHECK_EQ(s.count(), 2u);
        CHECK_EQ(s.denseOf(a), 0);
        CHECK_EQ(s.denseOf(b), 1);
        CHECK(s.alive(a) && s.alive(b));

        // 첫 스폰이 슬롯 0을 받는다 — 덤프를 읽을 때 중요한 성질이라 고정해둔다.
        CHECK_EQ(a.index(), 0u);
        CHECK_EQ(b.index(), 1u);

        // 엔티티별 난수 스트림이 서로 달라야 한다. 같으면 같은 위치에서
        // 스폰된 몹들이 똑같이 굴린다.
        CHECK(s.rngState[0] != s.rngState[1]);
        CHECK(s.rngState[0] != 0);

        // 만들지 않은 핸들은 거부된다.
        CHECK(!s.alive(EntityId::invalid()));
        CHECK_EQ(s.denseOf(EntityId::invalid()), -1);
        CHECK_EQ(s.denseOf(EntityId::make(5, 1)), -1);   // 아직 안 쓴 슬롯
    }

    dctest::section("용량 상한");
    {
        EntityStore s;
        s.init();
        for (uint32_t i = 0; i < EntityStore::CAPACITY; ++i) {
            CHECK_EQ(s.spawn(descFor(0), 0, 1).valid() ? 1 : 0, 1);
        }
        CHECK(s.full());
        // 넘치면 invalid를 돌려준다 — 조용히 덮어쓰지 않는다.
        CHECK(!s.spawn(descFor(0), 0, 1).valid());
        CHECK_EQ(s.count(), EntityStore::CAPACITY);
    }

    dctest::section("틱 중간 삭제는 표시만");
    {
        EntityStore s;
        s.init();
        const EntityId a = s.spawn(descFor(0), 0, 7);
        const EntityId b = s.spawn(descFor(1), 0, 7);

        CHECK(s.markDead(a));
        CHECK(!s.alive(a));          // 시스템은 즉시 죽은 것으로 본다
        CHECK_EQ(s.count(), 2u);     // 그런데 행은 아직 남아 있다
        CHECK_EQ(s.deadPending(), 1u);
        CHECK(s.alive(b));
        CHECK(!s.markDead(a));       // 두 번 죽이면 false — 카운터 이중 증가 방지

        CHECK_EQ(s.compact(), 1u);
        CHECK_EQ(s.count(), 1u);
        CHECK_EQ(s.deadPending(), 0u);
        CHECK_EQ(s.denseOf(b), 0);   // 앞으로 당겨졌다
        CHECK_EQ(s.compact(), 0u);   // 죽은 게 없으면 조기 반환
    }

    dctest::section("압축 — 순서 보존 · 필드 동반 이동");
    {
        EntityStore s;
        s.init();
        EntityId id[10];
        for (int32_t k = 0; k < 10; ++k) id[static_cast<uint32_t>(k)] = s.spawn(descFor(k), k, 99);

        // 가운데를 여럿 죽인다. 앞·뒤·연속을 섞어 이동 거리가 다양해지게.
        s.markDead(id[0]);
        s.markDead(id[3]);
        s.markDead(id[4]);
        s.markDead(id[8]);
        CHECK_EQ(s.compact(), 4u);
        CHECK_EQ(s.count(), 6u);

        // 생존자가 **스폰 순서 그대로** 남아야 한다 (안정 압축).
        const int32_t expect[6] = {1, 2, 5, 6, 7, 9};
        for (uint32_t i = 0; i < 6; ++i) {
            const int32_t k = expect[i];
            CHECK(s.idAt(i) == id[static_cast<uint32_t>(k)]);
            CHECK_EQ(s.denseOf(id[static_cast<uint32_t>(k)]), static_cast<int32_t>(i));
            CHECK(s.alive(id[static_cast<uint32_t>(k)]));
            CHECK(fingerprintOk(s, i, k));     // SoA 19개 필드 전수 확인
            CHECK_EQ(s.spawnTick[i], k);
        }
        // 죽은 것들은 전부 거부된다.
        const int32_t gone[4] = {0, 3, 4, 8};
        for (uint32_t i = 0; i < 4; ++i) {
            CHECK(!s.alive(id[static_cast<uint32_t>(gone[i])]));
            CHECK_EQ(s.denseOf(id[static_cast<uint32_t>(gone[i])]), -1);
        }
    }

    dctest::section("슬롯 재사용 · stale 거부");
    {
        EntityStore s;
        s.init();
        const EntityId old = s.spawn(descFor(0), 0, 3);
        s.markDead(old);
        s.compact();
        CHECK_EQ(s.freeSlots(), EntityStore::CAPACITY);

        // 반납된 슬롯을 다시 쓸 때까지 스폰한다. LIFO라 바로 다음 스폰이 가져간다.
        const EntityId fresh = s.spawn(descFor(1), 1, 3);
        CHECK_EQ(fresh.index(), old.index());        // 같은 슬롯
        CHECK(fresh != old);                         // 다른 ID
        CHECK_EQ(fresh.generation(), old.generation() + 1);
        CHECK(s.alive(fresh));
        CHECK(!s.alive(old));                        // **옛 핸들은 죽은 채로 남는다**
        CHECK_EQ(s.denseOf(old), -1);
        CHECK(!s.markDead(old));                     // 옛 핸들로 새 엔티티를 죽일 수 없다
        CHECK(s.alive(fresh));

        // 옛 핸들이 새 엔티티의 난수열을 물려받지 않는다.
        CHECK(s.rngState[0] != Rng::deriveEntity(3, old.bits).state());
        CHECK(s.rngState[0] == Rng::deriveEntity(3, fresh.bits).state());
    }

    dctest::section("전량 소멸 후 재사용");
    {
        EntityStore s;
        s.init();
        for (int32_t round = 0; round < 3; ++round) {
            for (int32_t k = 0; k < 64; ++k) (void)s.spawn(descFor(k), round, 11);
            CHECK_EQ(s.count(), 64u);
            for (uint32_t i = 0; i < 64; ++i) s.markDead(s.idAt(i));
            CHECK_EQ(s.compact(), 64u);
            CHECK_EQ(s.count(), 0u);
            CHECK_EQ(s.freeSlots(), EntityStore::CAPACITY);   // 슬롯 누수 없음
        }
    }

    dctest::section("결정론 — 같은 각본이면 같은 레이아웃");
    {
        // 두 저장소를 같은 각본으로 돌린다. 스폰·삭제가 섞여도
        // 최종 배열이 바이트 단위로 같아야 한다.
        auto script = [](EntityStore& s) {
            s.init();
            Rng r = Rng::derive(555, RngStream::Spawn);
            for (int32_t t = 0; t < 400; ++t) {
                const uint32_t n = r.range(4);
                for (uint32_t i = 0; i < n; ++i) (void)s.spawn(descFor(t), t, 555);
                if (s.count() > 0) {
                    const uint32_t kills = r.range(3);
                    for (uint32_t i = 0; i < kills && s.count() > 0; ++i) {
                        s.markDead(s.idAt(r.range(s.count())));
                    }
                }
                s.compact();
            }
        };
        EntityStore a, b;
        script(a);
        script(b);
        CHECK(a.count() > 0);

        bool same = (a.count() == b.count());
        for (uint32_t i = 0; i < a.count() && same; ++i) {
            if (!(a.idAt(i) == b.idAt(i))) same = false;
            if (a.posX[i].raw != b.posX[i].raw) same = false;
            if (a.rngState[i] != b.rngState[i])  same = false;
            if (a.spawnTick[i] != b.spawnTick[i]) same = false;
        }
        CHECK(same);
        printf("    400틱 후 생존 %u기, 슬롯 잔여 %u\n", a.count(), a.freeSlots());
    }

    return dctest::summary("test_entity_store");
}
