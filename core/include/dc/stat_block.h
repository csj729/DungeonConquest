// 모디파이어 시스템 (§9).
//
// 아이템·패시브 스킬·스킬 강화·이벤트 효과는 **전부 이 컨테이너 하나**다.
// 새 시스템을 만들지 않는다 — 그게 "새 던전 기믹은 코어에 if 추가가 아니라
// 기존 모디파이어 조합으로 표현할 것"(CLAUDE.md)의 실체다.
//
// ## 적용 순서 (고정)
//
//     1. Flat        — 누적합           (순서 무관)
//     2. PercentAdd  — 누적합 후 1회 적용 (순서 무관)
//     3. PercentMult — sourceId 오름차순 (순서 의존)
//     4. Override    — sourceId 최소값 하나만, 나머지 무시
//     5. 하한 클램프
//
// `Fixed`는 정수라 **덧셈에 결합·교환 법칙이 성립한다.** 따라서 1·2는 정렬이
// 필요 없고, 순서 고정이 실제로 필요한 것은 3·4뿐이다.
//
// ## 왜 PercentMult는 누적 곱으로 관리하지 않는가
//
// 덧셈은 정확한 역연산이 있다(더한 만큼 빼면 완전 복구). 곱셈의 역연산인 정수
// 나눗셈은 나머지를 버린다. 누적 곱으로 관리하면 **장비를 착탈할 때마다 반올림
// 오차가 쌓여 결정론이 깨진다.** 그래서 PercentMult만 매번 처음부터 다시 곱한다.
// 원소가 ≤8개라 값싼 대가다.
//
// ## permille 변환 오차는 설계된 것이다
//
// `Fixed::fromPermille`은 0방향 절삭이라 200permille + 300permille이 정확히 0.5가
// 아니다 (819 + 1228 = 2047, 0.5는 2048). 그래서 `150 × (1 + 0.2 + 0.3)`은 225가
// 아니라 224.96이 나온다.
//
// permille을 먼저 정수로 더하고 한 번만 변환하면 정확하지만, 모디파이어는 출처가
// 제각각(아이템·각인·이벤트)이라 그럴 수 없다. 오차는 모디파이어 하나당 최대
// 1/4096(0.024%)이고, §10이 20.12를 고른 근거인 "1% 표현 오차 0.098%"와 같은 자리다.
//
// **파이썬 검증 도구는 float로 계산하므로 여기와 소수점 아래가 다르다.** 의도된
// 차이다 — 파이썬은 밸런스 밴드를 판정하지 비트 일치를 판정하지 않는다.
//
// ## 체크섬
//
//   [상태] base · accumFlat · accumPctAdd · mults · overrides · bounds
//   [파생] cached · dirty   ← **해시에 넣지 않는다**
//
// cached/dirty를 넣으면 "지연 평가한 World"와 "즉시 평가한 World"가 다른 해시를
// 내서 거짓 양성이 된다. 둘은 관측 가능한 값이 완전히 같으므로 같은 해시여야 한다.
//
// bounds는 넣는다. 데이터에서 오는 값이라 런마다 같지만, 서버와 클라가 **다른
// stats.json을 로드한 경우** 틱 0에서 바로 잡힌다.
#ifndef DC_STAT_BLOCK_H
#define DC_STAT_BLOCK_H

#include <cstdint>

#include "checksum.h"
#include "fixed.h"
#include "small_vec.h"

namespace dc {

// 모디파이어 대상 스탯. **뒤에만 덧붙인다** — 값이 바뀌면 리플레이가 깨진다.
enum class Stat : uint8_t {
    AttackPower   = 0,
    AttackSpeed   = 1,   // 초당 공격 횟수. 간격(틱)은 여기서 파생된다
    Armor         = 2,
    Range         = 3,
    CorruptionMax = 4,   // 옛 MaxHealth (§2에서 잠식 게이지로 통합)
    CritChance    = 5,
    CritMult      = 6,
    ProcRate      = 7,   // 통합 proc 발동률 (§3)
    MoveSpeed     = 8,
    Count         = 9,
};

constexpr uint32_t STAT_COUNT = static_cast<uint32_t>(Stat::Count);
constexpr uint32_t statIndex(Stat s) { return static_cast<uint32_t>(s); }

inline const char* statName(Stat s) {
    switch (s) {
        case Stat::AttackPower:   return "attack_power";
        case Stat::AttackSpeed:   return "attack_speed";
        case Stat::Armor:         return "armor";
        case Stat::Range:         return "range";
        case Stat::CorruptionMax: return "corruption_max";
        case Stat::CritChance:    return "crit_chance";
        case Stat::CritMult:      return "crit_mult";
        case Stat::ProcRate:      return "proc_rate";
        case Stat::MoveSpeed:     return "move_speed";
        case Stat::Count:         break;
    }
    return "?";
}

// 하한. **밸런스 다이얼이 아니라 불변식이다** — 나눗셈 분모가 0이 되거나 값이
// 음수로 뒤집히는 것을 막는다. `data/stats.json`에서 온다.
struct StatBounds {
    Fixed minValue{};
    Fixed minPctAddSum = Fixed::fromPermille(-1000);
};

// sourceId가 붙은 모디파이어. sourceId는 World의 전역 단조 증가 카운터가 발급한다.
struct SourcedMod {
    uint32_t sourceId = 0;
    Fixed    value{};
};

// **정렬 키는 sourceId 하나뿐이고 유일하므로 완전 순서다.** 포인터 값이나
// 주소를 키로 쓰지 않는다 (CLAUDE.md).
constexpr bool operator<(const SourcedMod& a, const SourcedMod& b) {
    return a.sourceId < b.sourceId;
}

struct StatEntry {
    // [상태]
    Fixed base{};
    Fixed accumFlat{};      // Flat 누적합 — 추가/제거가 O(1) 가감
    Fixed accumPctAdd{};    // PercentAdd 누적합 — 클램프 전 원값을 저장한다
    SmallVec<SourcedMod, 8> mults{};
    SmallVec<SourcedMod, 4> overrides{};

    // [파생] — 체크섬 제외
    mutable Fixed cached{};
    mutable bool  dirty = true;
};

class StatBlock {
public:
    // bounds는 데이터(stats.json)에서 온다. **포인터로 들고 있지 않고 복사한다** —
    // World가 memcpy로 스냅샷되므로 포인터 멤버는 복원 시 매달린 참조가 된다.
    void init(const Fixed* baseValues, const StatBounds* bounds) {
        for (uint32_t i = 0; i < STAT_COUNT; ++i) {
            entries_[i] = StatEntry{};
            entries_[i].base = baseValues != nullptr ? baseValues[i] : Fixed{};
            bounds_[i]       = bounds != nullptr ? bounds[i] : StatBounds{};
            entries_[i].dirty = true;
        }
    }

    void  setBase(Stat s, Fixed v) { entries_[statIndex(s)].base = v; markDirty(s); }
    Fixed base(Stat s) const       { return entries_[statIndex(s)].base; }

    void setBounds(Stat s, const StatBounds& b) { bounds_[statIndex(s)] = b; markDirty(s); }
    const StatBounds& bounds(Stat s) const      { return bounds_[statIndex(s)]; }

    // ── Flat / PercentAdd — O(1). 재계산조차 필요 없다 ──────────────
    //
    // 제거는 **같은 값을 되돌려 빼는 것**으로 한다. `Fixed`가 정수라 덧셈의
    // 역연산이 정확하므로 안전하다. 호출자가 추가할 때 쓴 값을 그대로 넘겨야 한다.
    void addFlat(Stat s, Fixed v)      { entries_[statIndex(s)].accumFlat += v;   markDirty(s); }
    void removeFlat(Stat s, Fixed v)   { entries_[statIndex(s)].accumFlat -= v;   markDirty(s); }
    void addPctAdd(Stat s, Fixed v)    { entries_[statIndex(s)].accumPctAdd += v; markDirty(s); }
    void removePctAdd(Stat s, Fixed v) { entries_[statIndex(s)].accumPctAdd -= v; markDirty(s); }

    // ── PercentMult — sourceId 오름차순 불변식을 삽입으로 유지 ────────
    bool addMult(Stat s, uint32_t sourceId, Fixed v) {
        StatEntry& e = entries_[statIndex(s)];
        if (sourceId == 0) return false;                  // 0은 "없음" 예약
        if (findSource(e.mults, sourceId) >= 0) return false;   // sourceId는 유일해야 한다
        if (!e.mults.insertSorted(SourcedMod{sourceId, v})) return false;
        markDirty(s);
        return true;
    }
    bool removeMult(Stat s, uint32_t sourceId) {
        StatEntry& e = entries_[statIndex(s)];
        const int32_t at = findSource(e.mults, sourceId);
        if (at < 0) return false;
        e.mults.eraseAt(static_cast<uint32_t>(at));
        markDirty(s);
        return true;
    }

    // ── Override — sourceId 최소값 하나만 적용, 나머지는 무시 ─────────
    bool addOverride(Stat s, uint32_t sourceId, Fixed v) {
        StatEntry& e = entries_[statIndex(s)];
        if (sourceId == 0) return false;
        if (findSource(e.overrides, sourceId) >= 0) return false;
        if (!e.overrides.insertSorted(SourcedMod{sourceId, v})) return false;
        markDirty(s);
        return true;
    }
    bool removeOverride(Stat s, uint32_t sourceId) {
        StatEntry& e = entries_[statIndex(s)];
        const int32_t at = findSource(e.overrides, sourceId);
        if (at < 0) return false;
        e.overrides.eraseAt(static_cast<uint32_t>(at));
        markDirty(s);
        return true;
    }

    // ── 읽기 — 지연 평가 ──────────────────────────────────────────
    Fixed value(Stat s) const {
        const StatEntry& e = entries_[statIndex(s)];
        if (e.dirty) {
            e.cached = compute(s);
            e.dirty  = false;
        }
        return e.cached;
    }

    // 캐시를 건너뛴 직접 계산. 캐시가 거짓말하지 않는지 테스트가 이걸로 대조한다.
    Fixed compute(Stat s) const {
        const uint32_t i = statIndex(s);
        const StatEntry&  e = entries_[i];
        const StatBounds& b = bounds_[i];

        Fixed v = e.base + e.accumFlat;

        // **클램프는 저장이 아니라 읽을 때 한다.** 저장 시 클램프하면
        // add/remove가 정확한 역연산이 아니게 되고, 조건 토글에서 값이 샌다.
        const Fixed pct = fixedMax(e.accumPctAdd, b.minPctAddSum);
        v = v * (Fixed::one() + pct);

        for (uint32_t k = 0; k < e.mults.size(); ++k) {
            v = v * (Fixed::one() + e.mults[k].value);
        }

        // 오름차순이므로 front()가 sourceId 최소값이다.
        if (!e.overrides.empty()) v = e.overrides[0].value;

        // **Override도 하한을 넘지 못한다.** 불변식은 무조건 성립해야 하므로.
        // 속박이 이동속도를 0으로 만들어야 하면 move_speed의 하한을 0으로 둔다.
        return fixedMax(v, b.minValue);
    }

    bool dirty(Stat s) const { return entries_[statIndex(s)].dirty; }

    // 모든 캐시를 즉시 채운다. 스냅샷 직후나 벤치마크에서 쓴다.
    void warmAll() const {
        for (uint32_t i = 0; i < STAT_COUNT; ++i) (void)value(static_cast<Stat>(i));
    }

    uint32_t modCount(Stat s) const {
        const StatEntry& e = entries_[statIndex(s)];
        return e.mults.size() + e.overrides.size();
    }

    // [파생]인 cached/dirty는 넣지 않는다 — 파일 상단 주석 참조.
    void hashInto(Hasher& h) const {
        for (uint32_t i = 0; i < STAT_COUNT; ++i) {
            const StatEntry& e = entries_[i];
            h.feed(e.base);
            h.feed(e.accumFlat);
            h.feed(e.accumPctAdd);
            h.feed(e.mults.size());
            for (uint32_t k = 0; k < e.mults.size(); ++k) {
                h.feed(e.mults[k].sourceId);
                h.feed(e.mults[k].value);
            }
            h.feed(e.overrides.size());
            for (uint32_t k = 0; k < e.overrides.size(); ++k) {
                h.feed(e.overrides[k].sourceId);
                h.feed(e.overrides[k].value);
            }
            h.feed(bounds_[i].minValue);
            h.feed(bounds_[i].minPctAddSum);
        }
    }

private:
    void markDirty(Stat s) { entries_[statIndex(s)].dirty = true; }

    template <uint32_t N>
    static int32_t findSource(const SmallVec<SourcedMod, N>& v, uint32_t sourceId) {
        for (uint32_t i = 0; i < v.size(); ++i) {
            if (v[i].sourceId == sourceId) return static_cast<int32_t>(i);
        }
        return -1;
    }

    StatEntry  entries_[STAT_COUNT]{};
    StatBounds bounds_[STAT_COUNT]{};
};

}  // namespace dc

#endif  // DC_STAT_BLOCK_H
