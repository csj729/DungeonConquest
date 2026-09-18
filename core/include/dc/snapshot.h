// 상태 스냅샷 덤프/로드 (§10 검증 하네스).
//
// "임의 틱의 상태를 떠서 비교한다"는 디버깅 diff의 기본 단위다. 체크섬이
// **어느 틱·어느 영역**에서 갈렸는지 알려주면, 스냅샷이 **무엇이** 갈렸는지 알려준다.
//
// `World`가 trivially copyable이라 통짜 memcpy면 끝난다. 그 성질이 깨지면
// (가상 함수·포인터 멤버) 이 파일이 컴파일되지 않는다 — 설계 의도가 아니라
// 컴파일러가 강제한다.
//
// **memcmp로 스냅샷 두 개를 비교하지 말 것.** 구조체 패딩은 초기화되지 않으므로
// 논리적으로 같은 상태가 바이트로 다를 수 있다. 비교는 항상 `checksum()`으로 한다.
#ifndef DC_SNAPSHOT_H
#define DC_SNAPSHOT_H

#include <cstdint>
#include <cstring>
#include <type_traits>

#include "world.h"

namespace dc {

static_assert(std::is_trivially_copyable<World>::value,
              "World는 memcpy로 스냅샷 가능해야 한다 — 가상 함수·포인터 멤버 금지");

struct SnapshotHeader {
    uint32_t magic     = 0;
    uint32_t version   = 0;
    uint32_t worldSize = 0;
    uint32_t tick      = 0;
    uint64_t seed      = 0;
    uint64_t checksum  = 0;   // 로드 시 재계산해 대조한다
};

constexpr uint32_t SNAPSHOT_MAGIC   = 0x31534344u;   // 'DCS1'
// **포맷이 바뀌면 올린다.** 옛 스냅샷이 조용히 잘못 로드되는 것이
// 로드 실패보다 훨씬 비싸다.
constexpr uint32_t SNAPSHOT_VERSION = 1u;

enum class SnapshotStatus : int32_t {
    Ok               = 0,
    TooSmall         = 1,
    BadMagic         = 2,
    BadVersion       = 3,
    SizeMismatch     = 4,   // World 레이아웃이 바뀐 스냅샷
    ChecksumMismatch = 5,   // 바이트가 손상됐거나 해시 규칙이 바뀌었다
};

inline const char* snapshotStatusName(SnapshotStatus s) {
    switch (s) {
        case SnapshotStatus::Ok:               return "ok";
        case SnapshotStatus::TooSmall:         return "버퍼 부족";
        case SnapshotStatus::BadMagic:         return "매직 불일치";
        case SnapshotStatus::BadVersion:       return "버전 불일치";
        case SnapshotStatus::SizeMismatch:     return "World 크기 불일치";
        case SnapshotStatus::ChecksumMismatch: return "체크섬 불일치";
    }
    return "알 수 없음";
}

constexpr uint32_t snapshotSize() {
    return static_cast<uint32_t>(sizeof(SnapshotHeader) + sizeof(World));
}

// dst는 최소 snapshotSize() 바이트여야 한다. 쓴 바이트 수를 돌려준다(0이면 실패).
inline uint32_t snapshotSave(const World& w, void* dst, uint32_t dstLen) {
    if (dst == nullptr || dstLen < snapshotSize()) return 0;

    SnapshotHeader hdr;
    hdr.magic     = SNAPSHOT_MAGIC;
    hdr.version   = SNAPSHOT_VERSION;
    hdr.worldSize = static_cast<uint32_t>(sizeof(World));
    hdr.tick      = static_cast<uint32_t>(w.tickCount());
    hdr.seed      = w.masterSeed();
    hdr.checksum  = w.checksum();

    std::memcpy(dst, &hdr, sizeof(hdr));
    std::memcpy(static_cast<uint8_t*>(dst) + sizeof(hdr), &w, sizeof(World));
    return snapshotSize();
}

// 성공 시에만 w를 건드린다 — 실패해도 호출자의 World는 멀쩡하다.
inline SnapshotStatus snapshotLoad(World& w, const void* src, uint32_t srcLen) {
    if (src == nullptr || srcLen < sizeof(SnapshotHeader)) return SnapshotStatus::TooSmall;

    SnapshotHeader hdr;
    std::memcpy(&hdr, src, sizeof(hdr));
    if (hdr.magic != SNAPSHOT_MAGIC)              return SnapshotStatus::BadMagic;
    if (hdr.version != SNAPSHOT_VERSION)          return SnapshotStatus::BadVersion;
    if (hdr.worldSize != sizeof(World))           return SnapshotStatus::SizeMismatch;
    if (srcLen < snapshotSize())                  return SnapshotStatus::TooSmall;

    World tmp;
    std::memcpy(&tmp, static_cast<const uint8_t*>(src) + sizeof(hdr), sizeof(World));
    if (tmp.checksum() != hdr.checksum)           return SnapshotStatus::ChecksumMismatch;

    w = tmp;
    return SnapshotStatus::Ok;
}

}  // namespace dc

#endif  // DC_SNAPSHOT_H
