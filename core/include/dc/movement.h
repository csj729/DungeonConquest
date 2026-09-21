// 이동 (§3) — **영웅은 추격 후 공격만 한다. 도주·선회는 없다.**
//
// ## 이동 규칙이 타겟 우선순위에 종속된다
//
// 회피 벡터나 선회 같은 별도 이동 정책을 두지 않는다. 영웅은 현재 타겟을 향해
// 걸어가 사거리에 들어오면 멈추고 때린다. 그래서 **손잡이가 하나로 합쳐진다** —
// 누구를 때릴지 정하면 어디로 갈지도 정해진다 (§3 타겟 우선순위).
//
// 그래도 위치는 1급 요소로 남는다. 멀리 있는 엘리트를 쫓아가면 잡몹 무리가
// 뒤로 처지므로, **밀집도와 포위 형태가 타겟 선택의 부산물로 변한다.**
// `E_SWARM`(주변 적 수 비례)이나 `R_FROST`(주변 둔화)가 의미를 갖는 경로가 이것이다.
//
// §3의 "방어 수단은 사거리 이탈뿐"도 이 구조 안에서 성립한다 — 의도적으로
// 도망치는 게 아니라, 먼 타겟을 쫓는 동안 다른 적의 사거리에서 벗어나는 것이다.
#ifndef DC_MOVEMENT_H
#define DC_MOVEMENT_H

#include <cstdint>

#include "entity_store.h"
#include "fixed.h"
#include "sim_config.h"

namespace dc {

// from → to 방향으로 step만큼 전진한다. **목표 거리(stopAt)를 지나치지 않는다** —
// 지나치면 사거리 경계에서 매 틱 앞뒤로 떨린다.
// 이동했으면 true, 이미 도달했으면 false.
inline bool stepToward(Fixed* x, Fixed* y, Fixed toX, Fixed toY,
                       Fixed step, Fixed stopAt, Fixed* outDirX, Fixed* outDirY) {
    const int64_t dx = static_cast<int64_t>(toX.raw) - x->raw;
    const int64_t dy = static_cast<int64_t>(toY.raw) - y->raw;
    const int64_t d  = static_cast<int64_t>(isqrt64(static_cast<uint64_t>(dx * dx + dy * dy)));
    if (d <= 0) return false;

    if (outDirX != nullptr) {
        *outDirX = Fixed::fromRaw(static_cast<int32_t>((dx * Fixed::ONE_RAW) / d));
        *outDirY = Fixed::fromRaw(static_cast<int32_t>((dy * Fixed::ONE_RAW) / d));
    }

    int64_t travel = step.raw;
    const int64_t room = d - stopAt.raw;     // 멈춰야 할 거리까지 남은 여유
    if (room <= 0) return false;             // 이미 사거리 안이다
    if (travel > room) travel = room;        // 지나치지 않는다

    x->raw = static_cast<int32_t>(x->raw + (dx * travel) / d);
    y->raw = static_cast<int32_t>(y->raw + (dy * travel) / d);
    return true;
}

}  // namespace dc

#endif  // DC_MOVEMENT_H
