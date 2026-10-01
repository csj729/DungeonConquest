// 전투 시스템 (§3·§9).
//
// 영웅 1기 대 몹 N마리라 계산의 비대칭이 극단적이다(§11). 전부 브루트포스이고,
// 유일하게 공간 분할이 필요한 적 간 충돌은 아직 구현 대상이 아니다.
#ifndef DC_COMBAT_H
#define DC_COMBAT_H

#include <cstdint>

#include "levelup.h"
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

// 추격 정지 거리 — **사거리에서 여유만큼 안쪽이다.**
//
// 경계에 정확히 멈추면 `stepToward`의 정수 절삭(1 raw = 1/4096타일)과 분리 밀림이
// 거리를 사거리 밖으로 밀어내, 전투 판정이 매 틱 빗나간다. 실측: 엘리트 사거리를
// 영웅 사거리보다 크게 잡자 둘 다 3.00타일에 마주 선 채 300초간 처치 0이었다.
inline Fixed approachStop(Fixed attackRange, const SimConfig& cfg) {
    const Fixed margin = Fixed::fromPermille(cfg.approachMarginMilli);
    const Fixed stop = attackRange - margin;
    return stop.raw > 0 ? stop : Fixed{};
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
// 서리 오라는 이동에서 쓰이므로 선언을 앞에 둔다 (정의는 유물 절에).
inline Fixed frostMult(const World& w, const SimConfig& cfg, uint32_t i);

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
                           speed / cfg.tickHz, approachStop(range, cfg), &dirX, &dirY)) {
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
                         (w.entities.approachSpeed[i] * frostMult(w, cfg, i)) / cfg.tickHz,
                         approachStop(w.entities.attackRange[i], cfg), nullptr, nullptr);
    }
}

// 스킬 증폭 창.
//
// **쿨다운은 QTE 빈도를 제한하지 스킬을 봉인하지 않는다.** §3이 "상한은 QTE 자체
// 쿨다운으로 잡는다 — proc 확률로 조절하지 않는다"고 한 것은 *QTE가 몇 번 뜨는가*에
// 대한 규칙이다. 발동한 스킬은 창이 못 열려도 **기본 위력으로 즉시 나간다.**
//
// 이걸 혼동해 스킬까지 막았더니 몬테카를로에서 클리어율이 0%가 나왔다 —
// 처치율이 0.92 → 0.55마리/초로 떨어져 게이지가 11%밖에 안 찼다.
// 창을 못 열면 false를 돌려주고, 호출부가 즉시 실행한다.
inline bool openSkillQte(World& w, const SimConfig& cfg, uint32_t skillIndex) {
    if (w.hero.qte.open() || w.hero.qteCooldown > 0) return false;
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
    return true;
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
// ── 각인 효과 (§4 공통 각인) ─────────────────────────────────────
//
// **전부 `w.cards.engrave[]`를 읽는다.** 카드가 수치를 누적해 두고 여기서 한 번만
// 해석하므로, 새 각인을 붙여도 카드 쪽은 건드릴 게 없다.
//
// 수치형(E_CRIT·E_REND·E_WIDE·E_SWARM)은 피해 계산의 입력을 바꾼다.
// 훅형(E_PIERCE·E_CHAIN·E_DECAY·E_LEECH)은 타격 전후에 일을 더 한다.

inline Fixed engraveValue(const World& w, EngraveId e) {
    return w.cards.engrave[engraveIndex(e)];
}

// E_REND(파쇄) — 대상 Armor의 일정 비율을 무시한다.
// **방어 감쇠가 비율식이라 고방어 적에게 특히 크다** — 방패병(Armor 200)에서
// 25% 무시는 실효 HP를 300 → 250으로 17% 깎는다.
inline Fixed rendArmor(const World& w, Fixed armor) {
    const Fixed rend = engraveValue(w, EngraveId::Rend);
    if (rend.raw <= 0 || armor.raw <= 0) return armor;
    Fixed left = Fixed::one() - rend;
    if (left.raw < 0) left = Fixed{};          // 100% 초과 무시는 0으로 클램프
    return armor * left;
}

// E_SWARM(군집) — 주변 적 1명당 피해 증가, 상한까지.
// **물량이 곧 화력이 되는 유일한 축이다.** 전장이 고일수록 강해지므로
// 잠식이 위험해지는 구간과 반격이 세지는 구간이 맞물린다.
inline Fixed swarmMult(const World& w, const SimConfig& cfg) {
    const Fixed per = engraveValue(w, EngraveId::Swarm);
    if (per.raw <= 0 || cfg.swarmRadius.raw <= 0) return Fixed::one();
    const int64_t r2 = static_cast<int64_t>(cfg.swarmRadius.raw)
                     * static_cast<int64_t>(cfg.swarmRadius.raw);
    int32_t n = 0;
    const uint32_t cnt = w.entities.count();
    for (uint32_t i = 0; i < cnt; ++i) {
        if (w.entities.deadAt(i)) continue;
        if (distanceSq(w.entities.posX[i], w.entities.posY[i], w.hero.posX, w.hero.posY) > r2) continue;
        if (++n >= cfg.swarmMaxStacks) break;   // 상한에 닿으면 더 셀 이유가 없다
    }
    return Fixed::one() + per * n;
}

// E_CRIT(예리함) — 치확·치피를 얹는다. 치피는 치확의 2배로 들어간다.
//
// **치확 100% 초과분은 치피로 넘긴다** (§0 초과분 전환). 영웅 등급을 두 장 쌓아도
// 죽은 수치가 생기지 않아야 하고, 그게 이 각인이 빌드 종속으로 기능하는 조건이다.
inline void critWithEngrave(const World& w, Fixed* chance, Fixed* mult) {
    const Fixed e = engraveValue(w, EngraveId::Crit);
    if (e.raw <= 0) return;
    *chance += e;
    *mult   += e * Fixed(2);
    if (chance->raw > Fixed::one().raw) {
        *mult  += (*chance - Fixed::one());     // 초과분을 치피로 전환
        *chance = Fixed::one();
    }
}

// E_WIDE(확장) — 광역 반경을 늘린다. 단일기의 여파는 executeSkill이 따로 처리한다.
// 광역 반경 — **스탯에서 온다.** 전에는 SimConfig 고정값이라 아이템 광역 축이
// 붙을 자리가 없었다. E_WIDE(확장)는 그 위에 곱해진다.
inline Fixed aoeRadiusOf(const World& w) {
    return w.hero.stats.value(Stat::AoeRadius);
}
inline Fixed wideRadius(const World& w, Fixed base) {
    return base * (Fixed::one() + engraveValue(w, EngraveId::Wide));
}

// ── 유물 효과 (§4 유물) ──────────────────────────────────────────
//
// **각인이 스킬의 성질을 바꾼다면 유물은 전장에 규칙을 하나 더한다.**
// 그래서 스킬 경로가 아니라 틱 루프와 피해 계산 양쪽에 붙는다.

inline Fixed relicValue(const World& w, RelicId r) {
    return w.cards.relic[relicIndex(r)];
}

// R_RAGE(분노의 토템) + R_TIDE(밀물의 인장) — 영웅 공격력에 곱해지는 배율.
//
// 둘을 한 곳에서 접는 이유는 **StatBlock에 넣지 않기 위해서다.** 매 틱 변하는
// 값이라 모디파이어로 넣으면 추가/제거가 초당 20회씩 돌고, 그때마다 조건부
// 모디파이어 재평가가 따라붙는다. 피해 계산에서 한 번 곱하는 편이 싸고 명확하다.
inline Fixed heroPowerMult(const World& w, const SimConfig& cfg) {
    Fixed mult = Fixed::one();

    // 분노 — 피격 중첩. 중첩당 수치는 등급이 정하고 상한은 고정이다.
    const Fixed rage = relicValue(w, RelicId::Rage);
    if (rage.raw > 0 && w.hero.rageStacks > 0) mult += rage * w.hero.rageStacks;

    // 밀물 — 구간 경과 **초**에 비례하고 구간이 넘어가면 리셋된다.
    // 물량이 가장 쌓인 구간 후반에 가장 강하므로 위기와 반격이 맞물린다.
    const Fixed tide = relicValue(w, RelicId::Tide);
    if (tide.raw > 0 && cfg.tickHz > 0) {
        const int32_t sec = (w.tickCount() - w.run.segmentStartTick) / cfg.tickHz;
        if (sec > 0) mult += tide * sec;
    }
    return mult;
}

// R_BEACON(추적의 신호탄) — 엘리트·보스에게만 붙는 피해 증가.
// **물량 유물들과 정반대 축이다** — 잡몹에는 무용지물이라 "지금 무엇이 문제인가"에
// 따라 가치가 갈리고, 그래서 빌드 선택이 생긴다.
inline Fixed beaconMult(const World& w, Archetype a) {
    if (a == Archetype::Trash) return Fixed::one();
    return Fixed::one() + relicValue(w, RelicId::Beacon);
}

// R_FROST(서리 오라) — 반경 안의 적 이동속도를 깎는다. 이동 시점에 곱한다.
inline Fixed frostMult(const World& w, const SimConfig& cfg, uint32_t i) {
    // **R_FROST와 CC 축 아이템이 같은 경로를 쓴다.** 둔화가 두 군데에 따로 살면
    // 합산 규칙이 두 벌이 되고, 한쪽만 고치는 사고가 난다.
    //
    // CC를 새 스탯으로 두지 않은 이유가 여기 있다 — 둔화는 적을 늦출 뿐 더 빨리
    // 죽이지 않아 수익이 체감한다. 섞으면 클리어가 편해지지만 몰아도 이기지 못하므로
    // **메인 빌드가 되지 않는다** (§4 보너스 축).
    const Fixed frost = relicValue(w, RelicId::Frost) + Fixed::fromPermille(w.hero.slowAura);
    if (frost.raw <= 0 || cfg.frostRadius.raw <= 0) return Fixed::one();
    const int64_t r2 = static_cast<int64_t>(cfg.frostRadius.raw)
                     * static_cast<int64_t>(cfg.frostRadius.raw);
    if (distanceSq(w.entities.posX[i], w.entities.posY[i], w.hero.posX, w.hero.posY) > r2) {
        return Fixed::one();
    }
    Fixed left = Fixed::one() - frost;
    if (left.raw < 0) left = Fixed{};      // 100% 초과 둔화는 정지까지만
    return left;
}

// 관통은 스킬 경로에서도 쓰이므로 선언을 앞에 둔다 (정의는 아래).
inline void applyPierce(World& w, const SimConfig& cfg, uint32_t target, Fixed damage);
inline Fixed applyShockwave(World& w, const SimConfig& cfg, uint32_t target, Fixed damage);
inline Fixed applyAoeHits(World& w, const SimConfig& cfg, Fixed damage);

// E_DECAY(부식) — 타격한 적에게 도트를 건다.
// **갱신이지 중첩이 아니다.** 다시 맞으면 지속이 새로 시작되고 틱당 피해는 큰 쪽이
// 남는다. 중첩을 허용하면 연타(2타)·관통(다수)과 곱해져 각인 하나가 예산을 몇 배로
// 먹는다 — 문서가 "누적 대상은 피해 비율뿐"이라고 못박은 것과 같은 이유다.
inline void applyDecay(World& w, const SimConfig& cfg, uint32_t i) {
    const Fixed rate = w.cards.engrave[engraveIndex(EngraveId::Decay)];
    if (rate.raw <= 0 || cfg.decayTicks <= 0 || cfg.tickHz <= 0) return;
    // 초당 공격력의 rate → 틱당으로 나눈다
    const Fixed perTick = (w.hero.stats.value(Stat::AttackPower) * rate) / cfg.tickHz;
    if (perTick.raw <= 0) return;
    if (perTick.raw > w.entities.decayPerTick[i].raw) w.entities.decayPerTick[i] = perTick;
    w.entities.decayLeft[i] = cfg.decayTicks;
}

// **실제로 입힌 피해를 돌려준다** — `E_LEECH`가 그 값에 비례해 정화하기 때문이다.
// 굴리기 전 위력(rawDamage)이 아니라 방어 감쇠 후의 값이어야 방어력 높은 적에게
// 흡혈이 덜 붙는다.
// `hooks`가 false면 onHit 각인을 건너뛴다. **도트 자신은 도트를 갱신하면 안 된다** —
// decayRun이 매 틱 applyDecay를 다시 부르면 지속이 영원히 새로 시작되어 도트가
// 끝나지 않는다. 테스트가 이걸 잡았다.
// W_EXECUTE(처형) — 체력이 임계 이하면 즉시 처치한다. 고유 각인 3번.
//
// **비교는 정수 교차곱으로 한다.** `남은 체력 / 최대 체력 <= 임계/1000`을
// 나눗셈 없이 `남은 체력 × 1000 <= 최대 체력 × 임계`로 본다 — 나눗셈을 쓰면
// 절삭 방향이 경계에서 결과를 바꾸고, Fixed 나눗셈은 그 자리에서 정밀도를 잃는다.
// raw는 int32이므로 1000을 곱하기 전에 int64로 승격한다 (부호 있는 오버플로는 UB).
inline bool executeIfBelowThreshold(World& w, const SimConfig& cfg, uint32_t i) {
    if (cfg.executeThresholdPermille <= 0) return false;
    if (!w.cards.legendHas(legendIndexOf(LegendId::Execute))) return false;
    if ((w.entities.flags[i] & EntityFlag::ExecuteImmune) != 0) return false;

    const int64_t max  = w.entities.maxHp[i].raw;
    const int64_t left = max - static_cast<int64_t>(w.entities.damageTaken[i].raw);
    if (left <= 0) return false;                    // 이미 죽을 피해를 받았다
    if (left * 1000 > max * cfg.executeThresholdPermille) return false;

    w.entities.damageTaken[i] = w.entities.maxHp[i];
    return true;
}

inline Fixed applySkillHit(World& w, const SimConfig& cfg, uint32_t i, Fixed rawDamage,
                           bool hooks = true) {
    if (w.entities.deadAt(i)) return Fixed{};
    const Fixed dealt = mitigate(rawDamage * beaconMult(w, w.entities.archetype[i]),
                                 rendArmor(w, w.entities.armor[i]), cfg.armorK);
    w.entities.damageTaken[i] += dealt;

    // ── W_EXECUTE(처형) — 고유 각인 ──
    //
    // **남은 체력을 그대로 채워 죽음 경로로 보낸다.** 별도 처치 처리를 쓰면
    // 경험치·구슬·클리어 포인트가 두 군데에 적히고, 한쪽만 고치는 사고가 난다.
    //
    // `hooks`가 false인 호출(도트 · 보스 장판 · R_BOLT · RL_STORM)은 제외한다 —
    // 설계가 말한 범위는 **영웅의 타격**이고, 예산도 공속(1.54/s)으로 환산했다.
    // 틱형 피해까지 포함하면 판정 빈도가 올라 예산을 넘는다.
    if (hooks) executeIfBelowThreshold(w, cfg, i);

    if (w.entities.damageTaken[i].raw < w.entities.maxHp[i].raw) {
        if (hooks) applyDecay(w, cfg, i);   // 살아남은 적에게만 · 도트 자신은 제외
        return dealt;
    }
    if (!w.entities.markDead(w.entities.idAt(i))) return dealt;
    // 경험치는 실효 체력에 비례한다 (§4). Fixed가 아니라 정수 누적값이다 —
    // 고정소수점 범위 ±524,288을 훨씬 넘고 소수점이 필요 없다.
    {
        const int64_t ehp = static_cast<int64_t>(toInt(w.entities.maxHp[i]))
                          * (cfg.armorK + toInt(w.entities.armor[i])) / (cfg.armorK > 0 ? cfg.armorK : 1);
        gainExp(w, cfg, ehp * cfg.expPerEhpPermille / 1000);
    }
    // ── 회복 구슬 드랍 (§2) ──
    // **잡몹은 확률, 엘리트는 무조건이다.** 잡몹 쪽 확률은 기댓값을 유지하면서
    // 분산을 만들어 "운 좋게 버티는 순간"을 열고, 엘리트 쪽 확정은 10~25초를
    // 들인 위험에 확실한 보상을 준다 — 확률로 두면 체감이 도박이 된다.
    //
    // 드랍 위치는 **죽은 자리**다. 즉시 회복이 아니라서 영웅이 그 자리에 있어야
    // 주워진다 — 멀리 있는 엘리트를 쫓는 동안 흘린 구슬은 그대로 소멸한다.
    const int32_t orbExpire = w.tickCount() + cfg.orbLifetimeTicks;
    if (w.entities.archetype[i] == Archetype::Trash) {
        ++w.run.killedTrash;
        w.run.clearPoints += cfg.trashPoints;
        // **굴림은 처치할 때마다 한 번씩** — 드랍 여부와 무관하게 소비해야
        // 난수 소비 횟수가 처치 수만의 함수가 된다 (리플레이 안정성).
        if (w.rngItems.chancePermille(cfg.orbTrashDropPermille)) {
            w.orbs.push(w.entities.posX[i], w.entities.posY[i],
                        cfg.orbTrashAmount, orbExpire);
        }
    } else if (w.entities.archetype[i] == Archetype::Boss) {
        // **보스 처치가 곧 클리어다** (§2).
        w.run.bossAlive = false;
        if (!w.run.over()) {
            w.run.outcome = RunOutcome::Cleared;
            w.run.endTick = w.tickCount();
        }
    } else {
        ++w.run.killedElite;
        w.run.clearPoints += cfg.elitePoints;
        w.orbs.push(w.entities.posX[i], w.entities.posY[i], cfg.orbEliteAmount, orbExpire);
    }
    return dealt;
}

// 스킬 실행. QTE 창이 열렸으면 등급 배율이 붙고, 못 열렸으면 기본 위력이다.
inline void executeSkill(World& w, const SimConfig& cfg, uint32_t skillIndex, QteGrade g);

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
inline void executeSkill(World& w, const SimConfig& cfg, uint32_t skillIndex, QteGrade g) {
    if (skillIndex >= cfg.skillCount) return;
    const SkillConfig& sk = cfg.skills[skillIndex];
    Fixed dmg = w.hero.stats.value(Stat::AttackPower) * sk.mult * qteAmplify(cfg, g)
              * swarmMult(w, cfg) * heroPowerMult(w, cfg);

    // E_CHAIN(연타) — **총 피해를 올리고 그만큼을 2회로 나눈다.**
    // 합계만 올리면 수치형과 다를 게 없다. 나눠 때려야 과잉 피해가 줄고
    // (한 대에 죽을 적에게 남는 몫이 다음 타로 가지 않는다) onHit 훅이 두 번 돈다.
    const Fixed chain = engraveValue(w, EngraveId::Chain);
    const int32_t hits = chain.raw > 0 ? 2 : 1;
    if (hits == 2) dmg = dmg * (Fixed::one() + chain) / Fixed(2);

    Fixed dealt{};
    for (int32_t pass = 0; pass < hits; ++pass) {
        if (sk.aoe) {
            // 광역기는 우선순위가 없다 — 범위 안의 모든 적을 때린다 (§3).
            dealt += applyAoeHits(w, cfg, dmg);
        } else {
            const int32_t d = w.entities.denseOf(w.hero.target);
            if (d >= 0) {
                dealt += applySkillHit(w, cfg, static_cast<uint32_t>(d), dmg);
                applyPierce(w, cfg, static_cast<uint32_t>(d), dmg);
                // W_SHOCKWAVE(충격파) — **단일기가 직선 관통이 된다.** 고유 각인 4번
                dealt += applyShockwave(w, cfg, static_cast<uint32_t>(d), dmg);
            }

            // E_WIDE(확장) — **단일기는 주변에 여파를 남긴다.**
            // 광역기만 강화하면 단일 빌드에서 이 각인이 죽은 카드가 된다.
            const Fixed wide = engraveValue(w, EngraveId::Wide);
            if (wide.raw > 0 && d >= 0) {
                uint32_t hit[config::MAX_ENTITIES];
                const uint32_t n = collectInRadius(w.entities,
                                                   w.entities.posX[static_cast<uint32_t>(d)],
                                                   w.entities.posY[static_cast<uint32_t>(d)],
                                                   wideRadius(w, aoeRadiusOf(w)),
                                                   hit, config::MAX_ENTITIES);
                for (uint32_t k = 0; k < n; ++k) {
                    if (static_cast<int32_t>(hit[k]) == d) continue;   // 본체는 이미 맞았다
                    dealt += applySkillHit(w, cfg, hit[k], dmg * wide);
                }
            }
        }
    }

    // ── E_LEECH(흡혈) — 각인 계열의 유일한 회복 (§2) ──
    // **`onHit` 훅이라 대상마다 붙는다.** 광역기가 5명을 때리면 5배로 들어오는데,
    // 이것이 cards_vertical_slice.md가 예산 적합도 106%를 계산한 전제다.
    // 그래서 흡혈은 물량 빌드와 맞물리고, 기저 정화(처치·구간)와 축이 다르다 —
    // 기저는 **처치 수**에, 흡혈은 **입힌 피해**에 비례한다.
    const Fixed leech = w.cards.engrave[engraveIndex(EngraveId::Leech)];
    if (leech.raw > 0 && dealt.raw > 0) w.purgeCorruption(dealt * leech);
}

// 보스 패턴 순환 — **고정 순서 0 → 1 → 2 → 0.** 랜덤이 아니다
// (monsters_vertical_slice.md: 플레이어가 리듬을 학습할 수 있어야 한다).
//
// 한 번 불릴 때마다 현재 패턴의 피해·텔레그래프·리듬을 엔티티 슬롯에 싣고
// 인덱스를 하나 전진시킨다. 그래서 **호출 시점이 곧 "한 사이클의 시작"** 이고,
// 그 아래 윈드업 분기와 qteRun이 여기서 실린 값을 읽는다.
//
// 보스가 아니거나 패턴 표가 비면 아무것도 하지 않는다 — 잡몹·엘리트는 공격이
// 한 종류뿐이라 스폰 시 실린 값을 그대로 쓴다.
inline void loadBossPattern(World& w, const SimConfig& cfg, uint32_t i) {
    if (w.entities.archetype[i] != Archetype::Boss) return;
    if (cfg.bossPatternCount == 0) return;

    const uint32_t slot = w.entities.patternIndex[i] % cfg.bossPatternCount;
    const BossPattern& p = cfg.bossPatterns[slot];

    w.entities.attackDamage[i]   = p.damage;
    w.entities.windupTicks[i]    = p.windupTicks;
    w.entities.attackInterval[i] = p.cooldownTicks;

    // **uint8_t 순환이므로 255에서 0으로 감는다.** 그대로 두면 오버플로가
    // 아니라 정의된 랩어라운드이고, 아래 % 연산이 순서를 유지한다 —
    // 다만 패턴 수가 256의 약수가 아니면 감기는 지점에서 순서가 튄다.
    // 그래서 **미리 나머지를 취해 0..count-1 안에 가둔다.**
    w.entities.patternIndex[i] =
        static_cast<uint8_t>((slot + 1u) % cfg.bossPatternCount);
}

// CC 발동 임계치. **보스만 발동할 때마다 오른다** (§9) —
// `ccGaugeMax × (1 + resistStep × ccTriggerCount)`.
//
// 저장된 `ccGaugeMax`는 **기준값 그대로 둔다.** 발동할 때마다 그 자리를 덮으면
// 기준값이 사라져 "몇 배까지 올랐나"를 되물을 수 없고, 되돌리는 경로(그로기
// 해제·판 리셋)가 생기는 순간 복구할 원본이 없다. 여기서 매번 계산하는 쪽이
// 상태가 하나 적다.
//
// 엘리트는 임계치 고정이다 — CC 빌드로 계속 묶어둘 수 있는 중간 위협이고,
// 그게 보스와 엘리트를 가르는 설계 축이다.
inline Fixed ccThreshold(const EntityStore& e, const SimConfig& cfg, uint32_t i) {
    if (e.archetype[i] != Archetype::Boss || cfg.bossCcResistStepPermille <= 0) {
        return e.ccGaugeMax[i];
    }
    // 1 + step × count. **넓은 타입이 아니라 Fixed 곱으로 간다** — count는
    // 한 판에 한 자릿수이고 ccGaugeMax는 100 언저리라 20.12 범위를 넘지 않는다.
    const Fixed mult = Fixed(1)
                     + Fixed::fromPermille(cfg.bossCcResistStepPermille) * e.ccTriggerCount[i];
    return e.ccGaugeMax[i] * mult;
}

inline void qteRun(World& w, const SimConfig& cfg) {
    if (w.hero.qteCooldown > 0) --w.hero.qteCooldown;
    if (!w.hero.qte.open()) return;
    if (w.tickCount() < w.hero.qte.closeTick) return;

    const QteWindow q = w.hero.qte;
    const QteGrade  g = q.hasInput ? q.input : QteGrade::Miss;
    w.hero.qte.reset();

    if (q.kind == QteKind::SkillAmplify) {
        executeSkill(w, cfg, q.skillIndex, g);
        return;
    }

    // ── 위기 회피 ──
    // **QTE가 회피 판정 그 자체다** — 명중 굴림이 따로 없고, 실패하면 무조건 맞는다.
    const int32_t d = w.entities.denseOf(q.source);
    if (d < 0) return;                       // 텔레그래프 도중에 죽었다
    const uint32_t i = static_cast<uint32_t>(d);
    w.entities.attackCooldown[i] = w.entities.attackInterval[i];

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
    //
    // **충전량은 기준 임계치의 비율이다** — 오른 임계치가 아니라. 그래야 저항
    // 상승이 실제로 작동한다(오른 값에 비례하면 항상 같은 횟수에 발동한다).
    const Fixed gain = w.entities.ccGaugeMax[i]
                     * Fixed::fromPermille(cfg.qtePerfectCcGainPermille);
    w.entities.ccGauge[i] += gain;
    const Fixed threshold = ccThreshold(w.entities, cfg, i);
    if (threshold.raw > 0 && w.entities.ccGauge[i].raw >= threshold.raw) {
        w.entities.ccGauge[i] = Fixed{};
        ++w.entities.ccTriggerCount[i];
        w.entities.groggyLeft[i] = cfg.groggyTicks;
        w.entities.flags[i] = static_cast<uint8_t>(w.entities.flags[i] | EntityFlag::Groggy);
    }
}

// 구슬 습득·소멸. **전투보다 먼저 돈다** — 이번 틱에 받을 피해보다 먼저
// 회복이 들어와야 "아슬아슬하게 버텼다"가 성립한다. 반대로 두면 같은 틱에
// 죽은 뒤 구슬이 주워지는 순서가 되어 회복이 한 틱씩 늦게 체감된다.
//
// 순회는 뒤에서 앞으로 한다 — 안정 압축이 뒤쪽을 당겨오므로 앞으로 돌면
// 제거 직후의 원소를 건너뛴다.
inline void orbRun(World& w, const SimConfig& cfg) {
    if (w.orbs.count == 0) return;
    const int64_t r = (static_cast<int64_t>(cfg.orbPickupRadiusMilli) * Fixed::ONE_RAW) / 1000;
    const int64_t r2 = r * r;

    for (int32_t i = static_cast<int32_t>(w.orbs.count) - 1; i >= 0; --i) {
        const uint32_t k = static_cast<uint32_t>(i);
        if (w.tickCount() >= w.orbs.expireTick[k]) { w.orbs.removeAt(k); continue; }
        const int64_t dx = static_cast<int64_t>(w.orbs.posX[k].raw) - w.hero.posX.raw;
        const int64_t dy = static_cast<int64_t>(w.orbs.posY[k].raw) - w.hero.posY.raw;
        if (dx * dx + dy * dy > r2) continue;
        w.purgeCorruption(Fixed(w.orbs.amount[k]));
        w.orbs.removeAt(k);
    }
}

// 보스 페이즈 2 — 보스 체력이 임계 아래로 내려간 첫 틱에 한 번 정화한다.
//
// **전투 뒤에 돈다.** 이번 틱의 피해로 임계를 넘겼으면 같은 틱에 보상이 나가야
// "밀어붙인 것에 대한 보상"이 된다. 뒤로 미루면 한 틱 늦는다.
//
// 구간 진입 정화와 같은 장치이고 같은 이유로 존재한다 — 보스전 57초에는
// 구간 경계가 하나도 없어 숨 돌릴 자리가 없었다. 다른 점은 **시간이 아니라
// 보스 체력이 조건**이라는 것뿐이다.
// 페이즈 2에 들어가면 **광역 지속딜이 계속 흐른다.** 로테이션과 무관하게
// 매 틱 들어오므로, 정화 400이 한 번에 틔워준 숨통을 페이즈 2 내내 도로 조인다.
//
// **한 번에 합쳐 적용하지 않는 이유**가 패턴 피해와 다르다. 패턴은 타수를 합쳐도
// 결과가 같지만(mitigate가 선형), 지속딜은 구슬·흡혈과 같은 틱을 두고 경쟁한다 —
// 합쳐 버리면 "회복이 아슬아슬하게 앞섰다"가 성립하지 않는다.
inline void bossPhase2AuraRun(World& w, const SimConfig& cfg) {
    if (!w.run.bossPhase2 || !w.run.bossAlive) return;
    if (cfg.bossPhase2AuraDps <= 0 || cfg.tickHz <= 0) return;
    // 방어력 감쇠를 그대로 탄다 — 피해이므로 다른 피해와 같은 문을 지난다.
    // 나눗셈은 0방향 절삭이다(C++11 표준). 초당 14 / 20Hz = 틱당 0.7이고
    // Fixed 20.12에서 2867raw라 절삭 손실이 0.01% 미만이다.
    const Fixed perSec = mitigate(Fixed(cfg.bossPhase2AuraDps),
                                  w.hero.stats.value(Stat::Armor), cfg.armorK);
    w.hero.corruption += perSec / cfg.tickHz;
    w.notifyCorruptionChanged();
}

inline void bossPhaseRun(World& w, const SimConfig& cfg) {
    if (w.run.bossPhase2) { bossPhase2AuraRun(w, cfg); return; }
    if (!w.run.bossAlive) return;
    if (cfg.bossPhase2AtPermille <= 0 || cfg.bossPhase2Purge <= 0) return;

    const uint32_t n = w.entities.count();
    for (uint32_t i = 0; i < n; ++i) {
        if (w.entities.deadAt(i) || w.entities.archetype[i] != Archetype::Boss) continue;
        // 비율 비교를 나눗셈 없이 한다 — hp * 1000 <= maxHp * 임계.
        // **넓은 타입으로 승격한 뒤 곱한다** (부호 있는 정수 오버플로는 UB다).
        const int64_t max = static_cast<int64_t>(w.entities.maxHp[i].raw);
        const int64_t hp  = max - static_cast<int64_t>(w.entities.damageTaken[i].raw);
        if (hp * 1000 <= max * cfg.bossPhase2AtPermille) {
            w.purgeCorruption(Fixed(cfg.bossPhase2Purge));
            w.run.bossPhase2 = true;
            // 첫 소환은 **한 주기 뒤다.** 진입 즉시 부르면 정화와 소환이 같은 틱에
            // 겹쳐 서로를 가린다 — 숨통이 트인 것도 물량이 는 것도 체감되지 않는다.
            w.run.bossSummonNextTick = w.tickCount() + cfg.bossPhase2SummonPeriodTicks;
            // 진입한 틱부터 장판이 깔린다. 다음 틱으로 미루면 진입 연출과 어긋난다.
            bossPhase2AuraRun(w, cfg);
        }
        return;   // 보스는 하나다
    }
}

// 도트 진행 (E_DECAY). **전투보다 먼저 돈다** — 도트로 죽을 적이 이번 틱에
// 영웅을 때리지 않게 하기 위해서다. 순서를 뒤집으면 도트의 가치가 한 틱씩 늦는다.
inline void decayRun(World& w, const SimConfig& cfg) {
    const uint32_t n = w.entities.count();
    for (uint32_t i = 0; i < n; ++i) {
        if (w.entities.deadAt(i) || w.entities.decayLeft[i] <= 0) continue;
        --w.entities.decayLeft[i];
        const Fixed tick = w.entities.decayPerTick[i];
        if (tick.raw <= 0) continue;
        // **도트는 방어 감쇠를 그대로 받는다** — 무시하면 E_REND와 역할이 겹친다.
        // hooks=false로 불러 도트가 자기 지속을 갱신하지 못하게 한다.
        applySkillHit(w, cfg, i, tick, false);
        if (w.entities.decayLeft[i] <= 0) w.entities.decayPerTick[i] = Fixed{};
    }
}

// RL_STORM(폭풍의 핵) — **영웅 주위를 상시 도는 칼날.**
//
// 회전 위치를 실제로 계산하지 않는다. 칼날이 한 바퀴 도는 시간이 틱보다 짧으므로
// "반경 안에 있으면 맞는다"와 결과가 같고, 각도 상태를 [상태]로 들고 다니지
// 않아도 된다 — 연출은 프레젠테이션이 자기 시계로 그리면 그만이다 (§10).
//
// 수치는 RL_ECHO와 같은 크기가 되도록 역산했다. ECHO는 기본 공격을 한 번 더
// 넣어 +10.5 DPS인데, 반경 2.0타일에 평균 5.4마리가 들어오므로(dc_field 실측
// 타격 비율 0.18 × 상한 30) 대상당 초당 공격력의 19%면 같은 이득이 된다.
// **다만 축이 다르다** — ECHO는 단일 대상이고 STORM은 밀집도에 비례한다.
inline void stormRun(World& w, const SimConfig& cfg) {
    if (!w.cards.legendHas(legendIndexOf(LegendId::Storm))) return;
    if (cfg.stormDpsPermille <= 0 || cfg.tickHz <= 0 || cfg.stormRadius.raw <= 0) return;

    const Fixed perTick = (w.hero.stats.value(Stat::AttackPower)
                        * Fixed::fromPermille(cfg.stormDpsPermille)
                        * heroPowerMult(w, cfg)) / cfg.tickHz;
    if (perTick.raw <= 0) return;

    uint32_t hit[config::MAX_ENTITIES];
    const uint32_t n = collectInRadius(w.entities, w.hero.posX, w.hero.posY,
                                       cfg.stormRadius, hit, config::MAX_ENTITIES);
    // **onHit 각인을 태우지 않는다** — 매 틱 도는 지속 피해에 부식·관통이 붙으면
    // 각인 하나가 초당 20회 발동하는 꼴이 되어 예산이 통째로 무너진다.
    for (uint32_t k = 0; k < n; ++k) applySkillHit(w, cfg, hit[k], perTick, false);
}

// R_BOLT(뇌전의 성물) — 주기마다 무작위 적 하나에 공격력 비례 피해.
//
// **틱 카운터로 주기를 잡는다** — 실시간 참조가 없어 결정론에 안전하다(문서 §2).
// 대상 추첨은 `rngCombat`이 아니라 **살아있는 적을 배열 순서로 세어 k번째**를
// 고르는 방식이다. 기각 재시도를 쓰면 난수 소비 횟수가 전장 상태에 따라 흔들려
// 리플레이가 깨진다 — 카드 추첨(pickNth)에서 쓴 것과 같은 규칙이다.
inline void boltRun(World& w, const SimConfig& cfg) {
    const Fixed bolt = relicValue(w, RelicId::Bolt);
    if (bolt.raw <= 0 || cfg.boltIntervalTicks <= 0) return;
    if (w.tickCount() < w.hero.boltNextTick) return;
    w.hero.boltNextTick = w.tickCount() + cfg.boltIntervalTicks;

    const uint32_t cnt = w.entities.count();
    uint32_t alive = 0;
    for (uint32_t i = 0; i < cnt; ++i) if (!w.entities.deadAt(i)) ++alive;
    if (alive == 0) return;

    // **굴림은 적이 있을 때만 한 번** — 없을 때도 굴리면 소비 횟수가 갈린다.
    uint32_t k = w.rngCombat.range(alive);
    for (uint32_t i = 0; i < cnt; ++i) {
        if (w.entities.deadAt(i)) continue;
        if (k == 0) {
            applySkillHit(w, cfg, i,
                          w.hero.stats.value(Stat::AttackPower) * bolt * heroPowerMult(w, cfg));
            return;
        }
        --k;
    }
}

// E_PIERCE(관통) — **타겟 뒤의 직선상 적에게 감쇠된 피해를 준다.**
//
// 영웅 → 타겟 방향의 반직선 위에서, 타겟보다 멀고 폭 안에 있는 적을 고른다.
// 직선 판정은 내적(진행 거리)과 외적(수직 거리)으로 한다 — 제곱근이 필요 없고
// 전부 정수 연산이라 결정론에 안전하다.
// 영웅 → 타겟 반직선 위, 타겟보다 **뒤**이고 폭·길이 안에 있는 적을 모은다.
// dense 오름차순으로 채우므로 순회 순서가 배열 순서로 고정된다.
//
// E_PIERCE(관통)와 W_SHOCKWAVE(충격파)가 같은 기하를 공유한다 — 다른 것은
// **폭과 피해 비율**뿐이다. 두 벌로 두면 한쪽만 고치는 사고가 난다.
inline uint32_t collectInLine(const World& w, uint32_t target, Fixed width, Fixed length,
                              uint32_t* out, uint32_t outMax) {
    const int64_t dx = static_cast<int64_t>(w.entities.posX[target].raw) - w.hero.posX.raw;
    const int64_t dy = static_cast<int64_t>(w.entities.posY[target].raw) - w.hero.posY.raw;
    const int64_t len = static_cast<int64_t>(isqrt64(static_cast<uint64_t>(dx * dx + dy * dy)));
    if (len <= 0) return 0;

    uint32_t found = 0;
    const uint32_t n = w.entities.count();
    for (uint32_t i = 0; i < n && found < outMax; ++i) {
        if (i == target || w.entities.deadAt(i)) continue;
        const int64_t ex = static_cast<int64_t>(w.entities.posX[i].raw) - w.hero.posX.raw;
        const int64_t ey = static_cast<int64_t>(w.entities.posY[i].raw) - w.hero.posY.raw;
        // 진행 거리 = 내적 / |d| — 타겟보다 뒤이고 사거리 안이어야 한다
        const int64_t along = (ex * dx + ey * dy) / len;
        if (along <= len) continue;
        if (along > len + static_cast<int64_t>(length.raw)) continue;
        // 수직 거리 = |외적| / |d|
        int64_t perp = (ex * dy - ey * dx) / len;
        if (perp < 0) perp = -perp;
        if (perp > static_cast<int64_t>(width.raw)) continue;
        out[found++] = i;
    }
    return found;
}

inline void applyPierce(World& w, const SimConfig& cfg, uint32_t target, Fixed damage) {
    const Fixed ratio = w.cards.engrave[engraveIndex(EngraveId::Pierce)];
    if (ratio.raw <= 0 || cfg.pierceWidth.raw <= 0) return;

    uint32_t hit[config::MAX_ENTITIES];
    const uint32_t n = collectInLine(w, target, cfg.pierceWidth, cfg.pierceLength,
                                     hit, config::MAX_ENTITIES);
    for (uint32_t k = 0; k < n; ++k) applySkillHit(w, cfg, hit[k], damage * ratio);
}

// W_SHOCKWAVE(충격파) — 고유 각인 4번. **단일 타격이 직선 관통으로 바뀐다.**
//
// E_PIERCE와 겹쳐 쌓이는 것이 의도다 (cards_vertical_slice.md §4 중복 규칙) —
// 다만 질이 다르다: 관통은 감쇠된 피해, 충격파는 **전력 피해**다. 그래서 반폭을
// E_PIERCE(700)의 절반으로 잡았다. 같은 폭에 전력 피해면 예산의 194%가 된다.
//
// 길이는 `pierceLength`를 공유한다 — "보이는 만큼 뚫는다"가 두 각인에서 같아야
// 플레이어가 판정을 하나로 학습한다.
inline Fixed applyShockwave(World& w, const SimConfig& cfg, uint32_t target, Fixed damage) {
    if (cfg.shockwaveWidth.raw <= 0) return Fixed{};
    if (!w.cards.legendHas(legendIndexOf(LegendId::Shockwave))) return Fixed{};

    uint32_t hit[config::MAX_ENTITIES];
    const uint32_t n = collectInLine(w, target, cfg.shockwaveWidth, cfg.pierceLength,
                                     hit, config::MAX_ENTITIES);
    Fixed dealt{};
    for (uint32_t k = 0; k < n; ++k) dealt += applySkillHit(w, cfg, hit[k], damage);
    return dealt;
}

// 광역기 한 발. **W_CENTRIFUGE(원심력)가 여기서 반경을 키운다** — 고유 각인 6번.
//
// 각인이 없으면 반경 한 번에 한 번 훑는 것으로 끝난다(기존 동작과 같다).
// 있으면 **벤 수만큼 반경을 키워 다시 훑는다.** 이미 맞은 적은 제외하므로 한
// 발동에서 같은 적을 두 번 때리지 않는다.
//
// ## 결정론
//
// `collectInRadius`가 dense 오름차순으로 채우고, 제외 표식도 dense 인덱스다.
// **틱 중간에는 dense 인덱스가 움직이지 않는다** — 삭제는 틱 종료 압축이므로
// (world.h) 한 발동 안에서 인덱스가 안정적이다. 죽은 적은 다음 패스의
// `collectInRadius`가 걸러내므로 표식과 생존 판정이 어긋나지 않는다.
//
// ## 종료
//
// 새로 맞은 적이 없거나 중첩 상한에 닿으면 멈춘다. 상한이 10이고 패스마다
// 최소 1중첩이 쌓이므로 **최대 11패스**다 — 512마리에서 11 × O(N)이고,
// dc_load 실측으로 광역 발동은 12.4초마다 한 번이라 비용이 묻힌다.
inline Fixed applyAoeHits(World& w, const SimConfig& cfg, Fixed damage) {
    const Fixed base = wideRadius(w, aoeRadiusOf(w));
    uint32_t hit[config::MAX_ENTITIES];

    const bool grow = cfg.centrifugeStepPermille > 0 && cfg.centrifugeMaxStacks > 0
                   && w.cards.legendHas(legendIndexOf(LegendId::Centrifuge));
    if (!grow) {
        const uint32_t n = collectInRadius(w.entities, w.hero.posX, w.hero.posY, base,
                                           hit, config::MAX_ENTITIES);
        Fixed dealt{};
        for (uint32_t k = 0; k < n; ++k) dealt += applySkillHit(w, cfg, hit[k], damage);
        return dealt;
    }

    uint8_t struck[config::MAX_ENTITIES] = {0};
    Fixed   radius = base;
    Fixed   dealt{};
    int32_t stacks = 0;
    for (;;) {
        const uint32_t n = collectInRadius(w.entities, w.hero.posX, w.hero.posY, radius,
                                           hit, config::MAX_ENTITIES);
        int32_t fresh = 0;
        for (uint32_t k = 0; k < n; ++k) {
            if (struck[hit[k]] != 0) continue;
            struck[hit[k]] = 1;
            ++fresh;
            dealt += applySkillHit(w, cfg, hit[k], damage);
        }
        if (fresh == 0 || stacks >= cfg.centrifugeMaxStacks) break;
        stacks += fresh;
        if (stacks > cfg.centrifugeMaxStacks) stacks = cfg.centrifugeMaxStacks;
        radius = base * (Fixed::one()
                       + Fixed::fromPermille(cfg.centrifugeStepPermille * stacks));
    }
    return dealt;
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
            Fixed dmg = w.hero.stats.value(Stat::AttackPower)
                      * swarmMult(w, cfg) * heroPowerMult(w, cfg);
            {
                Fixed chance = w.hero.stats.value(Stat::CritChance);
                Fixed mult   = w.hero.stats.value(Stat::CritMult);
                critWithEngrave(w, &chance, &mult);
                if (w.rngCombat.chanceQ16(chanceToQ16(chance))) dmg = dmg * mult;
            }

            // 통합 proc 판정은 기본 공격당 한 번만 (§3). 발동이 확정되면
            // 어떤 스킬인지는 가중 추첨으로 고른다 — 스킬마다 독립 확률을 굴리면
            // PRD 손익분기가 체감선 밖으로 밀린다(§7).
            if (w.hero.proc.roll(w.rngCombat, cfg.procPrdCQ16) && cfg.skillCount > 0) {
                int32_t weights[MAX_SKILLS];
                for (uint32_t k = 0; k < cfg.skillCount; ++k) weights[k] = cfg.skills[k].weight;
                const uint32_t pick = w.rngCombat.weighted(weights, cfg.skillCount);
                // 창이 못 열려도 **스킬은 기본 위력으로 즉시 나간다.**
                if (!openSkillQte(w, cfg, pick)) {
                    executeSkill(w, cfg, pick, QteGrade::Miss);
                }
            }

            applySkillHit(w, cfg, i, dmg);
            applyPierce(w, cfg, i, dmg);

            // RL_ECHO(무한의 메아리) — **기본 공격이 한 번 더 나간다.**
            // proc은 굴리지 않는다(문서 §3). 파워는 크지만 영향이 "기본 공격 횟수"
            // 하나에 갇히므로 QTE 예산에 아무 영향이 없다 — 초안이던 "proc을 한 번
            // 더 굴린다"는 이득이 더 작으면서 구간당 스킬 발동을 6.8회로 밀어올려
            // 쿨다운이 강제 개입해야 했다.
            if (w.cards.legendHas(legendIndexOf(LegendId::Echo))
                && !w.entities.deadAt(i)) {
                applySkillHit(w, cfg, i, dmg);
                applyPierce(w, cfg, i, dmg);
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

        // **보스: 이번 사이클에 쓸 패턴을 싣는다.** 쿨다운이 끝난 직후여야 한다 —
        // 아래 윈드업 분기와 qteRun의 빗나감 피해가 둘 다 여기서 실린 값을 읽으므로,
        // 뒤로 미루면 한 패턴 늦은 값으로 판정된다.
        loadBossPattern(w, cfg, i);

        // **QTE는 표시된 패턴에만 걸린다** (§3). 일반 몹은 windupTicks가 0이라
        // 예비 동작 없이 즉시 들어가고 자동 판정된다.
        if (w.entities.windupTicks[i] > 0 && !w.hero.qte.open()) {
            w.entities.windupLeft[i] = w.entities.windupTicks[i];
            openCrisisQte(w, cfg, w.entities.idAt(i), w.entities.windupTicks[i]);
            continue;
        }

        incoming += mitigate(w.entities.attackDamage[i], heroArmor, cfg.armorK);
        w.entities.attackCooldown[i] = w.entities.attackInterval[i];
    }
    if (incoming.raw != 0) {
        w.hero.corruption += incoming;
        w.notifyCorruptionChanged();

        // R_RAGE — **피격이 곧 화력이 된다.** 잠식이 차오를수록 반격이 세지므로
        // 위기 구간과 반격 구간이 맞물린다 (R_TIDE와 같은 설계 의도다).
        if (relicValue(w, RelicId::Rage).raw > 0) {
            if (w.hero.rageStacks < cfg.rageMaxStacks) ++w.hero.rageStacks;
            w.hero.rageExpireTick = w.tickCount() + cfg.rageDurationTicks;
        }
    }
    // **만료는 통째로 푼다** — 중첩마다 개별 타이머를 두면 상태가 10배가 되는데,
    // 문서가 정한 것은 "5초"라는 지속 하나뿐이다.
    if (w.hero.rageStacks > 0 && w.tickCount() >= w.hero.rageExpireTick) {
        w.hero.rageStacks = 0;
    }

    // ── 잠식 물량 충전 (§2) ──
    // 피격량과 별개로, 전장의 몹 수가 임계를 넘으면 초당 일정량이 찬다.
    // **이게 "몬스터 수 상한 = 게임오버"를 게이지 하나로 흡수한 장치다.**
    if (cfg.tickHz > 0 && cfg.corruptionThreshold > 0) {
        const int32_t alive = static_cast<int32_t>(aliveCount(w.entities));
        if (alive > cfg.corruptionThreshold) {
            const Fixed perMob = Fixed::fromPermille(cfg.corruptionPerMobPermille);
            Fixed perSec = perMob * (alive - cfg.corruptionThreshold);

            // **상한 초과는 초과분에만 가속이 붙는다** (§2 "초과분에 ×3 가속").
            //
            // 전에는 `alive >= cap`에서 **전체 비율에** ×3을 곱했다. 스폰이 상한을
            // 유지하므로 포화 구간에서는 상시 alive == cap이고, 그 순간 잠식이
            // 6 → 18/초로 계단처럼 뛰었다. 그래서 처치율이 스폰율을 넘느냐 마느냐에
            // 따라 결과가 양분됐다 — 실측에서 처치율 2.12 → 2.33마리/초 사이에
            // 클리어율이 33% → 92%로 튀는 절벽이 이것이다.
            //
            // 한계 가속으로 바꾸면 연속 함수가 되어 **중간 난이도가 생긴다.**
            // tools/balance_baseline.py의 corruption_from_mass()와 같은 식이다.
            const int32_t cap = cfg.capFor(w.run.globalSegment(cfg.segmentsPerMap) + 1);
            if (cap > 0 && alive > cap) {
                const Fixed extra = Fixed::fromPermille(cfg.corruptionOverflowMultPermille - 1000);
                perSec += perMob * (alive - cap) * extra;
            }
            w.hero.corruption += perSec / cfg.tickHz;
            w.notifyCorruptionChanged();
        }
    }
}

}  // namespace dc

#endif  // DC_COMBAT_H
