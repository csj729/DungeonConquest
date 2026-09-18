// 결정론 난수 — 시드에서 파생된 스트림만 쓴다.
//
// **전역 RNG 금지** (CLAUDE.md). `World`의 마스터 시드 하나에서 용도별 스트림을
// 파생시킨다. 스트림을 나누면 한 시스템의 호출 횟수가 바뀌어도 다른 시스템의
// 난수열이 흔들리지 않는다 — 예: 스폰 위치를 하나 더 뽑는다고 전투 판정이
// 달라지면 밸런스를 만질 때마다 리플레이가 깨진다.
//
// 상태는 uint64_t 하나다. **체크섬 입력에 포함**되므로 작아야 한다.
#ifndef DC_RNG_H
#define DC_RNG_H

#include <cstdint>

namespace dc {

// 스트림 구분자. 새 시스템이 난수를 쓰면 여기에 추가한다.
// **값을 바꾸면 기존 리플레이가 전부 깨진다** — 뒤에만 덧붙일 것.
enum class RngStream : uint32_t {
    Spawn      = 1,   // 스폰 위치 · 방향 안의 좌표
    Combat     = 2,   // 통합 proc · 치명타
    Cards      = 3,   // 카드 등급 · 각인/유물 추첨
    Items      = 4,   // 아이템 뽑기
    Events     = 5,   // 돌발 이벤트
};

// SplitMix64 finalizer. 시드 파생 전용이며 std::hash를 대체한다
// (std::hash는 구현체마다 값이 달라 서버-클라 불일치 직행 — CLAUDE.md).
constexpr uint64_t splitMix64(uint64_t x) {
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

// FNV-1a 64비트. 체크섬과 데이터 해시에 쓴다. 역시 std::hash 대체.
constexpr uint64_t FNV_OFFSET = 1469598103934665603ull;
constexpr uint64_t FNV_PRIME  = 1099511628211ull;

constexpr uint64_t fnv1a(uint64_t h, uint8_t byte) {
    return (h ^ static_cast<uint64_t>(byte)) * FNV_PRIME;
}

inline uint64_t fnv1aBytes(uint64_t h, const void* data, uint32_t len) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    for (uint32_t i = 0; i < len; ++i) h = fnv1a(h, p[i]);
    return h;
}

// 확률 고정 스케일. 65536 = 100%.
constexpr uint32_t Q16_ONE = 65536u;

class Rng {
public:
    constexpr Rng() = default;
    explicit constexpr Rng(uint64_t seed) : state_(seed ? seed : 0x9E3779B97F4A7C15ull) {}

    // 마스터 시드에서 용도별 스트림을 파생시킨다.
    static constexpr Rng derive(uint64_t masterSeed, RngStream stream) {
        return Rng(splitMix64(masterSeed ^ (static_cast<uint64_t>(stream) * 0x9E3779B97F4A7C15ull)));
    }

    // **엔티티별 스트림.** 스폰 시 한 번 호출해 엔티티가 자기 상태를 들고 다닌다.
    //
    // 용도별 스트림 하나를 여러 엔티티가 공유하면 **소비 순서가 결과를 바꾼다** —
    // 시스템 실행 순서를 바꾸거나 병렬화하는 순간 리플레이가 깨진다.
    // 엔티티별 파생은 순서 독립이므로 그 제약에서 자유롭다.
    //
    // 입력에 EntityId.bits를 통째로 넣는다. generation이 섞이므로 **슬롯을
    // 재사용해도 새 엔티티가 죽은 엔티티의 난수열을 물려받지 않는다.**
    static constexpr Rng deriveEntity(uint64_t masterSeed, uint32_t entityBits) {
        return Rng(splitMix64(masterSeed
                              ^ (static_cast<uint64_t>(entityBits) << 32)
                              ^ 0xD1B54A32D192ED03ull));   // 용도 스트림과 겹치지 않는 태그
    }

    // xorshift64* — 상태 8바이트, 주기 2^64-1. 통계 품질은 게임 용도에 충분하고
    // 상태가 작아 체크섬에 넣기 좋다.
    constexpr uint64_t nextU64() {
        state_ ^= state_ >> 12;
        state_ ^= state_ << 25;
        state_ ^= state_ >> 27;
        return state_ * 0x2545F4914F6CDD1Dull;
    }

    constexpr uint32_t nextU32() { return static_cast<uint32_t>(nextU64() >> 32); }

    // [0, n) 균등. **모듈로 편향을 제거한다.**
    // 그냥 `x % n`을 쓰면 n이 2의 거듭제곱이 아닐 때 앞쪽 값이 더 자주 나온다.
    // 예: 5로 나누면 0~1이 나머지보다 약 25% 더 자주 뽑힌다 — 카드 등급 추첨처럼
    // 확률을 검산해둔 곳에서는 그대로 밸런스 오차가 된다.
    constexpr uint32_t range(uint32_t n) {
        if (n <= 1) return 0;
        const uint32_t reject = static_cast<uint32_t>(0u - n) % n;   // 2^32 mod n
        uint32_t v = 0;
        do { v = nextU32(); } while (v < reject);
        return v % n;
    }

    // q16(65536 = 100%) 확률로 참. **낮은 확률 전용 스케일이다.**
    //
    // permille은 1/1000이라 3.2% 같은 값을 32로밖에 담지 못해 상대오차가 커진다
    // (PRD 상수 C에서 실측 3.0%). §10이 예고한 "5% 미만 확률은 별도 고정
    // 스케일(65536 = 100%)로 분리"가 이것이다 — 같은 값에서 오차가 0.02%로 떨어진다.
    //
    // 2^16이 2^64를 정확히 나누므로 **기각 표집이 필요 없고 편향도 없다.**
    // 상위 16비트를 쓰는 이유는 xorshift64*의 하위 비트가 상위보다 약하기 때문이다.
    constexpr bool chanceQ16(uint32_t q) {
        if (q == 0) return false;
        if (q >= Q16_ONE) return true;
        return static_cast<uint32_t>(nextU64() >> 48) < q;
    }

    // permille(1/1000) 확률로 참. data/*.json의 표현과 그대로 맞물린다.
    constexpr bool chancePermille(int32_t pm) {
        if (pm <= 0) return false;
        if (pm >= 1000) return true;
        return range(1000) < static_cast<uint32_t>(pm);
    }

    // 가중치 추첨 — 통합 proc의 스킬 선택(§3)이 이 형태다.
    // 동점·경계 처리를 한 곳에 모아둔다.
    constexpr uint32_t weighted(const int32_t* weights, uint32_t count) {
        int64_t total = 0;
        for (uint32_t i = 0; i < count; ++i) total += weights[i];
        if (total <= 0) return 0;
        int64_t roll = static_cast<int64_t>(range(static_cast<uint32_t>(total)));
        for (uint32_t i = 0; i < count; ++i) {
            roll -= weights[i];
            if (roll < 0) return i;
        }
        return count - 1;   // 도달 불가 — 방어
    }

    constexpr uint64_t state() const { return state_; }
    constexpr void setState(uint64_t s) { state_ = s; }

private:
    uint64_t state_ = 0x9E3779B97F4A7C15ull;
};

}  // namespace dc

#endif  // DC_RNG_H
