// 고정 용량 인라인 배열.
//
// **동적 할당을 하지 않는다.** 할당자 동작은 플랫폼마다 다르고, 할당 실패 경로가
// 곧 분기 차이 = 서버-클라 불일치 경로다 (CLAUDE.md).
//
// 용량을 넘으면 **조용히 버리지 않고 false를 돌려준다.** 조용한 드롭은 결정론적이긴
// 해도 밸런스 버그가 되고, 증상이 "가끔 아이템 효과가 안 붙는다"라 잡기 어렵다.
#ifndef DC_SMALL_VEC_H
#define DC_SMALL_VEC_H

#include <cstdint>

namespace dc {

template <typename T, uint32_t N>
struct SmallVec {
    static constexpr uint32_t CAPACITY = N;

    T        items[N]{};
    uint32_t count = 0;

    constexpr uint32_t size() const  { return count; }
    constexpr bool     empty() const { return count == 0; }
    constexpr bool     full() const  { return count >= N; }
    constexpr void     clear()       { count = 0; }

    constexpr T&       operator[](uint32_t i)       { return items[i]; }
    constexpr const T& operator[](uint32_t i) const { return items[i]; }

    constexpr bool pushBack(const T& v) {
        if (count >= N) return false;
        items[count++] = v;
        return true;
    }

    // 오름차순 불변식을 삽입으로 유지한다. 원소가 소수(≤8)라 정렬 호출보다 싸고,
    // **정렬 함수의 안정성·비교자 규칙에 의존하지 않는다** — 결정론 표면이 그만큼 준다.
    constexpr bool insertSorted(const T& v) {
        if (count >= N) return false;
        uint32_t i = count;
        while (i > 0 && v < items[i - 1]) {
            items[i] = items[i - 1];
            --i;
        }
        items[i] = v;
        ++count;
        return true;
    }

    // 뒤를 앞으로 당긴다 — **순서를 보존한다.** swap-remove를 쓰면 오름차순
    // 불변식이 깨져서 PercentMult 적용 순서가 바뀐다.
    constexpr bool eraseAt(uint32_t i) {
        if (i >= count) return false;
        for (uint32_t k = i + 1; k < count; ++k) items[k - 1] = items[k];
        --count;
        return true;
    }
};

}  // namespace dc

#endif  // DC_SMALL_VEC_H
