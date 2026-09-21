// 적 간 충돌·회피 (§11) — 균등 그리드를 쓰는 유일한 곳.
//
// **이게 없으면 몹 전부가 영웅 한 점에 겹친다.** 실측으로 확인한 결과다:
// 분리 없이 돌리면 동시 접촉이 26마리로, `balance_baseline.py`의 가정
// `SURROUND_COUNT = 5`의 5배가 나왔다. 그 가정은 "영웅 주위에 물리적으로 몇 마리가
// 붙을 수 있는가"라는 **패킹 한계**이고, 패킹은 분리가 있어야 생긴다.
//
// 영웅 주위 반지름 r에 간격 s로 늘어서면 첫 링에 들어가는 수는 약 2πr/s다.
// 그 수가 곧 피격량이고, 피격은 잠식 게이지의 74%다.
#ifndef DC_SEPARATION_H
#define DC_SEPARATION_H

#include <cstdint>

#include "grid.h"
#include "sim_config.h"
#include "world.h"

namespace dc {

// 겹친 두 엔티티를 밀어낸다. 각 쌍을 **정확히 한 번씩** 처리하고 양쪽에 절반씩
// 나눠 주므로 순서를 바꿔도 같은 결과가 나온다.
inline void separationRun(World& w, const SimConfig& cfg, SimScratch& scratch) {
    if (cfg.separationMilli <= 0) return;

    const int64_t minSep = (static_cast<int64_t>(cfg.separationMilli) * Fixed::ONE_RAW) / 1000;
    const int64_t heroSep = (static_cast<int64_t>(cfg.heroSeparationMilli) * Fixed::ONE_RAW) / 1000;

    EntityStore& e = w.entities;
    scratch.grid.build(e, Fixed::fromRaw(static_cast<int32_t>(minSep)),
                       w.hero.posX, w.hero.posY);

    const uint32_t n = e.count();
    for (uint32_t i = 0; i < n; ++i) {
        if (e.deadAt(i)) continue;
        const uint32_t cell = scratch.grid.cellOf(i);
        const int32_t  cx   = static_cast<int32_t>(scratch.grid.cellX(cell));
        const int32_t  cy   = static_cast<int32_t>(scratch.grid.cellY(cell));

        // 3×3 이웃 셀. 셀 크기가 최소 이격이므로 그 밖은 볼 필요가 없다.
        for (int32_t oy = -1; oy <= 1; ++oy) {
            const int32_t ny = cy + oy;
            if (ny < 0 || ny >= static_cast<int32_t>(UniformGrid::DIM)) continue;
            for (int32_t ox = -1; ox <= 1; ++ox) {
                const int32_t nx = cx + ox;
                if (nx < 0 || nx >= static_cast<int32_t>(UniformGrid::DIM)) continue;
                const uint32_t nc = static_cast<uint32_t>(ny) * UniformGrid::DIM
                                  + static_cast<uint32_t>(nx);
                for (uint32_t s = scratch.grid.begin(nc); s < scratch.grid.end(nc); ++s) {
                    const uint32_t j = scratch.grid.item(s);
                    if (j <= i) continue;          // 각 쌍을 한 번만
                    if (e.deadAt(j)) continue;

                    int64_t dx = static_cast<int64_t>(e.posX[i].raw) - e.posX[j].raw;
                    int64_t dy = static_cast<int64_t>(e.posY[i].raw) - e.posY[j].raw;
                    int64_t d  = static_cast<int64_t>(isqrt64(
                        static_cast<uint64_t>(dx * dx + dy * dy)));
                    if (d >= minSep) continue;

                    if (d == 0) {
                        // 정확히 겹쳤다. **EntityId 차이로 방향을 고정한다** —
                        // 무작위로 밀면 리플레이가 깨진다.
                        dx = ((e.idAt(i).bits ^ e.idAt(j).bits) & 1u) ? minSep : -minSep;
                        dy = ((e.idAt(i).bits ^ e.idAt(j).bits) & 2u) ? minSep : -minSep;
                        d  = static_cast<int64_t>(isqrt64(
                            static_cast<uint64_t>(dx * dx + dy * dy)));
                        if (d == 0) continue;
                    }

                    const int64_t push = (minSep - d) / 2;
                    const int32_t px = static_cast<int32_t>((dx * push) / d);
                    const int32_t py = static_cast<int32_t>((dy * push) / d);
                    e.posX[i] = Fixed::fromRaw(e.posX[i].raw + px);
                    e.posY[i] = Fixed::fromRaw(e.posY[i].raw + py);
                    e.posX[j] = Fixed::fromRaw(e.posX[j].raw - px);
                    e.posY[j] = Fixed::fromRaw(e.posY[j].raw - py);
                }
            }
        }
    }

    // 영웅과의 이격. **영웅은 밀리지 않는다** — 밀리면 이동 AI의 결정이 뒤집힌다.
    // 이 반지름이 곧 "몇 마리가 붙을 수 있는가"를 정한다 (첫 링 ≈ 2πr / 간격).
    if (heroSep <= 0) return;
    for (uint32_t i = 0; i < n; ++i) {
        if (e.deadAt(i)) continue;
        const int64_t dx = static_cast<int64_t>(e.posX[i].raw) - w.hero.posX.raw;
        const int64_t dy = static_cast<int64_t>(e.posY[i].raw) - w.hero.posY.raw;
        const int64_t d  = static_cast<int64_t>(isqrt64(
            static_cast<uint64_t>(dx * dx + dy * dy)));
        if (d >= heroSep || d == 0) continue;
        const int64_t push = heroSep - d;
        e.posX[i] = Fixed::fromRaw(e.posX[i].raw + static_cast<int32_t>((dx * push) / d));
        e.posY[i] = Fixed::fromRaw(e.posY[i].raw + static_cast<int32_t>((dy * push) / d));
    }
}

}  // namespace dc

#endif  // DC_SEPARATION_H
