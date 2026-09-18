// 상태 해시 — 리플레이 검증의 계측 장비.
//
// **std::hash 금지** (CLAUDE.md). 구현체마다 값이 달라 서버-클라 불일치 직행이다.
// 여기서는 SplitMix64 finalizer를 결합자로 쓴다.
//
// ## 왜 바이트 FNV-1a가 아니라 워드 splitMix64인가 (측정)
//
// 체크섬 워크로드는 "8바이트 정렬된 값 수천 개"다. FNV-1a는 바이트당 곱셈 1회라
// 워드당 8회를 쓰는데, splitMix64는 워드당 곱셈 2회로 같은 확산을 얻는다.
//
//     엔티티 60기 × 필드 19 + 자유슬롯 964 = 워드 2136개, 24000틱(한 판)
//       바이트 FNV-1a    523.7 ms   틱당 21.82 us
//       워드 splitMix64  232.3 ms   틱당  9.68 us   ← 2.25배
//
// 한 판 기준 차이는 0.3초지만, 서버 리플레이 검증과 몬테카를로 하네스는
// 이걸 만 번 단위로 돌린다. 거기서는 5분과 2분의 차이다.
//
// FNV-1a는 그대로 남긴다(`rng.h`) — 바이트 스트림(데이터 파일 해시)에는 그쪽이 맞다.
//
// **필드 패킹은 하지 않았다.** 워드를 더 줄일 수는 있지만(작은 필드 여럿을 한
// 워드에 묶기) 그 대가로 `hashInto()`가 한눈에 읽히지 않게 된다. 패킹 버그는
// 곧 조용한 거짓 음성이고, 아래 실측을 보면 그 위험을 살 이유가 없다.
//
// ## 호출 빈도는 용도가 정한다 (실측, -O2)
//
//     생존  45기   8.6 us/회   →  24000틱 매 틱 호출 시   205 ms
//     생존 150기  17.9 us/회   →                          429 ms
//     생존 512기  50.0 us/회   →                         1200 ms
//     생존1024기  95.3 us/회   →                         2287 ms
//
// - **재현성 하네스**: 매 틱. 최초로 갈라진 틱을 찾아야 하므로 선택지가 없다
// - **서버 리플레이 검증**: 런 종료 시 1회, 또는 일정 간격
// - **몬테카를로 하네스**: 런 종료 시 1회. 만 번 × 매 틱은 34분짜리 계산이다
// - **클라이언트**: 평시에는 부르지 않는다
//
// ## 무엇을 넣고 무엇을 빼는가
//
//   [상태] 재현에 필요한 값        → 넣는다
//   [파생] 상태에서 다시 만들 수 있는 값 → **넣지 않는다**
//
// 파생값을 넣으면 지연 평가와 즉시 평가가 다른 해시를 내서 **거짓 양성**이 된다.
// 상태를 빠뜨리면 결과가 갈렸는데 해시는 같은 **거짓 음성**이 된다. 둘 다 치명적이라
// 각 구조체가 자기 `hashInto()`를 들고 있고, 필드 옆에 구분을 적어둔다.
#ifndef DC_CHECKSUM_H
#define DC_CHECKSUM_H

#include <cstdint>

#include "entity_id.h"
#include "fixed.h"
#include "rng.h"

namespace dc {

class Hasher {
public:
    constexpr Hasher() = default;
    explicit constexpr Hasher(uint64_t seed) : h_(seed) {}

    // 결합자. 순서에 민감하다 — 같은 값을 다른 순서로 넣으면 다른 해시가 나온다.
    // 체크섬은 순서까지 검증해야 하므로 이게 요구사항이다.
    constexpr void feed(uint64_t v) { h_ = splitMix64(h_ ^ v); }

    // 좁은 타입은 **부호 확장 없이** 폭만 맞춘다. int32_t -1을 0xFFFFFFFF로 보고
    // 0xFFFFFFFFFFFFFFFF로 보지 않는다 — 폭이 바뀌어도 해시가 흔들리지 않게.
    constexpr void feed(int64_t v)  { feed(static_cast<uint64_t>(v)); }
    constexpr void feed(uint32_t v) { feed(static_cast<uint64_t>(v)); }
    constexpr void feed(int32_t v)  { feed(static_cast<uint32_t>(v)); }
    constexpr void feed(uint16_t v) { feed(static_cast<uint32_t>(v)); }
    constexpr void feed(int16_t v)  { feed(static_cast<uint16_t>(v)); }
    constexpr void feed(uint8_t v)  { feed(static_cast<uint32_t>(v)); }
    constexpr void feed(int8_t v)   { feed(static_cast<uint8_t>(v)); }
    constexpr void feed(bool v)     { feed(static_cast<uint32_t>(v ? 1u : 0u)); }

    constexpr void feed(Fixed v)    { feed(v.raw); }
    constexpr void feed(EntityId v) { feed(v.bits); }
    constexpr void feed(const Rng& v) { feed(v.state()); }

    constexpr uint64_t value() const { return h_; }

private:
    // 0이 아닌 값에서 출발한다. 0에서 시작하면 "아무것도 안 넣은 해시"와
    // "0 하나를 넣은 해시"를 구별하기 어려운 구성이 생기기 쉽다.
    uint64_t h_ = 0x243F6A8885A308D3ull;
};

// 시스템별 부분 체크섬 (§10 검증 하네스).
//
// **갈린 틱만으로는 부족하다.** 어느 시스템이 갈렸는지까지 나와야 디버깅이
// 한 자리에서 끝난다. 시스템이 아직 없으므로 **그 시스템이 쓰는 상태 영역**으로
// 나눠둔다 — 시스템이 붙어도 이 대응은 그대로다.
//
//   entities ← Spawn · Targeting · Combat
//   hero     ← Combat · QTE · 레벨업
//   run      ← 진행(클리어 게이지)
//   spawn    ← Spawn
//   rng      ← 전역 스트림 소비 전부
struct Checksums {
    uint64_t total    = 0;
    uint64_t entities = 0;
    uint64_t hero     = 0;
    uint64_t run      = 0;
    uint64_t spawn    = 0;
    uint64_t rng      = 0;

    // total은 부분들의 함수다. 따라서 **"total은 갈렸는데 부분은 전부 같다"가
    // 구조적으로 불가능**하다 — 분기 지점을 항상 특정할 수 있다는 뜻이다.
    constexpr void seal(uint64_t tick, uint64_t seed) {
        Hasher h;
        h.feed(tick);
        h.feed(seed);
        h.feed(entities);
        h.feed(hero);
        h.feed(run);
        h.feed(spawn);
        h.feed(rng);
        total = h.value();
    }

    constexpr bool operator==(const Checksums& o) const {
        return total == o.total && entities == o.entities && hero == o.hero
            && run == o.run && spawn == o.spawn && rng == o.rng;
    }
    constexpr bool operator!=(const Checksums& o) const { return !(*this == o); }
};

// 처음으로 어긋난 영역의 이름. 같으면 nullptr.
// 순서는 틱 루프의 실행 순서에 맞춘다 — 먼저 도는 시스템이 먼저 보고된다.
inline const char* firstDivergentDomain(const Checksums& a, const Checksums& b) {
    if (a.spawn    != b.spawn)    return "spawn";
    if (a.entities != b.entities) return "entities";
    if (a.hero     != b.hero)     return "hero";
    if (a.run      != b.run)      return "run";
    if (a.rng      != b.rng)      return "rng";
    if (a.total    != b.total)    return "total";   // 도달하면 seal()이 깨진 것
    return nullptr;
}

}  // namespace dc

#endif  // DC_CHECKSUM_H
