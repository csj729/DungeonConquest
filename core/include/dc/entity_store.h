// 엔티티 저장소 — SoA 밀집 배열 + 핸들 간접 테이블.
//
// 세 가지를 동시에 만족해야 한다:
//   1. **순회가 밀집해야 한다.** 적 간 충돌은 N×N이고 타겟 탐색은 브루트포스다(§11).
//      죽은 칸을 건너뛰며 스캔하면 그 비용이 그대로 늘어난다
//   2. **참조가 안정해야 한다.** 영웅의 타겟, 이벤트 큐의 대상은 틱을 넘어 살아남는다
//   3. **순서가 결정적이어야 한다.** 체크섬과 시스템 순회가 이 배열 순서를 본다
//
// 1과 2는 정면충돌한다(밀집시키면 인덱스가 밀리고, 인덱스를 고정하면 구멍이 생긴다).
// **핸들 간접**으로 푼다 — 바깥에는 안정적인 EntityId를 주고, 안으로는 밀집 배열을 쓴다.
//
//     EntityId(slot, generation) ──slotToDense_──> dense 인덱스 [0, count_)
//                                <──denseId_──────
//
// 레이아웃 선택 근거는 이 파일 아래 "압축 방식" 주석 참조.
#ifndef DC_ENTITY_STORE_H
#define DC_ENTITY_STORE_H

#include <cstdint>

#include "checksum.h"
#include "config.h"
#include "entity_id.h"
#include "fixed.h"
#include "rng.h"

namespace dc {

// 엔티티 분류. 성장 곡선·타겟 우선순위·QTE 대상 여부가 전부 여기서 갈린다(§2·§3).
enum class Archetype : uint8_t {
    Trash = 0,
    Elite = 1,
    Boss  = 2,
};

// 비트 플래그. 새 플래그는 **뒤에만 덧붙인다** — 값이 바뀌면 리플레이가 깨진다.
namespace EntityFlag {
constexpr uint8_t None   = 0;
constexpr uint8_t Ranged = 1u << 0;   // 원거리 몹 (§2 구간 구성의 melee/ranged)
constexpr uint8_t Groggy = 1u << 1;   // CC 게이지 만충으로 행동 불가 (§9)
}  // namespace EntityFlag

// 스폰 인자. 인자 12개짜리 함수를 만들지 않으려는 목적도 있지만,
// 더 중요한 건 **스폰 시점에 확정되는 값이 무엇인지**를 한곳에 모으는 것이다.
// 특히 maxHp는 스폰 시점의 구간으로 고정된다(§2) — 나중에 조회하지 않는다.
struct SpawnDesc {
    Fixed     posX{};
    Fixed     posY{};
    Fixed     maxHp{};
    Fixed     armor{};
    Fixed     attackDamage{};
    Fixed     approachSpeed{};
    Fixed     attackRange{};
    int32_t   attackCooldownTicks = 0;
    int32_t   windupTicks         = 0;
    int32_t   targetPriority      = 0;
    Fixed     ccGaugeMax{};
    uint16_t  typeId              = 0;
    Archetype archetype           = Archetype::Trash;
    uint8_t   flags               = EntityFlag::None;
};

class EntityStore {
public:
    static constexpr uint32_t CAPACITY = config::MAX_ENTITIES;

    // ---- SoA. 유효 범위는 항상 [0, count_) 이며 그 밖은 읽지 않는다 ----
    //
    // 배열을 필드별로 쪼개면 **뜨거운 필드와 차가운 필드가 자동으로 분리된다.**
    // 이동·타겟팅은 posX/posY만 훑고, 엘리트 패턴이나 CC 게이지 배열은
    // 캐시라인에 올라오지도 않는다. 그래서 "엘리트 전용 희소 풀"을 따로 두는
    // 안은 채택하지 않았다 — SoA가 이미 같은 효과를 내면서 코드가 단순하다.

    // 뜨거움 — 매 틱 전수 순회
    Fixed     posX[CAPACITY]{};
    Fixed     posY[CAPACITY]{};
    Archetype archetype[CAPACITY]{};
    uint8_t   flags[CAPACITY]{};

    // 전투 — 사거리 안에서만
    Fixed     maxHp[CAPACITY]{};
    Fixed     damageTaken[CAPACITY]{};   // 현재 체력은 maxHp - damageTaken (§9)
    Fixed     armor[CAPACITY]{};
    Fixed     attackDamage[CAPACITY]{};
    Fixed     approachSpeed[CAPACITY]{};
    Fixed     attackRange[CAPACITY]{};     // 사거리. §9의 Range 스탯과 같은 개념이다
    // 타겟 선정 우선순위 (§3). **archetype이 아니라 몬스터 타입별 데이터다** —
    // 주술사는 QTE가 없지만 소환으로 물량을 불리므로 궁병대장보다 먼저여야 하고,
    // archetype 고정 순서로는 그 판단을 표현할 수 없다.
    int32_t   targetPriority[CAPACITY]{};
    int32_t   attackCooldown[CAPACITY]{};  // 남은 틱
    int32_t   windupLeft[CAPACITY]{};      // 남은 틱, 0이면 텔레그래프 중 아님
    int32_t   windupTicks[CAPACITY]{};     // 텔레그래프 길이(정적). 0이면 QTE 패턴 없음
    int32_t   groggyLeft[CAPACITY]{};      // QTE 완벽 판정 보상 — 행동 불가 남은 틱

    // 엘리트·보스 전용 — 대부분 0으로 남는다 (SoA라 비용이 없다)
    uint8_t   patternIndex[CAPACITY]{};
    int32_t   patternCooldown[CAPACITY]{};
    Fixed     ccGauge[CAPACITY]{};
    Fixed     ccGaugeMax[CAPACITY]{};
    int32_t   ccTriggerCount[CAPACITY]{};

    // 메타
    uint16_t  typeId[CAPACITY]{};
    int32_t   spawnTick[CAPACITY]{};
    uint64_t  rngState[CAPACITY]{};      // 엔티티별 난수 스트림 (§10)

    void init() {
        count_ = 0;
        deadPending_ = 0;
        freeCount_ = CAPACITY;
        for (uint32_t i = 0; i < CAPACITY; ++i) {
            slotGen_[i]     = 1;     // generation 0은 무효 약속
            slotToDense_[i] = -1;
            // 뒤에서 pop하므로 역순으로 채운다 → 첫 스폰이 슬롯 0을 받는다.
            // 결정론에 필수는 아니지만 로그·덤프를 읽을 때 차이가 크다.
            freeSlots_[i]   = CAPACITY - 1 - i;
        }
    }

    uint32_t count() const       { return count_; }        // 죽음 표시된 행 포함
    uint32_t deadPending() const { return deadPending_; }
    uint32_t freeSlots() const   { return freeCount_; }
    bool     full() const        { return freeCount_ == 0; }

    // 실패 시 invalid를 돌려준다. **호출부가 반드시 확인해야 한다** —
    // 예외를 던지지 않는 이유는 C ABI 경계를 넘을 수 없기 때문이다(CLAUDE.md).
    EntityId spawn(const SpawnDesc& d, int32_t tick, uint64_t masterSeed) {
        if (freeCount_ == 0) return EntityId::invalid();

        const uint32_t slot  = freeSlots_[--freeCount_];
        const uint32_t dense = count_++;
        const EntityId id    = EntityId::make(slot, slotGen_[slot]);

        slotToDense_[slot] = static_cast<int32_t>(dense);
        denseId_[dense]    = id;
        denseDead_[dense]  = 0;

        posX[dense]            = d.posX;
        posY[dense]            = d.posY;
        archetype[dense]       = d.archetype;
        flags[dense]           = d.flags;
        maxHp[dense]           = d.maxHp;
        damageTaken[dense]     = Fixed{};
        armor[dense]           = d.armor;
        attackDamage[dense]    = d.attackDamage;
        approachSpeed[dense]   = d.approachSpeed;
        attackRange[dense]     = d.attackRange;
        targetPriority[dense]  = d.targetPriority;
        attackCooldown[dense]  = d.attackCooldownTicks;
        windupLeft[dense]      = 0;
        windupTicks[dense]     = d.windupTicks;
        groggyLeft[dense]      = 0;
        patternIndex[dense]    = 0;
        patternCooldown[dense] = 0;
        ccGauge[dense]         = Fixed{};
        ccGaugeMax[dense]      = d.ccGaugeMax;
        ccTriggerCount[dense]  = 0;
        typeId[dense]          = d.typeId;
        spawnTick[dense]       = tick;
        rngState[dense]        = Rng::deriveEntity(masterSeed, id.bits).state();
        return id;
    }

    // 살아있고 참조가 유효한가. **stale 핸들은 여기서 걸러진다.**
    bool alive(EntityId id) const {
        const int32_t d = denseOf(id);
        return d >= 0 && denseDead_[d] == 0;
    }

    // dense 인덱스. 무효하거나 stale이면 -1.
    int32_t denseOf(EntityId id) const {
        if (!id.valid()) return -1;
        const uint32_t slot = id.index();
        if (slot >= CAPACITY) return -1;
        if (slotGen_[slot] != id.generation()) return -1;   // 슬롯 재사용 → 옛 ID 거부
        return slotToDense_[slot];
    }

    EntityId idAt(uint32_t dense) const { return denseId_[dense]; }
    bool     deadAt(uint32_t dense) const { return denseDead_[dense] != 0; }

    // **틱 중간에는 표시만 한다** (CLAUDE.md). 실제 제거는 compact()가 한다.
    // 중간에 지우면 같은 틱의 다른 시스템이 보는 배열이 시스템마다 달라지고,
    // 그러면 "시스템 실행 순서 고정"이 지켜져도 결과가 흔들린다.
    bool markDead(EntityId id) {
        const int32_t d = denseOf(id);
        if (d < 0 || denseDead_[d] != 0) return false;
        denseDead_[d] = 1;
        ++deadPending_;
        return true;
    }

    // 틱 종료 시 일괄 압축. 제거된 수를 돌려준다.
    //
    // **압축 방식: 안정 압축(생존자를 앞으로 당기는 1패스). swap-remove가 아니다.**
    //   - swap-remove는 O(제거 수)로 더 싸지만 배열 순서가 **삭제 순서에 의존**한다.
    //     결과 자체는 결정적이지만, 체크섬이 갈렸을 때 "레이아웃이 달라서인가
    //     상태가 달라서인가"를 매번 되짚어야 한다
    //   - 안정 압축은 배열 순서가 **스폰 순서만의 함수**가 된다. 삭제가 언제
    //     일어났든 같은 스폰 시퀀스면 같은 레이아웃이다. 디버깅 비용 차이가 크다
    //   - 비용은 O(count_) 1패스. N≤512에 필드당 4~8바이트라 한 틱에 수 μs 수준이고,
    //     애초에 §11이 "시뮬은 병목이 아니다"라고 판단한 구간이다
    //   - 제거가 없는 틱은 아래 조기 반환으로 0원
    uint32_t compact() {
        if (deadPending_ == 0) return 0;

        uint32_t w = 0;
        for (uint32_t r = 0; r < count_; ++r) {
            if (denseDead_[r] != 0) {
                const uint32_t slot = denseId_[r].index();
                slotGen_[slot]     = EntityId::nextGeneration(slotGen_[slot]);  // 옛 ID 무효화
                slotToDense_[slot] = -1;
                freeSlots_[freeCount_++] = slot;
                continue;
            }
            if (w != r) moveRow(w, r);
            slotToDense_[denseId_[w].index()] = static_cast<int32_t>(w);
            ++w;
        }

        const uint32_t removed = count_ - w;
        count_ = w;
        deadPending_ = 0;
        return removed;
    }

    // 체크섬 입력. **여기에 빠진 필드는 곧 거짓 음성이다** — 결과는 갈렸는데
    // 해시는 같은 상태가 된다. 필드를 추가하면 여기도 반드시 추가할 것.
    //
    // 행 단위가 아니라 **배열 단위로 훑는다.** 결정론은 어느 쪽이든 같지만,
    // 배열 단위는 19개 배열을 순차 스윕하고 행 단위는 19개 스트림을 교차한다.
    void hashInto(Hasher& h) const {
        h.feed(count_);
        h.feed(deadPending_);

        const uint32_t n = count_;
        for (uint32_t i = 0; i < n; ++i) h.feed(denseId_[i]);
        for (uint32_t i = 0; i < n; ++i) h.feed(denseDead_[i]);
        for (uint32_t i = 0; i < n; ++i) h.feed(posX[i]);
        for (uint32_t i = 0; i < n; ++i) h.feed(posY[i]);
        for (uint32_t i = 0; i < n; ++i) h.feed(static_cast<uint8_t>(archetype[i]));
        for (uint32_t i = 0; i < n; ++i) h.feed(flags[i]);
        for (uint32_t i = 0; i < n; ++i) h.feed(maxHp[i]);
        for (uint32_t i = 0; i < n; ++i) h.feed(damageTaken[i]);
        for (uint32_t i = 0; i < n; ++i) h.feed(armor[i]);
        for (uint32_t i = 0; i < n; ++i) h.feed(attackDamage[i]);
        for (uint32_t i = 0; i < n; ++i) h.feed(approachSpeed[i]);
        for (uint32_t i = 0; i < n; ++i) h.feed(attackRange[i]);
        for (uint32_t i = 0; i < n; ++i) h.feed(targetPriority[i]);
        for (uint32_t i = 0; i < n; ++i) h.feed(attackCooldown[i]);
        for (uint32_t i = 0; i < n; ++i) h.feed(windupLeft[i]);
        for (uint32_t i = 0; i < n; ++i) h.feed(windupTicks[i]);
        for (uint32_t i = 0; i < n; ++i) h.feed(groggyLeft[i]);
        for (uint32_t i = 0; i < n; ++i) h.feed(patternIndex[i]);
        for (uint32_t i = 0; i < n; ++i) h.feed(patternCooldown[i]);
        for (uint32_t i = 0; i < n; ++i) h.feed(ccGauge[i]);
        for (uint32_t i = 0; i < n; ++i) h.feed(ccGaugeMax[i]);
        for (uint32_t i = 0; i < n; ++i) h.feed(ccTriggerCount[i]);
        for (uint32_t i = 0; i < n; ++i) h.feed(typeId[i]);
        for (uint32_t i = 0; i < n; ++i) h.feed(spawnTick[i]);
        for (uint32_t i = 0; i < n; ++i) h.feed(rngState[i]);

        // **자유 슬롯 목록은 상태다.** 다음 스폰이 어떤 EntityId를 받는지를
        // 결정하므로, 두 런의 자유 목록이 어긋나면 다음 스폰부터 갈린다.
        //
        // 슬롯 번호와 세대를 따로 넣지 않고 `EntityId::make(slot, gen)`로 묶는다.
        // 워드가 절반이고, 의미도 정확히 **"이 슬롯에서 다음에 나올 ID"** 다.
        //
        // slotToDense_는 [파생]이라 넣지 않는다 — denseId_와 count_에서 복원된다.
        h.feed(freeCount_);
        for (uint32_t i = 0; i < freeCount_; ++i) {
            const uint32_t slot = freeSlots_[i];
            h.feed(EntityId::make(slot, slotGen_[slot]));
        }
    }

private:
    void moveRow(uint32_t dst, uint32_t src) {
        posX[dst]            = posX[src];
        posY[dst]            = posY[src];
        archetype[dst]       = archetype[src];
        flags[dst]           = flags[src];
        maxHp[dst]           = maxHp[src];
        damageTaken[dst]     = damageTaken[src];
        armor[dst]           = armor[src];
        attackDamage[dst]    = attackDamage[src];
        approachSpeed[dst]   = approachSpeed[src];
        attackRange[dst]     = attackRange[src];
        targetPriority[dst]  = targetPriority[src];
        attackCooldown[dst]  = attackCooldown[src];
        windupLeft[dst]      = windupLeft[src];
        windupTicks[dst]     = windupTicks[src];
        groggyLeft[dst]      = groggyLeft[src];
        patternIndex[dst]    = patternIndex[src];
        patternCooldown[dst] = patternCooldown[src];
        ccGauge[dst]         = ccGauge[src];
        ccGaugeMax[dst]      = ccGaugeMax[src];
        ccTriggerCount[dst]  = ccTriggerCount[src];
        typeId[dst]          = typeId[src];
        spawnTick[dst]       = spawnTick[src];
        rngState[dst]        = rngState[src];
        denseId_[dst]        = denseId_[src];
        denseDead_[dst]      = denseDead_[src];
    }

    EntityId denseId_[CAPACITY]{};      // dense → EntityId (압축 시 함께 이동)
    uint8_t  denseDead_[CAPACITY]{};
    int32_t  slotToDense_[CAPACITY]{};  // slot → dense, 미사용은 -1
    uint32_t slotGen_[CAPACITY]{};
    uint32_t freeSlots_[CAPACITY]{};

    uint32_t count_       = 0;
    uint32_t deadPending_ = 0;
    uint32_t freeCount_   = 0;
};

}  // namespace dc

#endif  // DC_ENTITY_STORE_H
