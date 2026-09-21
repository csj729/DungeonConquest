// 전투 시스템 (§3·§9).
//
// 영웅 1기 대 몹 N마리라 계산의 비대칭이 극단적이다(§11). 전부 브루트포스이고,
// 유일하게 공간 분할이 필요한 적 간 충돌은 아직 구현 대상이 아니다.
#ifndef DC_COMBAT_H
#define DC_COMBAT_H

#include <cstdint>

#include "sim_config.h"
#include "spawn.h"
#include "targeting.h"
#include "world.h"

namespace dc {

// 방어력 감쇠 — **감산이 아니라 비율** (§9).
// 아이템 조합으로 위력이 곱해지는 게임이라 감산 방식은 후반에 방어력을 무의미하게
// 만든다. 비율은 배율 성장과 무관하게 일정 비율을 유지한다.
// **전 계산을 int64에서 한다.** `damage * Fixed(k) / (Fixed(k) + armor)`를 Fixed
// 연산자로 그대로 쓰면 중간 곱이 넘친다 — damage 10000, k 100에서
// raw 40,960,000 × 409,600 >> 12 = 4.1e9 으로 int32(2.1e9)를 넘는다.
// 20.12의 **표현 범위**(±524,288)는 충분한데 **중간값**이 넘치는 전형적인 자리다.
inline Fixed mitigate(Fixed damage, Fixed armor, int32_t k) {
    if (k <= 0) return damage;
    const int64_t a     = armor.raw < 0 ? 0 : armor.raw;   // 방깎이 음수로 내려가지 않게
    const int64_t kk    = static_cast<int64_t>(k) * Fixed::ONE_RAW;
    const int64_t denom = kk + a;
    if (denom <= 0) return damage;
    const int64_t v = (static_cast<int64_t>(damage.raw) * kk) / denom;
    return Fixed::fromRaw(static_cast<int32_t>(v));
}

// 영웅 공격 간격(틱). AttackSpeed는 초당 공격 횟수다.
inline int32_t heroAttackInterval(const World& w, const SimConfig& cfg) {
    const Fixed speed = w.hero.stats.value(Stat::AttackSpeed);
    if (speed.raw <= 0 || cfg.tickHz <= 0) return 1;
    const int32_t t = toInt(Fixed(cfg.tickHz) / speed);
    return t < 1 ? 1 : t;
}

// Fixed 확률(0~1) → q16. raw가 값×4096이므로 ×16이면 값×65536이다.
inline uint32_t chanceToQ16(Fixed p) {
    if (p.raw <= 0) return 0;
    const int64_t q = static_cast<int64_t>(p.raw) * 16;
    return q >= Q16_ONE ? Q16_ONE : static_cast<uint32_t>(q);
}

inline void combatRun(World& w, const SimConfig& cfg) {
    const uint32_t n = w.entities.count();

    // ── 영웅 → 적 ──
    if (w.hero.attackCooldown > 0) --w.hero.attackCooldown;
    const int32_t td = w.entities.denseOf(w.hero.target);
    if (td >= 0 && !w.entities.deadAt(static_cast<uint32_t>(td)) && w.hero.attackCooldown <= 0) {
        const uint32_t i = static_cast<uint32_t>(td);
        const Fixed range = w.hero.stats.value(Stat::Range);
        const int64_t r2 = static_cast<int64_t>(range.raw) * static_cast<int64_t>(range.raw);
        if (distanceSq(w.entities.posX[i], w.entities.posY[i], w.hero.posX, w.hero.posY) <= r2) {
            Fixed dmg = w.hero.stats.value(Stat::AttackPower);
            if (w.rngCombat.chanceQ16(chanceToQ16(w.hero.stats.value(Stat::CritChance)))) {
                dmg = dmg * w.hero.stats.value(Stat::CritMult);
            }
            const Fixed applied = mitigate(dmg, w.entities.armor[i], cfg.armorK);
            w.entities.damageTaken[i] += applied;

            // 통합 proc 판정은 기본 공격당 한 번만 (§3). 어떤 스킬인지는
            // 발동이 확정된 뒤 가중 추첨으로 고른다 — 그건 §14-9 QTE 단계다.
            (void)w.hero.proc.roll(w.rngCombat, cfg.procPrdCQ16);

            if (w.entities.damageTaken[i].raw >= w.entities.maxHp[i].raw) {
                if (w.entities.markDead(w.entities.idAt(i))) {
                    if (w.entities.archetype[i] == Archetype::Trash) {
                        ++w.run.killedTrash;
                        w.run.clearPoints += cfg.trashPoints;
                    } else {
                        ++w.run.killedElite;
                        w.run.clearPoints += cfg.elitePoints;
                    }
                }
            }
            w.hero.attackCooldown = heroAttackInterval(w, cfg);
        }
    }

    // ── 적 → 영웅 ──
    // 추격 → 사거리 진입 → 공격 → 쿨다운 (§3). 일반 몹은 예비 동작이 없다.
    const Fixed heroArmor = w.hero.stats.value(Stat::Armor);
    Fixed incoming{};
    for (uint32_t i = 0; i < n; ++i) {
        if (w.entities.deadAt(i)) continue;

        const Fixed dx = w.hero.posX - w.entities.posX[i];
        const Fixed dy = w.hero.posY - w.entities.posY[i];
        const int64_t d2 = static_cast<int64_t>(dx.raw) * dx.raw + static_cast<int64_t>(dy.raw) * dy.raw;
        const int64_t r2 = static_cast<int64_t>(w.entities.attackRange[i].raw)
                         * static_cast<int64_t>(w.entities.attackRange[i].raw);

        if (d2 > r2) {
            // 추격. 방향 정규화에 정수 제곱근을 쓴다 — libm sqrt는 구현마다 값이
            // 갈리므로(docs/determinism.md §4) 쓸 수 없다.
            const Fixed len = fixedLength(dx, dy);
            if (len.raw > 0 && cfg.tickHz > 0) {
                const Fixed step = w.entities.approachSpeed[i] / cfg.tickHz;
                w.entities.posX[i] += dx * step / len;
                w.entities.posY[i] += dy * step / len;
            }
            continue;
        }

        if (w.entities.windupLeft[i] > 0) { --w.entities.windupLeft[i]; continue; }
        if (w.entities.attackCooldown[i] > 0) { --w.entities.attackCooldown[i]; continue; }

        incoming += mitigate(w.entities.attackDamage[i], heroArmor, cfg.armorK);
        w.entities.attackCooldown[i] = cfg.trash.cooldownTicks;
    }
    if (incoming.raw != 0) {
        w.hero.corruption += incoming;
        w.notifyCorruptionChanged();
    }

    // ── 잠식 물량 충전 (§2) ──
    // 피격량과 별개로, 전장의 몹 수가 임계를 넘으면 초당 일정량이 찬다.
    // **이게 "몬스터 수 상한 = 게임오버"를 게이지 하나로 흡수한 장치다.**
    if (cfg.tickHz > 0 && cfg.corruptionThreshold > 0) {
        const int32_t alive = static_cast<int32_t>(aliveCount(w.entities));
        if (alive > cfg.corruptionThreshold) {
            Fixed perSec = Fixed::fromPermille(cfg.corruptionPerMobPermille)
                         * (alive - cfg.corruptionThreshold);
            const int32_t cap = cfg.capFor(w.run.globalSegment(cfg.segmentsPerMap) + 1);
            if (cap > 0 && alive >= cap) {
                perSec = perSec * Fixed::fromPermille(cfg.corruptionOverflowMultPermille);
            }
            w.hero.corruption += perSec / cfg.tickHz;
            w.notifyCorruptionChanged();
        }
    }
}

}  // namespace dc

#endif  // DC_COMBAT_H
