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
// 아이템 뽑기 1칸 + 성장 카드 3장 (§4의 4칸 레이아웃).
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
    Greed  = 5,   // 탐욕의 주머니 — 경험치 획득 증가
    Count  = 6,
};

constexpr uint16_t relicIndex(RelicId r) { return static_cast<uint16_t>(r); }

// 전설 풀 인덱스. **전설 유물 3종이 앞을 차지하고 뒤는 직업 고유 각인이다**
// (전사 6종). 유물 3종은 효과가 붙어 있고, 고유 각인은 **수치가 설계되지 않아
// 효과가 없다** — 풀에는 남아 있으므로 뽑히기는 하고, 그만큼 전설 기대값이 낮다.
//
// 고유 각인은 효과의 *성질*만 정해져 있다 (heroes_vertical_slice.md §4: 처형·
// 충격파·소용돌이·원심력·여진·균열). 전부 "배수가 아니라 스킬의 성질을 바꾼다"
// 쪽이라 수치 하나가 아니라 설계 결정이 필요하다 — 임의로 채우지 않는다.
enum class LegendId : uint16_t {
    Echo  = 0,   // 무한의 메아리 — 기본 공격 1회 추가 발동
    Storm = 1,   // 폭풍의 핵 — 영웅 주위 상시 회전 칼날
    Forge = 2,   // 대장장이의 화로 — 구간 종료 시 아이템 1개 획득
    // 3~8: 전사 고유 각인. **순서는 `data/cards.json`의 `unique_engravings`와
    // 같다** — 풀 인덱스가 리플레이에 기록되므로 순서를 바꾸면 기존 로그가 깨진다.
    Execute    = 3,   // 처형 — 체력 임계 이하 즉시 처치 (영웅의 모든 타격)
    Shockwave  = 4,   // 충격파 — 단일 타격이 직선 관통으로, 전력 피해
    Vortex     = 5,   // 소용돌이 — 회전이 지속 장판으로
    Centrifuge = 6,   // 원심력 — 적을 벨 때마다 반경 증가
    Aftershock = 7,   // 여진 — 캐스트 중심에 2차 폭발
    Fissure    = 8,   // 균열 — 캐스트 중심에 지속 둔화 지역
    UniqueFirst = 3,
};

// **9칸 전부 효과가 있다.** Vortex · Aftershock · Fissure는 `ZoneState`(world.h)
// 하나를 `kind` 값 세 개로 나눠 쓴다 — 위치 + 수명 + 상한 + 안정 압축이 셋 다
// 같은 모양이라, `if`를 늘리지 않고 풀 하나로 표현된다.
//
// **각인이 어느 스킬에 붙는지는 `SimConfig::uniqueSkill`이 안다** (데이터에서 온다).
// `aoe` 여부로 가르면 원심력이 광역기 둘 다에 걸려 예산의 221%가 된다.

constexpr uint32_t legendIndexOf(LegendId l) { return static_cast<uint32_t>(l); }

// **뒤에만 덧붙인다** — 값이 바뀌면 기존 리플레이가 깨진다.
enum class CardKind : uint8_t {
    None      = 0,
    StatBoost = 1,   // 일반 — 기본 스탯. 위력 증가분이 곧 등급 예산이다
    Engraving = 2,   // 고급~영웅 — 공통 각인. 등급은 수치 티어다
    Relic     = 3,   // 고급~영웅 — 유물. 스킬과 무관하게 독립 작동
    Legend    = 4,   // 전설 — 고유 각인 또는 전설 유물. 잭팟 자리
    // **맨 왼쪽 고정 칸** (§4). 항상 흔함 등급 아이템을 뽑는다.
    // 위력의 주력이 여기서 들어온다 — 한 판 성장의 70%가 아이템 조합 몫이다.
    ItemDraw  = 5,
    Count     = 6,
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
