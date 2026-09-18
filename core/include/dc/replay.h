// 재현성 하네스 — "같은 시드 N회 실행 → 매 틱 checksum() 일치" (CLAUDE.md).
//
// 실패했을 때 무엇을 보고해야 하는지가 이 파일의 전부다:
//   1. **최초로 갈라진 틱 번호** (CLAUDE.md 명시)
//   2. **갈라진 영역** — 부분 체크섬 덕에 시스템까지 좁혀진다
//   3. 몇 번째 실행에서
//
// 틱 진행을 `step` 호출자로 받는 이유: 시스템이 아직 없어서가 아니라,
// **부분 실행도 검증 대상**이기 때문이다. 나중에 "Spawn만 돌린 틱"이나
// "입력 로그를 먹인 틱"도 같은 하네스로 검증한다.
#ifndef DC_REPLAY_H
#define DC_REPLAY_H

#include <cstdint>

#include "checksum.h"
#include "world.h"

namespace dc {

struct DivergenceReport {
    bool        ok        = true;
    int32_t     tick      = -1;        // 최초로 갈라진 틱. ok면 -1
    int32_t     run       = -1;        // 몇 번째 재실행에서 (0이 기준 실행)
    const char* domain    = nullptr;   // "entities" · "hero" · ...
    Checksums   expected{};
    Checksums   actual{};
    uint64_t    finalChecksum = 0;     // 기준 실행의 마지막 틱 체크섬
    int32_t     ticksRun  = 0;
};

// scratch는 최소 (ticks + 1)칸이어야 한다 — 틱 0(초기 상태)부터 기록한다.
// **코어는 동적 할당을 하지 않으므로** 버퍼는 호출자가 준다.
template <typename Step>
DivergenceReport verifyReproducible(uint64_t seed, int32_t ticks, int32_t runs,
                                    Checksums* scratch, int32_t scratchLen, Step step) {
    DivergenceReport rep;
    if (scratch == nullptr || scratchLen < ticks + 1 || ticks < 0 || runs < 1) {
        rep.ok = false;
        rep.domain = "인자 오류";
        return rep;
    }

    for (int32_t run = 0; run < runs; ++run) {
        World w;
        w.init(seed);

        for (int32_t t = 0; t <= ticks; ++t) {
            if (t > 0) step(w);
            const Checksums c = w.checksums();

            if (run == 0) {
                scratch[t] = c;
                continue;
            }
            if (c != scratch[t]) {
                rep.ok       = false;
                rep.tick     = t;
                rep.run      = run;
                rep.domain   = firstDivergentDomain(scratch[t], c);
                rep.expected = scratch[t];
                rep.actual   = c;
                rep.ticksRun = t;
                return rep;   // 최초 분기에서 멈춘다. 그 뒤는 전부 파생 증상이다
            }
        }
    }

    rep.finalChecksum = scratch[ticks].total;
    rep.ticksRun      = ticks;
    return rep;
}

}  // namespace dc

#endif  // DC_REPLAY_H
