// World 테스트 — 상태의 완결성.
//
// 이 파일이 확인하는 건 "시뮬이 잘 도는가"가 아니다. 아직 시스템이 없다.
// **World 안에 있는 것만으로 런을 재현할 수 있는가**를 본다.
// 상태가 하나라도 밖에 새어 있으면 checksum()은 같은데 결과는 달라진다.
#include <cstring>
#include <type_traits>

#include "../include/dc/world.h"
#include "../tools/dev_script.h"
#include "test_main.h"

using namespace dc;

// 상태 비교는 실제 checksum()으로 한다. 예전에는 이 자리에 임시 지문 함수가
// 있었는데, §14-3에서 checksum()이 붙으면서 필요가 없어졌다.
// 틱 드라이버도 공용 임시 드라이버(core/tools/dev_script.h)를 쓴다.
static uint64_t digest(const World& w) { return w.checksum(); }
static void runScript(World& w, int32_t ticks) { dev::runScript(w, ticks); }

int main() {
    printf("test_world\n");

    dctest::section("스냅샷 가능성");
    {
        // **memcpy로 통째로 저장·복원할 수 있어야 한다.** §10이 요구하는
        // "임의 틱의 상태 스냅샷 덤프/로드"가 이 성질 위에 선다.
        // 가상 함수나 포인터 멤버가 하나라도 들어오면 여기서 깨진다.
        static_assert(std::is_trivially_copyable<World>::value,
                      "World는 memcpy로 스냅샷 가능해야 한다");
        static_assert(std::is_trivially_copyable<EntityStore>::value, "");
        static_assert(std::is_trivially_copyable<HeroState>::value, "");
        static_assert(std::is_trivially_copyable<Fixed>::value, "");
        static_assert(std::is_trivially_copyable<Rng>::value, "");
        printf("    sizeof(World) = %zu 바이트 (엔티티 %u칸 포함)\n",
               sizeof(World), EntityStore::CAPACITY);
        CHECK(sizeof(World) < 256u * 1024u);   // 스냅샷을 자주 뜰 수 있는 크기인가
    }

    dctest::section("init 결정론");
    {
        World a, b;
        a.init(20250918);
        b.init(20250918);
        CHECK_EQU(digest(a), digest(b));

        World c;
        c.init(20250919);
        CHECK(digest(c) != digest(a));      // 시드가 결과를 가른다

        // 다섯 스트림이 서로 달라야 한다. 같으면 스트림 분리가 무의미하다.
        CHECK(a.rngSpawn.state() != a.rngCombat.state());
        CHECK(a.rngCombat.state() != a.rngCards.state());
        CHECK(a.rngCards.state() != a.rngItems.state());
        CHECK(a.rngItems.state() != a.rngEvents.state());
    }

    dctest::section("init은 완전 초기화");
    {
        // 더럽힌 World를 재init하면 새 World와 구별되지 않아야 한다.
        // 한 필드라도 빠뜨리면 **이전 런의 잔재가 다음 런에 샌다** —
        // 리플레이 검증에서 "서버만 틀리는" 증상으로 나타난다.
        World dirty;
        dirty.init(1);
        runScript(dirty, 200);
        CHECK(dirty.entities.count() > 0);
        dirty.init(20250918);

        World clean;
        clean.init(20250918);
        CHECK_EQU(digest(dirty), digest(clean));
        CHECK_EQ(dirty.entities.count(), 0u);
        CHECK_EQ(dirty.entities.freeSlots(), EntityStore::CAPACITY);
        CHECK_EQ(dirty.tickCount(), 0);
    }

    dctest::section("틱 전진");
    {
        World w;
        w.init(7);
        CHECK_EQ(w.tickCount(), 0);
        CHECK_EQ(w.elapsedSeconds(), 0);
        for (int i = 0; i < 20; ++i) w.tick();
        CHECK_EQ(w.tickCount(), 20);
        CHECK_EQ(w.elapsedSeconds(), 1);    // 20Hz 고정 — 실시간을 보지 않는다
        for (int i = 0; i < 19; ++i) w.tick();
        CHECK_EQ(w.elapsedSeconds(), 1);    // 절삭
    }

    dctest::section("틱 종료 일괄 압축");
    {
        World w;
        w.init(3);
        SpawnDesc d;
        const EntityId a = w.entities.spawn(d, 0, 3);
        const EntityId b = w.entities.spawn(d, 0, 3);
        w.entities.markDead(a);

        CHECK_EQ(w.entities.count(), 2u);   // 틱이 끝나기 전에는 안 사라진다
        w.tick();
        CHECK_EQ(w.entities.count(), 1u);   // tick()의 applyDeaths가 정리한다
        CHECK(w.entities.alive(b));
    }

    dctest::section("재현성 — 같은 시드 N회");
    {
        // CLAUDE.md의 재현성 테스트 형식 그대로. 시스템이 붙으면 이 루프에
        // checksum()이 들어간다.
        uint64_t first = 0;
        bool allSame = true;
        for (int run = 0; run < 5; ++run) {
            World w;
            w.init(0xDEADBEEF);
            runScript(w, 600);
            const uint64_t d = digest(w);
            if (run == 0) first = d;
            else if (d != first) allSame = false;
        }
        CHECK(allSame);
        printf("    600틱 × 5회 지문: %llu\n", static_cast<unsigned long long>(first));
    }

    dctest::section("스냅샷 복원");
    {
        // 중간 지점에서 뜬 스냅샷으로 되돌리면, 한 번도 안 끊긴 것과 같은
        // 궤적을 그려야 한다. 디버깅 시 diff의 기본 단위가 이것이다(§10).
        World w;
        w.init(1234);
        runScript(w, 300);

        alignas(World) static uint8_t snapshot[sizeof(World)];
        std::memcpy(snapshot, &w, sizeof(World));
        const uint64_t atSnapshot = digest(w);

        runScript(w, 300);
        const uint64_t straight = digest(w);
        CHECK(straight != atSnapshot);

        std::memcpy(&w, snapshot, sizeof(World));
        CHECK_EQU(digest(w), atSnapshot);       // 정확히 그 시점으로 돌아왔다
        CHECK_EQ(w.tickCount(), 300);

        runScript(w, 300);
        CHECK_EQU(digest(w), straight);         // 이어 돌리면 같은 궤적
    }

    dctest::section("잠식 게이지");
    {
        HeroState h;
        h.corruptionMax = Fixed(1250);
        h.corruption    = Fixed(0);
        CHECK(!h.dead());
        CHECK_EQ(h.corruptionLeft().raw, Fixed(1250).raw);

        h.corruption = Fixed(1000);
        CHECK_EQ(h.corruptionLeft().raw, Fixed(250).raw);
        CHECK(!h.dead());

        h.corruption = Fixed(1250);
        CHECK(h.dead());                        // 도달하면 게임오버
        CHECK_EQ(h.corruptionLeft().raw, 0);

        // 잔량은 음수로 내려가지 않는다.
        h.corruption = Fixed(2000);
        CHECK_EQ(h.corruptionLeft().raw, 0);
        CHECK(h.dead());

        // **최대치가 내려가도 누적값은 손실되지 않는다** — 체력 모델이었다면
        // damageTaken을 다시 계산해야 했을 자리다 (§9 돌발 이벤트 A).
        h.corruption    = Fixed(600);
        h.corruptionMax = Fixed(1250);
        CHECK(!h.dead());
        h.corruptionMax = Fixed(2000);          // 최대치 증가
        CHECK_EQ(h.corruption.raw, Fixed(600).raw);     // 누적값 그대로
        CHECK_EQ(h.corruptionLeft().raw, Fixed(1400).raw);
        h.corruptionMax = Fixed(500);           // 누적값 아래로 감소 → 즉사
        CHECK(h.dead());
        CHECK_EQ(h.corruption.raw, Fixed(600).raw);     // 여전히 그대로
    }

    dctest::section("전역 구간 인덱스");
    {
        RunState r;
        CHECK_EQ(r.globalSegment(8), 0);
        r.mapIndex = 0; r.segmentIndex = 7;
        CHECK_EQ(r.globalSegment(8), 7);
        r.mapIndex = 1; r.segmentIndex = 0;
        CHECK_EQ(r.globalSegment(8), 8);        // 맵 경계에서 이어진다
        r.mapIndex = 2; r.segmentIndex = 7;
        CHECK_EQ(r.globalSegment(8), 23);       // 전체 24구간의 마지막
    }

    return dctest::summary("test_world");
}
