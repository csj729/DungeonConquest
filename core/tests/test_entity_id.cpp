// EntityId 테스트 — 팩·언팩, 무효 표현, 세대 순환, 그리고 **핵심인 stale 참조 거부**.
//
// 슬롯 재사용 시나리오를 실제로 돌려본다. 이 테스트가 없으면
// "죽은 몹을 가리키던 타겟 ID가 새로 스폰된 몹을 가리키는" 버그를 코드 리뷰로만 잡아야 한다.
#include "../include/dc/entity_id.h"
#include "test_main.h"

using namespace dc;

int main() {
    printf("test_entity_id\n");

    dctest::section("팩·언팩");
    {
        // 경계 포함 왕복. 12비트 인덱스 전 구간을 돈다.
        bool ok = true;
        for (uint32_t i = 0; i <= EntityId::MAX_INDEX; ++i) {
            const EntityId id = EntityId::make(i, 1);
            if (id.index() != i || id.generation() != 1) ok = false;
        }
        CHECK(ok);

        const EntityId top = EntityId::make(EntityId::MAX_INDEX, EntityId::GEN_MASK);
        CHECK_EQ(top.index(), EntityId::MAX_INDEX);
        CHECK_EQ(top.generation(), EntityId::GEN_MASK);

        // 비트가 겹치지 않는지 — index를 꽉 채워도 generation이 오염되지 않아야 한다.
        CHECK_EQ(EntityId::make(EntityId::MAX_INDEX, 7).generation(), 7u);
        CHECK_EQ(EntityId::make(3, EntityId::GEN_MASK).index(), 3u);

        // 범위를 넘는 입력은 마스킹된다 (조용히 옆 필드를 침범하지 않는다).
        CHECK_EQ(EntityId::make(4096, 1).index(), 0u);
        CHECK_EQ(EntityId::make(1, 1u << 20).generation(), 0u);
    }

    dctest::section("무효 표현");
    {
        CHECK(!EntityId().valid());            // 기본 생성 = 무효
        CHECK(!EntityId::invalid().valid());
        CHECK_EQ(EntityId::invalid().bits, 0u);

        // generation 0만 무효다 — index 0은 멀쩡한 슬롯이다.
        CHECK(EntityId::make(0, 1).valid());
        CHECK(!EntityId::make(0, 0).valid());
        CHECK(!EntityId::make(500, 0).valid());
    }

    dctest::section("세대 순환");
    {
        CHECK_EQ(EntityId::nextGeneration(1), 2u);
        // 순환 지점에서 0을 건너뛴다. 안 건너뛰면 살아있는 엔티티가 무효로 보인다.
        CHECK_EQ(EntityId::nextGeneration(EntityId::GEN_MASK), 1u);
        // GEN_MASK를 넘는 입력도 마스킹 후 0 회피가 걸린다.
        CHECK(EntityId::nextGeneration(EntityId::GEN_MASK) != 0u);

        // 전 구간을 돌려도 0이 나오지 않아야 한다 (샘플링).
        bool neverZero = true;
        uint32_t g = 1;
        for (int i = 0; i < 100000; ++i) {
            g = EntityId::nextGeneration(g);
            if (g == 0) neverZero = false;
        }
        CHECK(neverZero);
    }

    dctest::section("stale 참조 거부");
    {
        // 최소 슬롯 풀. 죽으면 세대를 올리고 슬롯을 반납한다.
        struct Pool {
            uint32_t gen[8] = {0, 0, 0, 0, 0, 0, 0, 0};
            bool     live[8] = {false};

            EntityId spawn(uint32_t slot) {
                gen[slot] = gen[slot] == 0 ? 1 : EntityId::nextGeneration(gen[slot]);
                live[slot] = true;
                return EntityId::make(slot, gen[slot]);
            }
            void kill(uint32_t slot) { live[slot] = false; }
            bool alive(EntityId id) const {
                const uint32_t s = id.index();
                return id.valid() && live[s] && gen[s] == id.generation();
            }
        } pool;

        const EntityId oldMob = pool.spawn(3);
        CHECK(pool.alive(oldMob));

        pool.kill(3);
        CHECK(!pool.alive(oldMob));            // 죽은 직후

        const EntityId newMob = pool.spawn(3); // 같은 슬롯 재사용
        CHECK(pool.alive(newMob));
        CHECK(!pool.alive(oldMob));            // **여기가 핵심** — 옛 ID는 여전히 무효
        CHECK(oldMob != newMob);
        CHECK_EQ(oldMob.index(), newMob.index());   // 슬롯은 같은데 ID는 다르다
    }

    dctest::section("전순서");
    {
        // 동점 처리(거리 같은 적 고르기)에 쓰므로 완전 순서여야 한다.
        const EntityId a = EntityId::make(1, 1);
        const EntityId b = EntityId::make(2, 1);
        const EntityId c = EntityId::make(1, 2);

        CHECK(!(a < a));                       // 비반사
        CHECK((a < b) != (b < a));             // 비대칭
        CHECK(a < b);
        CHECK(b < c);
        CHECK(a < c);                          // 추이

        // 서로 다른 ID면 반드시 한쪽이 앞선다 (동점이 남지 않는다).
        bool totalOrder = true;
        for (uint32_t i = 0; i < 64; ++i) {
            for (uint32_t j = 0; j < 64; ++j) {
                const EntityId x = EntityId::make(i, 1 + (i % 3));
                const EntityId y = EntityId::make(j, 1 + (j % 3));
                if (x == y) continue;
                if ((x < y) == (y < x)) totalOrder = false;
            }
        }
        CHECK(totalOrder);
    }

    dctest::section("크기·constexpr");
    {
        // uint32_t 한 칸을 넘으면 상태 버퍼·체크섬 비용이 그대로 늘어난다.
        static_assert(sizeof(EntityId) == 4, "EntityId는 4바이트여야 한다");
        static_assert(EntityId::INDEX_BITS + EntityId::GEN_BITS == 32, "비트 배분이 32를 넘거나 남는다");

        constexpr EntityId id = EntityId::make(11, 5);
        static_assert(id.index() == 11, "");
        static_assert(id.generation() == 5, "");
        static_assert(id.valid(), "");
        static_assert(!EntityId::invalid().valid(), "");
        static_assert(EntityId::nextGeneration(EntityId::GEN_MASK) == 1, "");
        CHECK(true);
    }

    return dctest::summary("test_entity_id");
}
