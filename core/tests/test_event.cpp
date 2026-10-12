// 출력 이벤트 테스트 — **연출이 "순간"을 받는가.**
//
// 이 파일이 지키는 것 셋:
//
//   1. **이벤트가 시뮬에 새지 않는다** — 큐를 채우고 비워도 체크섬이 같다.
//      그게 "출력이다"라는 주장을 검사로 만든 것이고, 깨지면 결정론이 깨진다
//   2. **죽은 자리가 이벤트에 남는다** — 엔티티 행은 틱 끝에 압축되어 사라지므로
//      상태 버퍼로는 어디서 죽었는지 알 수 없다. 이게 큐가 존재하는 주된 이유다
//   3. **넘치면 센다** — 조용히 잃는 쪽이 이 프로젝트에서 반복된 고장 방식이다
#include "../include/dc/sim.h"
#include "../tools/dev_script.h"
#include "test_main.h"

using namespace dc;

namespace {

World makeWorld(uint64_t seed) {
    World w;
    initWorld(w, seed, dev::data().cfg, dev::data().table, dev::data().hero);
    return w;
}

// hp는 Fixed(20.12) 범위 안이어야 한다 (±524,287) — test_legend와 같은 이유다.
SpawnDesc mob(Fixed x, Fixed y, int32_t hp) {
    SpawnDesc d;
    d.posX = x; d.posY = y;
    d.maxHp = Fixed(hp);
    d.archetype = Archetype::Trash;
    d.attackRange = Fixed(1);
    return d;
}

uint32_t countKind(const World& w, EventKind k) {
    uint32_t n = 0;
    for (uint32_t i = 0; i < w.events.count; ++i) {
        if (w.events.events[i].kind == static_cast<int32_t>(k)) ++n;
    }
    return n;
}

const SimEvent* firstOf(const World& w, EventKind k) {
    for (uint32_t i = 0; i < w.events.count; ++i) {
        if (w.events.events[i].kind == static_cast<int32_t>(k)) return &w.events.events[i];
    }
    return nullptr;
}

}  // namespace

int main() {
    printf("test_event\n");

    dctest::section("이벤트는 **시뮬에 새지 않는다** (체크섬 불변)");
    {
        // 같은 시드로 두 번 돌리고, 한쪽은 중간에 큐를 비운다.
        // 드레인 타이밍이 결과를 바꾸면 결정론이 깨진 것이다.
        uint64_t sums[2] = {0, 0};
        for (int32_t k = 0; k < 2; ++k) {
            World w = makeWorld(20250918ull);
            for (int32_t t = 0; t < 600; ++t) {
                dev::runScript(w, 1);
                if (k == 1 && (t % 7) == 0) w.events.clear();   // 드레인 흉내
            }
            sums[k] = w.checksum();
        }
        CHECK_EQU(sums[0], sums[1]);
        printf("    드레인해도 체크섬 동일 %016llx\n",
               static_cast<unsigned long long>(sums[0] & 0xFFFFFFFFFFFFFFFFull));
    }

    dctest::section("처치 이벤트가 **죽은 자리**를 들고 있다");
    {
        // 엔티티 행은 틱 끝에 압축되므로, 이벤트가 위치를 안 들고 있으면
        // 다음 프레임에 그 정보가 **어디에도 없다.**
        World w = makeWorld(7ull);
        const SimConfig& cfg = dev::data().cfg;
        w.hero.posX = Fixed{}; w.hero.posY = Fixed{};
        const EntityId id = w.entities.spawn(
            mob(Fixed(2), Fixed(3), 1), 0, 7);
        const uint32_t d = static_cast<uint32_t>(w.entities.denseOf(id));
        w.events.clear();
        applySkillHit(w, cfg, d, Fixed(1000));          // 한 대에 죽는다

        const SimEvent* kill = firstOf(w, EventKind::Kill);
        CHECK(kill != nullptr);
        if (kill != nullptr) {
            CHECK_EQ(kill->entity, id.bits);
            CHECK_EQ(kill->posX, Fixed(2).raw);
            CHECK_EQ(kill->posY, Fixed(3).raw);
        }
        // 틱을 끝내면 행이 사라진다 — 그래도 이벤트는 그대로다
        w.endTick();
        CHECK(w.entities.denseOf(id) < 0);
        CHECK(firstOf(w, EventKind::Kill) != nullptr);
        printf("    행은 사라지고 이벤트는 남는다 (압축 후 dense %d)\n",
               w.entities.denseOf(id));
    }

    dctest::section("타격 이벤트가 **같은 틱 두 번째 타격도** 센다");
    {
        // 상태 디프로는 두 번이 한 번으로 보인다 — 큐의 두 번째 이유다.
        World w = makeWorld(8ull);
        const SimConfig& cfg = dev::data().cfg;
        const EntityId id = w.entities.spawn(mob(Fixed(2), Fixed{}, 100000), 0, 8);
        const uint32_t d = static_cast<uint32_t>(w.entities.denseOf(id));
        w.events.clear();
        applySkillHit(w, cfg, d, Fixed(1));
        applySkillHit(w, cfg, d, Fixed(1));
        CHECK_EQ(countKind(w, EventKind::Hit), 2u);
    }

    dctest::section("전달 피해를 싣는다 — **요청량이 아니다**");
    {
        // 남은 체력보다 큰 피해를 넣으면 전달량으로 깎여 와야 한다.
        // 연출이 요청량을 보여 주면 플레이어가 화력을 과대평가한다.
        World w = makeWorld(9ull);
        const SimConfig& cfg = dev::data().cfg;
        const EntityId id = w.entities.spawn(mob(Fixed(2), Fixed{}, 5), 0, 9);
        const uint32_t d = static_cast<uint32_t>(w.entities.denseOf(id));
        w.events.clear();
        applySkillHit(w, cfg, d, Fixed(1000));
        const SimEvent* hit = firstOf(w, EventKind::Hit);
        CHECK(hit != nullptr);
        if (hit != nullptr) {
            CHECK_EQ(hit->value, Fixed(5).raw);     // 남은 체력까지만
            CHECK(hit->value < Fixed(1000).raw);
        }
    }

    dctest::section("넘치면 **버리고 센다**");
    {
        World w;
        w.events.clear();
        for (uint32_t i = 0; i < EventQueue::MAX_EVENTS + 10; ++i) {
            w.events.push(EventKind::Hit, 0, Fixed{}, Fixed{}, 1);
        }
        CHECK_EQ(w.events.count, EventQueue::MAX_EVENTS);
        CHECK_EQ(w.events.dropped, 10u);
        printf("    상한 %u · 버린 수 %u\n", w.events.count, w.events.dropped);
    }

    dctest::section("런을 돌리면 **종류가 골고루 나온다** (연출이 받을 것이 있다)");
    {
        World w = makeWorld(20250918ull);
        uint32_t seen[16] = {0};
        for (int32_t t = 0; t < 4000; ++t) {
            dev::runScript(w, 1);
            for (uint32_t i = 0; i < w.events.count; ++i) {
                const int32_t k = w.events.events[i].kind;
                if (k > 0 && k < 16) ++seen[k];
            }
            w.events.clear();            // 프레임마다 드레인
        }
        // 이 넷이 없으면 런이 돌지 않은 것이다
        CHECK(seen[static_cast<int32_t>(EventKind::Hit)] > 0);
        CHECK(seen[static_cast<int32_t>(EventKind::Kill)] > 0);
        CHECK(seen[static_cast<int32_t>(EventKind::LevelUp)] > 0);
        CHECK(seen[static_cast<int32_t>(EventKind::CardOffer)] > 0);
        printf("    타격 %u · 처치 %u · 레벨업 %u · 카드 %u · 구슬드랍 %u · 기절 %u\n",
               seen[1], seen[2], seen[5], seen[6], seen[3], seen[9]);
    }

    return dctest::summary("test_event");
}
