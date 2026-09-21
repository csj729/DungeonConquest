// 균등 그리드 (§11) — **적 간 충돌·회피에만 쓴다.**
//
// 영웅 → 적 타겟 탐색(1×N)과 적 → 영웅 거리 판정(N×1)은 브루트포스로 충분하다.
// N×N인 적 간 충돌 하나만 공간 분할이 필요하다.
//
// ## 해시 그리드를 쓰지 않는다
//
// `unordered_map` 순회는 구현체마다 순서가 달라 금지이고(CLAUDE.md) 캐시도 나쁘다.
// 대신 **밀집 배열 + counting sort로 매 틱 재구축**한다 — 동적 할당 0회, O(N),
// 완전 결정론. 유닛이 매 틱 움직이므로 트리를 유지하는 것보다 다시 만드는 게 싸다.
//
// ## [파생]이다
//
// 매 틱 엔티티 위치에서 다시 만들어지므로 체크섬에 넣지 않는다.
// 그래서 `World`가 아니라 `SimScratch`에 산다.
#ifndef DC_GRID_H
#define DC_GRID_H

#include <cstdint>

#include "config.h"
#include "entity_store.h"
#include "fixed.h"

namespace dc {

class UniformGrid {
public:
    static constexpr uint32_t DIM   = 64;          // 64×64 셀
    static constexpr uint32_t CELLS = DIM * DIM;

    // 셀 크기는 최소 이격 거리로 잡는다 — 그러면 이웃 탐색이 3×3 셀로 닫힌다.
    void build(const EntityStore& e, Fixed cellSize, Fixed centerX, Fixed centerY) {
        cellSize_ = cellSize.raw > 0 ? cellSize : Fixed(1);
        // 원점을 중심에서 절반만큼 뒤로 물린다 (영웅이 격자 한가운데 오도록)
        const int64_t half = (static_cast<int64_t>(cellSize_.raw) * DIM) / 2;
        originX_ = Fixed::fromRaw(static_cast<int32_t>(centerX.raw - half));
        originY_ = Fixed::fromRaw(static_cast<int32_t>(centerY.raw - half));
        count_   = e.count();

        for (uint32_t c = 0; c <= CELLS; ++c) cellStart_[c] = 0;

        // 1패스: 셀별 개수
        for (uint32_t i = 0; i < count_; ++i) {
            const uint32_t c = cellOfEntity(e, i);
            cellOf_[i] = static_cast<uint16_t>(c);
            ++cellStart_[c + 1];
        }
        // 2패스: 누적합
        for (uint32_t c = 0; c < CELLS; ++c) cellStart_[c + 1] += cellStart_[c];
        // 3패스: 배치. **엔티티를 dense 오름차순으로 훑으므로 셀 안의 순서도
        // dense 오름차순이다** — 순회 순서가 배열 순서로 고정된다.
        for (uint32_t c = 0; c <= CELLS; ++c) fill_[c] = cellStart_[c];
        for (uint32_t i = 0; i < count_; ++i) {
            items_[fill_[cellOf_[i]]++] = static_cast<uint16_t>(i);
        }
    }

    uint32_t cellX(uint32_t cell) const { return cell % DIM; }
    uint32_t cellY(uint32_t cell) const { return cell / DIM; }
    uint32_t cellOf(uint32_t denseIndex) const { return cellOf_[denseIndex]; }

    uint32_t begin(uint32_t cell) const { return cellStart_[cell]; }
    uint32_t end(uint32_t cell) const   { return cellStart_[cell + 1]; }
    uint32_t item(uint32_t slot) const  { return items_[slot]; }

private:
    uint32_t cellOfEntity(const EntityStore& e, uint32_t i) const {
        return cellIndex(e.posX[i], e.posY[i]);
    }
    uint32_t cellIndex(Fixed x, Fixed y) const {
        // 격자 밖은 가장자리 셀로 물린다. 멀리 있는 몹이 한 셀에 몰려도
        // 아래 거리 검사가 걸러내므로 결과는 같고, 비용만 조금 든다.
        int64_t gx = (static_cast<int64_t>(x.raw) - originX_.raw) / cellSize_.raw;
        int64_t gy = (static_cast<int64_t>(y.raw) - originY_.raw) / cellSize_.raw;
        if (gx < 0) gx = 0;
        if (gx >= DIM) gx = DIM - 1;
        if (gy < 0) gy = 0;
        if (gy >= DIM) gy = DIM - 1;
        return static_cast<uint32_t>(gy) * DIM + static_cast<uint32_t>(gx);
    }

    uint32_t cellStart_[CELLS + 1]{};
    uint32_t fill_[CELLS + 1]{};
    uint16_t items_[config::MAX_ENTITIES]{};
    uint16_t cellOf_[config::MAX_ENTITIES]{};
    Fixed    cellSize_ = Fixed(1);
    Fixed    originX_{};
    Fixed    originY_{};
    uint32_t count_ = 0;
};

// 시뮬 한 틱이 쓰는 [파생] 작업 공간. **World에 넣지 않는다** —
// 매 틱 다시 만들어지는 값이라 체크섬·스냅샷에 들어갈 이유가 없고,
// 넣으면 100KB짜리 World가 140KB가 된다.
struct SimScratch {
    UniformGrid grid;
};

}  // namespace dc

#endif  // DC_GRID_H
