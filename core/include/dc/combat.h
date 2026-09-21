// 전투 시스템 (§3·§9).
//
// 영웅 1기 대 몹 N마리라 계산의 비대칭이 극단적이다(§11). 전부 브루트포스이고,
// 유일하게 공간 분할이 필요한 적 간 충돌은 아직 구현 대상이 아니다.
#ifndef DC_COMBAT_H
#define DC_COMBAT_H

#include <cstdint>

#include "movement.h"
#include "qte.h"
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

// 이동 (§3) — 영웅은 타겟을 향해, 몹은 영웅을 향해. **양쪽 다 추격뿐이다.**
// 사거리 판정보다 먼저 돌고, 그 뒤에 분리가 겹침을 푼다.
inline void movementRun(World& w, const SimConfig& cfg) {
    if (cfg.tickHz <= 0) return;

    // 영웅 → 타겟
    const int32_t td = w.entities.denseOf(w.hero.target);
    if (td >= 0 && !w.entities.deadAt(static_cast<uint32_t>(td))) {
        const uint32_t i = static_cast<uint32_t>(td);
        const Fixed speed = w.hero.stats.value(Stat::MoveSpeed);
        if (speed.raw > 0) {
            Fixed dirX{}, dirY{};
            const Fixed range = w.hero.stats.value(Stat::Range);
            if (stepToward(&w.hero.posX, &w.hero.posY,
                           w.entities.posX[i], w.entities.posY[i],
                           speed / cfg.tickHz, range, &dirX, &dirY)) {
                // 바라보는 방향. 멈춰 있을 때도 유지되므로 [상태]다 (연출이 읽는다).
                w.hero.facingX = dirX;
                w.hero.facingY = dirY;
            }
        }
    }

    // 몹 → 영웅
    const uint32_t n = w.entities.count();
    for (uint32_t i = 0; i < n; ++i) {
        if (w.entities.deadAt(i)) continue;
        if (w.entities.approachSpeed[i].raw <= 0) continue;
        (void)stepToward(&w.entities.posX[i], &w.entities.posY[i],
                         w.hero.posX, w.hero.posY,
                         w.entities.approachSpeed[i] / cfg.tickHz,
                         w.entities.attackRange[i], nullptr, nullptr);
    }
}

// 스킬 증폭 창. **쿨다운이 남아 있으면 열지 않는다** — 빈도 상한을 proc 확률이
// 아니라 쿨다운으로 잡는 이유는 §7의 PRD 손익분기를 건드리지 않기 위해서다(§3).
inline void openSkillQte(World& w, const SimConfig& cfg, uint32_t skillIndex) {
    if (w.hero.qte.open() || w.hero.qteCooldown > 0) return;
    const int32_t len = cfg.qtePerfectWindowTicks * 4 + 4;   // 완벽 구간을 품는 창
    QteWindow q;
    q.kind        = QteKind::SkillAmplify;
    q.skillIndex  = static_cast<uint16_t>(skillIndex);
    q.openTick    = w.tickCount();
    q.closeTick   = w.tickCount() + len;
    q.perfectTo   = q.closeTick - 1;                       // 입력 가능한 마지막 틱
    q.perfectFrom = q.perfectTo - cfg.qtePerfectWindowTicks;
    w.hero.qte        = q;
    w.hero.qteCooldown = cfg.qteCooldownTicks;
}

// 위기 회피 창. **쿨다운을 무시하고 열되 소모는 한다.**
// 창 길이가 곧 텔레그래프 길이이고, 완벽 구간은 타격 직전이다.
inline void openCrisisQte(World& w, const SimConfig& cfg, EntityId src, int32_t windup) {
    QteWindow q;
    q.kind        = QteKind::CrisisEvade;
    q.source      = src;
    q.openTick    = w.tickCount();
    q.closeTick   = w.tickCount() + windup;
    q.perfectTo   = q.closeTick - 1;
    q.perfectFrom = q.perfectTo - cfg.qtePerfectWindowTicks;
    w.hero.qte         = q;
    w.hero.qteCooldown = cfg.qteCooldownTicks;
}

// 피해 적용 + 처치 판정. 영웅 기본 공격과 스킬이 같은 경로를 쓴다.
inline void applySkillHit(World& w, const SimConfig& cfg, uint32_t i, Fixed rawDamage) {
    if (w.entities.deadAt(i)) return;
    w.entities.damageTaken[i] += mitigate(rawDamage, w.entities.armor[i], cfg.armorK);
    if (w.entities.damageTaken[i].raw < w.entities.maxHp[i].raw) return;
    if (!w.entities.markDead(w.entities.idAt(i))) return;
    if (w.entities.archetype[i] == Archetype::Trash) {
        ++w.run.killedTrash;
        w.run.clearPoints += cfg.trashPoints;
    } else {
        ++w.run.killedElite;
        w.run.clearPoints += cfg.elitePoints;
    }
}

// 등급별 위력 배율. 실패는 **페널티가 아니라 보너스 없음**이다 (§3).
inline Fixed qteAmplify(const SimConfig& cfg, QteGrade g) {
    switch (g) {
        case QteGrade::Perfect: return cfg.qtePerfectMult;
        case QteGrade::Success: return cfg.qteSuccessMult;
        case QteGrade::Miss:    break;
    }
    return Fixed::one();
}

// 창을 닫고 결과를 적용한다. **closeTick에 입력이 없으면 Miss로 자동 해결**된다 —
// 판정을 미루면 그 틱의 다른 계산이 무엇을 봐야 할지가 모호해진다.
inline void qteRun(World& w, const SimConfig& cfg) {
    if (w.hero.qteCooldown > 0) --w.hero.qteCooldown;
    if (!w.hero.qte.open()) return;
    if (w.tickCount() < w.hero.qte.closeTick) return;

    const QteWindow q = w.hero.qte;
    const QteGrade  g = q.hasInput ? q.input : QteGrade::Miss;
    w.hero.qte.reset();

    if (q.kind == QteKind::SkillAmplify) {
        if (q.skillIndex >= cfg.skillCount) return;
        const SkillConfig& sk = cfg.skills[q.skillIndex];
        const Fixed dmg = w.hero.stats.value(Stat::AttackPower)
                        * sk.mult * qteAmplify(cfg, g);
        if (sk.aoe) {
            // 광역기는 우선순위가 없다 — 범위 안의 모든 적을 때린다 (§3).
            uint32_t hit[config::MAX_ENTITIES];
            const uint32_t n = collectInRadius(w.entities, w.hero.posX, w.hero.posY,
                                               cfg.aoeRadius, hit, config::MAX_ENTITIES);
            for (uint32_t k = 0; k < n; ++k) applySkillHit(w, cfg, hit[k], dmg);
        } else {
            const int32_t d = w.entities.denseOf(w.hero.target);
            if (d >= 0) applySkillHit(w, cfg, static_cast<uint32_t>(d), dmg);
        }
        return;
    }

    // ── 위기 회피 ──
    // **QTE가 회피 판정 그 자체다** — 명중 굴림이 따로 없고, 실패하면 무조건 맞는다.
    const int32_t d = w.entities.denseOf(q.source);
    if (d < 0) return;                       // 텔레그래프 도중에 죽었다
    const uint32_t i = static_cast<uint32_t>(d);
    w.entities.attackCooldown[i] = cfg.trash.cooldownTicks;

    if (g == QteGrade::Miss) {
        w.hero.corruption += mitigate(w.entities.attackDamage[i],
                                      w.hero.stats.value(Stat::Armor), cfg.armorK);
        w.notifyCorruptionChanged();
        return;
    }
    if (g != QteGrade::Perfect) return;      // 성공은 무피해까지

    // 완벽 — CC 게이지를 채운다. **피해가 아니라 게이지로 주는 이유**는 보스의
    // 실효 체력이 커서 반격 피해로는 체감이 없고, 게이지는 CC 스탯 없는 빌드에도
    // 보스를 무력화할 경로를 열어주기 때문이다 (§3).
    const Fixed gain = w.entities.ccGaugeMax[i]
                     * Fixed::fromPermille(cfg.qtePerfectCcGainPermille);
    w.entities.ccGauge[i] += gain;
    if (w.entities.ccGaugeMax[i].raw > 0
        && w.entities.ccGauge[i].raw >= w.entities.ccGaugeMax[i].raw) {
        w.entities.ccGauge[i] = Fixed{};
        ++w.entities.ccTriggerCount[i];
        w.entities.groggyLeft[i] = cfg.groggyTicks;
        w.entities.flags[i] = static_cast<uint8_t>(w.entities.flags[i] | EntityFlag::Groggy);
    }
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

            // 통합 proc 판정은 기본 공격당 한 번만 (§3). 발동이 확정되면
            // 어떤 스킬인지는 가중 추첨으로 고른다 — 스킬마다 독립 확률을 굴리면
            // PRD 손익분기가 체감선 밖으로 밀린다(§7).
            if (w.hero.proc.roll(w.rngCombat, cfg.procPrdCQ16) && cfg.skillCount > 0) {
                int32_t weights[MAX_SKILLS];
                for (uint32_t k = 0; k < cfg.skillCount; ++k) weights[k] = cfg.skills[k].weight;
                const uint32_t pick = w.rngCombat.weighted(weights, cfg.skillCount);
                openSkillQte(w, cfg, pick);
            }

            applySkillHit(w, cfg, i, dmg);
            w.hero.attackCooldown = heroAttackInterval(w, cfg);
        }
    }

    // ── 적 → 영웅 ──
    // 추격 → 사거리 진입 → 공격 → 쿨다운 (§3). 일반 몹은 예비 동작이 없다.
    const Fixed heroArmor = w.hero.stats.value(Stat::Armor);
    Fixed incoming{};
    for (uint32_t i = 0; i < n; ++i) {
        if (w.entities.deadAt(i)) continue;

        // 이동은 movementRun이 이미 했다. 여기서는 사거리 안인지만 본다.
        const Fixed dx = w.hero.posX - w.entities.posX[i];
        const Fixed dy = w.hero.posY - w.entities.posY[i];
        const int64_t d2 = static_cast<int64_t>(dx.raw) * dx.raw + static_cast<int64_t>(dy.raw) * dy.raw;
        const int64_t r2 = static_cast<int64_t>(w.entities.attackRange[i].raw)
                         * static_cast<int64_t>(w.entities.attackRange[i].raw);
        if (d2 > r2) continue;

        // 그로기 — QTE 완벽 판정의 보상. 행동 불가다 (§3).
        if (w.entities.groggyLeft[i] > 0) { --w.entities.groggyLeft[i]; continue; }
        // 텔레그래프 진행 중이면 QTE 창이 열려 있다. 해결은 qteRun이 한다.
        if (w.entities.windupLeft[i] > 0) { --w.entities.windupLeft[i]; continue; }
        if (w.entities.attackCooldown[i] > 0) { --w.entities.attackCooldown[i]; continue; }

        // **QTE는 표시된 패턴에만 걸린다** (§3). 일반 몹은 windupTicks가 0이라
        // 예비 동작 없이 즉시 들어가고 자동 판정된다.
        if (w.entities.windupTicks[i] > 0 && !w.hero.qte.open()) {
            w.entities.windupLeft[i] = w.entities.windupTicks[i];
            openCrisisQte(w, cfg, w.entities.idAt(i), w.entities.windupTicks[i]);
            continue;
        }

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
