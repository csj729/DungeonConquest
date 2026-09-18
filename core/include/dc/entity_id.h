// 엔티티 식별자 — index 12비트 + generation 20비트, uint32_t 한 칸.
//
// **해결하는 문제는 결정론이 아니라 dangling 참조다.** 엔티티가 죽고 그 슬롯이
// 재사용되면, 옛 참조가 엉뚱한 새 엔티티를 가리킨다. 세대 카운터가 그걸 막는다 —
// 슬롯을 재사용할 때마다 generation이 오르므로 옛 ID는 더 이상 일치하지 않는다.
//
// 결정론에는 간접적으로 기여한다:
//   - 포인터 대신 **안정적인 정수 키**를 주므로 "포인터를 정렬 키로 쓰지 마라"
//     (CLAUDE.md)를 지킬 수 있다. 주소는 실행마다 다르지만 ID는 같다
//   - 틱 끝 일괄 압축으로 인덱스가 밀려도 ID는 유효하다
//
// 비트 배분: index 12비트 = 4096칸 (MAX_ENTITIES 1024의 4배 여유),
//            generation 20비트 = 슬롯당 약 105만 번 재사용 후 순환.
//            한 판 20분에 몹 약 900마리이므로 순환은 사실상 도달 불가.
#ifndef DC_ENTITY_ID_H
#define DC_ENTITY_ID_H

#include <cstdint>

namespace dc {

struct EntityId {
    static constexpr uint32_t INDEX_BITS = 12;
    static constexpr uint32_t GEN_BITS   = 20;
    static constexpr uint32_t INDEX_MASK = (1u << INDEX_BITS) - 1;   // 0xFFF
    static constexpr uint32_t GEN_MASK   = (1u << GEN_BITS) - 1;     // 0xFFFFF
    static constexpr uint32_t MAX_INDEX  = INDEX_MASK;

    // 0은 "없음"으로 쓰지 않는다 — index 0 · generation 0이 유효한 조합이기
    // 때문이다. 대신 generation 0을 무효로 약속하고, 살아있는 엔티티의
    // generation은 항상 1 이상이다.
    uint32_t bits = 0;

    constexpr EntityId() = default;

    static constexpr EntityId make(uint32_t index, uint32_t generation) {
        return EntityId{ (index & INDEX_MASK) | ((generation & GEN_MASK) << INDEX_BITS) };
    }

    static constexpr EntityId invalid() { return EntityId{0}; }

    constexpr uint32_t index() const      { return bits & INDEX_MASK; }
    constexpr uint32_t generation() const { return (bits >> INDEX_BITS) & GEN_MASK; }
    constexpr bool     valid() const      { return generation() != 0; }

    // generation 순환 시 0을 건너뛴다 (0은 무효 약속이므로)
    static constexpr uint32_t nextGeneration(uint32_t g) {
        const uint32_t n = (g + 1) & GEN_MASK;
        return n == 0 ? 1 : n;
    }
};

constexpr bool operator==(EntityId a, EntityId b) { return a.bits == b.bits; }
constexpr bool operator!=(EntityId a, EntityId b) { return a.bits != b.bits; }

// 정렬 키로 쓸 수 있도록 전순서를 준다. **동점 처리에 이걸 쓴다** —
// "가장 가까운 적"에서 거리가 같으면 EntityId로 갈라 순회 순서 의존을 없앤다.
constexpr bool operator<(EntityId a, EntityId b) { return a.bits < b.bits; }

}  // namespace dc

#endif  // DC_ENTITY_ID_H
