// 물량 부하 — **정상성 검사다. 성능 측정이 아니다** (§14-12 · §11).
//
// 성능 숫자는 `core/tools/dc_load.cpp`가 Release에서 잰다. 여기는 그 반대쪽,
// **512 / 1024마리에서 코어가 조용히 틀리지 않는지**를 본다. 그게 이 파일이
// 테스트인 이유다 — `dc_load`는 도구라 ASan·UBSan 빌드에서 돌지 않는데,
// 물량이 깨뜨릴 종류의 사고는 대부분 느려지는 게 아니라 **UB**다:
//
//   · 고정폭 배열 넘침       `items_` · `cellOf_` · `collectInRadius`의 출력 버퍼
//   · Fixed 20.12 오버플로   `operator*(Fixed, int32_t)`가 int32 곱이다 —
//                            잠식 물량 충전이 `perMob × (alive - threshold)`를 곱한다
//   · uint16 절삭            셀 인덱스(4096칸)를 uint16에 담는다
//
// 이 셋은 전부 **일반 구간 30~60마리에서는 절대 밟지 않는다.** 그래서 나머지
// 테스트가 전부 초록인 채로 남아 있을 수 있고, 그게 여기를 따로 만든 이유다.
//
// 정상 진행으로는 512에 닿을 수 없다 (`capBySegment`가 30 → 37이다). 그래서
// **실제 스폰 경로(`spawnTrashBatch`)로 직접 채운다** — 상한만 우회하고 스폰
// 자체는 출시 코드다. 직접 `entities.spawn`을 부르면 방향 분배·엔티티별 난수
// 파생을 건너뛰어 "테스트만 통과하는 월드"가 된다.
#include <cstdio>
#include <cstdlib>

#include "../include/dc/sim.h"
#include "../tools/data_files.h"
#include "test_main.h"

using namespace dc;

// §11 물량 규모. **부하 목표 512 · 고정 할당 1024**가 설계값이고, 여기서 두
// 경계를 모두 밟는다. 상한은 "넘치지 않는다"를, 목표는 "실제로 돈다"를 본다.
constexpr uint32_t kTarget = 512;

static World makeFilled(uint64_t seed, uint32_t want, const SimConfig& cfg, int32_t* outMade) {
    World w;
    initWorld(w, seed, cfg, dev::data().table, dev::data().hero);
    *outMade = spawnTrashBatch(w, cfg, static_cast<int32_t>(want), cfg.trash.hp);
    return w;
}

// 물량을 유지하는 설정. **체력을 올리는 것이 핵심이다** — 정상성을 N의 함수로
// 보려는 것이므로 N이 측정 중에 줄어들면 무엇을 본 것인지 알 수 없다.
// 상한도 같이 올린다 (안 올리면 초과분 가속 ×3이 얹힌다).
//
// `survivable`은 **잠식 물량 충전을 끈다.** 끄지 않으면 1024마리에서 영웅이
// 42틱에 죽는다 — 버그가 아니라 §2가 설계한 "몬스터 수 상한 = 게임오버"가
// 그대로 작동하는 모습이고, 아래 첫 절이 그걸 따로 못 박는다. 다만 UB를
// 사냥하려면 월드가 살아서 틱을 돌아야 하므로 그 절에서만 끈다.
static SimConfig loadConfig(uint32_t n, bool survivable) {
    SimConfig c = dev::data().cfg;
    c.trash.hp = Fixed(100000);        // Fixed(1000000)은 20.12에서 오버플로다
    if (survivable) c.corruptionPerMobPermille = 0;
    for (uint32_t i = 0; i < 32; ++i) c.capBySegment[i] = static_cast<int32_t>(n);
    return c;
}

int main() {
    printf("=== 물량 부하 정상성 (§14-12 목표 %u · 상한 %u) ===\n",
           kTarget, config::MAX_ENTITIES);

    dctest::section("고정 할당 상한에서 스폰이 **거부**된다 (조용히 덮어쓰지 않는다)");
    {
        const SimConfig c = loadConfig(config::MAX_ENTITIES, true);
        int32_t made = 0;
        World w = makeFilled(1, config::MAX_ENTITIES, c, &made);
        CHECK_EQ(made, static_cast<int32_t>(config::MAX_ENTITIES));
        CHECK_EQ(w.entities.count(), config::MAX_ENTITIES);

        // 상한을 넘겨 더 넣으려 하면 **0마리만 들어간다.** 여기가 부서지면
        // 배열 밖 쓰기이고, ASan 빌드에서만 보이는 종류의 사고다.
        const uint64_t before = w.checksum();
        const int32_t extra = spawnTrashBatch(w, c, 64, c.trash.hp);
        CHECK_EQ(extra, 0);
        CHECK_EQ(w.entities.count(), config::MAX_ENTITIES);
        // **거부는 상태를 바꾸지 않는다.** directionCursor 하나라도 움직이면
        // 리플레이가 "거부된 스폰"에서 갈라진다.
        CHECK_EQU(w.checksum(), before);
        printf("    %u마리 적재 후 추가 64 요청 → %d마리 · 체크섬 불변\n",
               config::MAX_ENTITIES, extra);
    }

    dctest::section("균등 그리드가 **모든 엔티티를 정확히 한 칸에** 담는다");
    {
        const SimConfig c = loadConfig(config::MAX_ENTITIES, true);
        int32_t made = 0;
        World w = makeFilled(2, config::MAX_ENTITIES, c, &made);

        static SimScratch sc;
        separationRun(w, c, sc);      // 그리드를 세우는 유일한 호출자

        // 셀 점유 합 = 엔티티 수. 한 칸이라도 새면 분리가 그 쌍을 못 본다 —
        // 증상은 크래시가 아니라 "가끔 겹친다"라서 눈으로는 못 잡는다.
        uint32_t sum = 0;
        uint32_t maxCell = 0;
        for (uint32_t cell = 0; cell < UniformGrid::CELLS; ++cell) {
            const uint32_t n = sc.grid.end(cell) - sc.grid.begin(cell);
            sum += n;
            if (n > maxCell) maxCell = n;
        }
        CHECK_EQ(sum, w.entities.count());

        // 각 엔티티가 자기 셀에 들어 있다 (cellOf와 items_가 일관적이다)
        static uint32_t seen[config::MAX_ENTITIES];
        for (uint32_t i = 0; i < config::MAX_ENTITIES; ++i) seen[i] = 0;
        for (uint32_t cell = 0; cell < UniformGrid::CELLS; ++cell) {
            uint32_t prev = 0;
            bool first = true;
            for (uint32_t s = sc.grid.begin(cell); s < sc.grid.end(cell); ++s) {
                const uint32_t i = sc.grid.item(s);
                CHECK(i < w.entities.count());
                CHECK_EQ(sc.grid.cellOf(i), cell);
                ++seen[i];
                // **셀 안이 dense 오름차순이다** — grid.h가 약속한 성질이고,
                // 이게 깨지면 분리의 `j <= i` 쌍 걸러내기가 쌍을 빠뜨린다.
                if (!first) CHECK(i > prev);
                prev = i;
                first = false;
            }
        }
        for (uint32_t i = 0; i < w.entities.count(); ++i) CHECK_EQ(seen[i], 1u);
        printf("    %u마리 → 점유 합 %u · 최대 셀 %u마리\n",
               w.entities.count(), sum, maxCell);
    }

    dctest::section("**512마리는 플레이 가능한 물량이 아니다** — 잠식이 먼저 끝낸다");
    {
        // 부하 목표 512는 **성능 목표**이고, 게임 규칙상 도달하면 죽는다.
        // 유입이 `0.6 × (N - 20)`/초라 N=512면 초당 295, 게이지 1250이 4.2초에
        // 찬다 — §2가 "몬스터 수 상한 = 게임오버"를 게이지 하나로 흡수한 장치가
        // 그대로 작동하는 것이다.
        //
        // **이 절이 없으면 그 장치가 고장나도 아무도 모른다.** 물량 상한을
        // 따로 검사하는 코드가 없으므로, 잠식이 물량에 반응하지 않게 되면
        // 512마리가 조용히 "그냥 좀 많은 상태"가 된다.
        for (uint32_t n : {kTarget, config::MAX_ENTITIES}) {
            const SimConfig c = loadConfig(n, false);     // 잠식 그대로
            int32_t made = 0;
            World w = makeFilled(3, n, c, &made);
            w.hero.corruption = Fixed{};
            static SimScratch sc;
            int32_t died = -1;
            for (int32_t t = 0; t < 400 && died < 0; ++t) {
                stepWorld(w, c, dev::data().table, sc);
                if (w.hero.dead()) died = t + 1;
            }
            CHECK(died > 0);                       // 반드시 죽는다
            CHECK(died < 400);
            printf("    %4u마리 → %d틱(%.1f초)에 잠식 가득\n", n, died,
                   static_cast<double>(died) / config::TICK_HZ);
        }
    }

    dctest::section("**1024마리를 실제로 돌린다** — UB가 있으면 여기서 터진다");
    {
        // ASan/UBSan 빌드의 본체다. 통과만으로는 아무것도 증명하지 않지만,
        // 이 절이 없으면 1024마리 경로를 **한 번도 실행하지 않는다.**
        // 잠식을 끄는 이유는 위 절에 있다 — 켜두면 42틱에 끝나 전투·분리·
        // 압축이 한 바퀴도 돌지 않는다.
        const SimConfig c = loadConfig(config::MAX_ENTITIES, true);
        int32_t made = 0;
        World w = makeFilled(4, config::MAX_ENTITIES, c, &made);

        static SimScratch sc;
        const int32_t kTicks = 200;      // 10초 — 분리가 자리를 잡고 전투가 한 바퀴 돈다
        for (int32_t t = 0; t < kTicks; ++t) stepWorld(w, c, dev::data().table, sc);

        CHECK(!w.hero.dead());
        CHECK(w.entities.count() > kTarget);
        printf("    %d틱 후 %u마리 생존 (잠식 충전 off)\n", kTicks, aliveCount(w.entities));
    }

    dctest::section("잠식 물량 충전이 1024마리에서 **유한하고 단조**다");
    {
        // `operator*(Fixed, int32_t)`가 int32 곱이라 여기가 오버플로 후보다.
        // 넘치면 UBSan이 잡고, UBSan 없는 빌드에서는 **부호가 뒤집혀 잠식이
        // 줄어든다** — 물량이 많을수록 유리해지는 조용한 역전이다.
        const SimConfig c = loadConfig(config::MAX_ENTITIES, false);
        int32_t prev = -1;
        for (uint32_t n : {32u, 64u, 150u, 512u, 1024u}) {
            int32_t made = 0;
            World w = makeFilled(5, n, c, &made);
            w.hero.corruption = Fixed{};
            static SimScratch sc;
            stepWorld(w, c, dev::data().table, sc);
            const int32_t got = w.hero.corruption.raw;
            CHECK(got > 0);                    // 임계(20) 위이므로 반드시 찬다
            CHECK(got > prev);                 // **많을수록 더 찬다**
            prev = got;
        }
        printf("    32 → 1024마리까지 잠식 유입이 단조 증가 (마지막 raw %d)\n", prev);
    }

    dctest::section("1024마리에서도 **결정론이 유지된다**");
    {
        // 물량이 늘면 분리가 쌍을 훑는 순서가 복잡해진다. 순서 의존이 끼어들면
        // 여기가 유일하게 그걸 잡는 자리다 (일반 구간 60마리는 쌍이 적어 묻힌다).
        const SimConfig c = loadConfig(config::MAX_ENTITIES, true);
        uint64_t first = 0;
        for (int32_t run = 0; run < 3; ++run) {
            int32_t made = 0;
            World w = makeFilled(6, config::MAX_ENTITIES, c, &made);
            static SimScratch sc;
            for (int32_t t = 0; t < 100; ++t) stepWorld(w, c, dev::data().table, sc);
            if (run == 0) first = w.checksum();
            else CHECK_EQU(w.checksum(), first);
        }
        printf("    같은 시드 3회 × 100틱 체크섬 일치 (%llu)\n",
               static_cast<unsigned long long>(first));
    }

    dctest::section("분리가 1024마리에서도 **영웅 주위 패킹을 지킨다**");
    {
        // 물량이 상한에 닿으면 밀어낼 공간이 모자랄 수 있다. 그때 몹이 **점에
        // 뭉친 채 멈추면** SURROUND_COUNT 가정이 무너져 밸런스 모델 전체가
        // 틀린다 (§11: 분리 없이 돌리면 동시 접촉 26마리 = 가정의 5배).
        //
        // **고정 상수와 비교하지 않는다.** 정확한 접촉 수는 패킹 기하가 정하고
        // dc_field가 재므로 여기 적으면 두 번째 진실 원천이 된다. 대신 분리를
        // 켠 월드와 끈 월드를 **같은 시드로 나란히 돌려 차이를 본다** — 그게
        // "분리가 일을 하고 있다"의 직접 증거다.
        int32_t contact[2] = {0, 0};
        for (int32_t k = 0; k < 2; ++k) {
            SimConfig c = loadConfig(config::MAX_ENTITIES, true);
            if (k == 1) { c.separationMilli = 0; c.heroSeparationMilli = 0; }
            int32_t made = 0;
            World w = makeFilled(7, config::MAX_ENTITIES, c, &made);
            static SimScratch sc;
            for (int32_t t = 0; t < 200; ++t) stepWorld(w, c, dev::data().table, sc);

            // 몹 **사거리** 안의 수 = 실제로 영웅을 때릴 수 있는 수.
            // 영웅 사거리(3.0)가 아니라 이쪽이 SURROUND_COUNT의 정의다.
            for (uint32_t i = 0; i < w.entities.count(); ++i) {
                if (w.entities.deadAt(i)) continue;
                const int64_t r = w.entities.attackRange[i].raw;
                if (distanceSq(w.entities.posX[i], w.entities.posY[i],
                               w.hero.posX, w.hero.posY) <= r * r) ++contact[k];
            }
        }
        // 분리를 끄면 전원이 한 점에 모이므로 접촉이 폭증한다. 켠 쪽이 **훨씬
        // 작아야** 하고, 그 차이가 분리가 살아 있다는 증거다.
        CHECK(contact[0] > 0);                       // 전투가 성립한다
        CHECK(contact[0] * 10 < contact[1]);         // 한 자릿수 배 이상 차이
        printf("    1024마리 접촉: 분리 켬 %d · 끔 %d (%.0f배)\n",
               contact[0], contact[1],
               contact[0] > 0 ? static_cast<double>(contact[1]) / contact[0] : 0.0);
    }

    return dctest::summary("test_load");
}
