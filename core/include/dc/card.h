// 레벨업 카드 (§4) — **성장의 주력이자 빌드가 갈리는 지점.**
//
// ## 구현 범위
//
// 이 파일은 **추첨 · 비복원 · 전설 풀 · 선택 · 누적**까지다.
// 각 각인의 개별 효과(관통 피해, 부식 DoT, 군집 가산 …)는 각각이 별도 콘텐츠이고
// §9 모디파이어 위에 얹히므로, 여기서는 **누적 수치만 상태로 들고 있는다.**
// 그 수치가 곧 효과의 입력이 되므로 순서가 뒤집히지 않는다.
//
// ## 결정론 (§4)
//
// - 전설 풀만 상태를 갖는다 — **ID 오름차순 고정 배열 + 획득 비트마스크.**
//   `std::unordered_set` 순회 금지 규칙에 걸리지 않는다
// - 추첨은 **"살아있는 후보를 배열 순서로 세어 k번째"** 로 한다.
//   기각 재시도를 쓰면 난수 소비 횟수가 후보 상태에 따라 달라져
//   같은 시드에서도 뒤가 흔들린다
// - 누적은 덧셈뿐이다. PercentAdd 누산기는 O(1)이고 순서에 의존하지 않는다
#ifndef DC_CARD_H
#define DC_CARD_H

#include <cstdint>

#include "checksum.h"
#include "fixed.h"

namespace dc {

constexpr uint32_t MAX_CARD_GRADES    = 5;   // 일반 · 고급 · 희귀 · 영웅 · 전설
constexpr uint32_t MAX_ENGRAVINGS     = 16;
constexpr uint32_t MAX_RELICS         = 16;
constexpr uint32_t MAX_LEGEND_POOL    = 32;
constexpr uint32_t MAX_CARDS_PER_LEVEL = 4;
constexpr uint32_t MAX_LEVEL_NEED     = 80;

// 각인 인덱스. **`data/cards.json`의 배열 순서와 같아야 한다** —
// `tools/verify_core_constants.py`가 둘을 대조한다.
// 효과가 구현된 것만 코어가 이름으로 참조하고, 나머지는 아직 수치만 쌓인다.
enum class EngraveId : uint16_t {
    Pierce = 0,   // 관통
    Chain  = 1,   // 연타
    Decay  = 2,   // 부식
    Leech  = 3,   // 흡혈 — 구현됨 (§2 잠식 회복)
    Swarm  = 4,   // 군집
    Crit   = 5,   // 예리함
    Rend   = 6,   // 파쇄
    Wide   = 7,   // 확장
    Count  = 8,
};

constexpr uint16_t engraveIndex(EngraveId e) { return static_cast<uint16_t>(e); }

// 유물 인덱스. **`data/cards.json`의 relics 배열 순서와 같아야 한다.**
// 각인이 스킬의 성질을 바꾸는 것과 달리, 유물은 전장에 규칙을 하나 더한다.
enum class RelicId : uint16_t {
    Rage   = 0,   // 분노의 토템 — 피격 시 공격력 중첩
    Bolt   = 1,   // 뇌전의 성물 — 주기적 자동 피해
    Frost  = 2,   // 서리 오라 — 반경 내 이동속도 감소
    Beacon = 3,   // 추적의 신호탄 — 엘리트·보스 피해 증가
    Tide   = 4,   // 밀물의 인장 — 구간 경과에 비례한 공격력
    Greed  = 5,   // 탐욕의 주머니 — 골드 획득 (골드 시스템 미구현이라 효과 보류)
    Count  = 6,
};

constexpr uint16_t relicIndex(RelicId r) { return static_cast<uint16_t>(r); }

// **뒤에만 덧붙인다** — 값이 바뀌면 기존 리플레이가 깨진다.
enum class CardKind : uint8_t {
    None      = 0,
    StatBoost = 1,   // 일반 — 기본 스탯. 위력 증가분이 곧 등급 예산이다
    Engraving = 2,   // 고급~영웅 — 공통 각인. 등급은 수치 티어다
    Relic     = 3,   // 고급~영웅 — 유물. 스킬과 무관하게 독립 작동
    Legend    = 4,   // 전설 — 고유 각인 또는 전설 유물. 잭팟 자리
    Count     = 5,
};

struct CardOffer {
    CardKind kind    = CardKind::None;
    uint8_t  grade   = 0;      // 0 일반 ~ 4 전설
    uint16_t entryId = 0;      // 각인/유물/전설 풀 인덱스
    Fixed    value{};          // 이 한 장이 얹어주는 양

    void hashInto(Hasher& h) const {
        h.feed(static_cast<uint8_t>(kind));
        h.feed(grade);
        h.feed(entryId);
        h.feed(value);
    }
};

// 한 번의 레벨업이 제시하는 선택지.
struct CardOfferSet {
    CardOffer offers[MAX_CARDS_PER_LEVEL]{};
    uint8_t   count = 0;

    bool open() const { return count > 0; }
    void clear() { count = 0; }

    // **같은 화면 중복 금지** — 4장은 비복원 추출이다 (§4).
    bool contains(CardKind k, uint16_t id) const {
        for (uint8_t i = 0; i < count && i < MAX_CARDS_PER_LEVEL; ++i) {
            if (offers[i].kind == k && offers[i].entryId == id) return true;
        }
        return false;
    }

    void hashInto(Hasher& h) const {
        h.feed(count);
        for (uint8_t i = 0; i < count && i < MAX_CARDS_PER_LEVEL; ++i) offers[i].hashInto(h);
    }
};

// 카드가 만든 성장의 누적 상태. **전부 체크섬 입력이다.**
struct CardState {
    // 각인·유물의 누적 수치. 중복 선택은 승급이 아니라 **수치 누적**이다 (§4).
    Fixed    engrave[MAX_ENGRAVINGS]{};
    Fixed    relic[MAX_RELICS]{};
    // 전설 풀 획득 비트마스크. 32종까지 uint32 한 칸이다.
    uint32_t legendTaken = 0;
    // 통계 — 몬테카를로 하네스가 "전설 획득 횟수별 클리어율"을 집계할 때 쓴다 (§14)
    int32_t  takenByGrade[MAX_CARD_GRADES] = {0, 0, 0, 0, 0};
    int32_t  rerolls = 0;
    // 밀린 레벨업. 연속 레벨업도 **한 번에 한 화면씩** 처리한다 —
    // 화면을 겹쳐 띄우면 비복원 추출의 범위가 모호해진다.
    int32_t  pendingLevelUps = 0;

    CardOfferSet offer{};

    bool legendHas(uint32_t i) const { return (legendTaken & (1u << i)) != 0; }
    void legendTake(uint32_t i) { legendTaken |= (1u << i); }

    // 남은 전설 수. 0이면 등급 강등 (§4 전설 풀 고갈 폴백).
    uint32_t legendLeft(uint32_t poolSize) const {
        uint32_t n = 0;
        for (uint32_t i = 0; i < poolSize && i < MAX_LEGEND_POOL; ++i) {
            if (!legendHas(i)) ++n;
        }
        return n;
    }

    void hashInto(Hasher& h) const {
        for (uint32_t i = 0; i < MAX_ENGRAVINGS; ++i) h.feed(engrave[i]);
        for (uint32_t i = 0; i < MAX_RELICS; ++i) h.feed(relic[i]);
        h.feed(legendTaken);
        for (uint32_t i = 0; i < MAX_CARD_GRADES; ++i) h.feed(takenByGrade[i]);
        h.feed(rerolls);
        h.feed(pendingLevelUps);
        offer.hashInto(h);
    }
};

}  // namespace dc

#endif  // DC_CARD_H
