// 헤드리스 체크섬 러너 — CI의 Debug/Release 일치 검증 진입점.
//
// CLAUDE.md: "Debug 빌드와 Release 빌드의 체크섬 일치를 CI에서 검증할 것."
// 이 바이너리를 두 빌드로 만들어 출력을 비교하면 그게 곧 그 검증이다.
//
// 나중에 서버 리플레이 검증(headless run → checksum)이 쓸 진입점의 원형이기도 하다.
#include <cstdio>
#include <cstdlib>

#include "../include/dc/replay.h"
#include "../include/dc/world.h"
#include "dev_script.h"

using namespace dc;

int main(int argc, char** argv) {
    const uint64_t seed  = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 0xDC0DE5EEDull;
    const int32_t  ticks = argc > 2 ? std::atoi(argv[2]) : 2400;
    const int32_t  every = argc > 3 ? std::atoi(argv[3]) : 400;

    World w;
    w.init(seed);

    printf("seed=%llu ticks=%d\n", static_cast<unsigned long long>(seed), ticks);
    printf("%8s %18s %18s %18s %18s %18s %18s\n",
           "tick", "total", "entities", "hero", "run", "spawn", "rng");

    for (int32_t t = 0; t <= ticks; ++t) {
        if (t > 0) dev::scriptTick(w);
        if (t % every == 0 || t == ticks) {
            const Checksums c = w.checksums();
            printf("%8d %18llu %18llu %18llu %18llu %18llu %18llu\n", t,
                   static_cast<unsigned long long>(c.total),
                   static_cast<unsigned long long>(c.entities),
                   static_cast<unsigned long long>(c.hero),
                   static_cast<unsigned long long>(c.run),
                   static_cast<unsigned long long>(c.spawn),
                   static_cast<unsigned long long>(c.rng));
        }
    }
    printf("생존 %u기 · 처치 잡몹 %d 엘리트 %d · 잠식 raw %d\n",
           w.entities.count(), w.run.killedTrash, w.run.killedElite, w.hero.corruption.raw);
    return 0;
}
