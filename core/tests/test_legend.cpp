// 전설 고유 각인 (heroes_vertical_slice.md §4 · 전설 풀 3~8번).
//
// **고정 체크섬이 이 효과들을 보지 못한다.** `dev_script`는 전설을 1.5% × 9칸에서
// 뽑으므로 1200틱 런이 특정 각인을 집을 확률이 2% 남짓이고, 실제로 세 효과를
// 전부 꺼도 골든 값이 한 비트도 안 움직인다(실측으로 확인했다). 그래서 발동
// 경로는 여기서 **직접 각인을 쥐여** 본다 — 이 파일이 없으면 세 효과는
// 컴파일만 되고 한 번도 실행되지 않는다.
//
// 구현된 셋만 다룬다. Vortex · Aftershock · Fissure는 지역 효과 풀 대기이고,
// 아래 마지막 절이 "아직 효과가 없다"를 사실로 못 박는다 — 어느 날 효과가
// 붙으면 그 절이 먼저 깨져서 테스트를 같이 쓰게 된다.
#include <cstdio>

#include "../include/dc/sim.h"
#include "../tools/data_files.h"
#include "test_main.h"

using namespace dc;

static World makeWorld(uint64_t seed = 1) {
    World w;
    initWorld(w, seed, dev::data().cfg, dev::data().table, dev::data().hero);
    return w;
}

static SpawnDesc mob(Fixed x, Fixed y, int32_t hp, Archetype a = Archetype::Trash,
                     uint8_t flags = EntityFlag::None) {
    SpawnDesc d;
    d.posX = x; d.posY = y;
    d.maxHp = Fixed(hp);
    d.archetype = a;
    d.attackRange = Fixed(1);
    d.flags = flags;
    return d;
}

// 남은 체력 비율을 permille로. 경계 검사에 쓴다.
static int32_t hpPermille(const World& w, uint32_t i) {
    const int64_t max = w.entities.maxHp[i].raw;
    if (max <= 0) return 0;
    const int64_t left = max - static_cast<int64_t>(w.entities.damageTaken[i].raw);
    return static_cast<int32_t>(left * 1000 / max);
}

int main() {
    printf("=== 전설 고유 각인 ===\n");
    const SimConfig& cfg = dev::data().cfg;
    const uint32_t EXEC   = legendIndexOf(LegendId::Execute);
    const uint32_t SHOCK  = legendIndexOf(LegendId::Shockwave);
    const uint32_t CENTRI = legendIndexOf(LegendId::Centrifuge);

    printf("  수치: 처형 임계 %d‰(%s) · 충격파 반폭 %d‰ · 원심력 +%d‰/적 상한 %d\n",
           cfg.executeThresholdPermille, cfg.executeAllAttacks ? "모든 공격" : "스킬 발동",
           cfg.shockwaveWidth.raw * 1000 / Fixed::ONE_RAW,
           cfg.centrifugeStepPermille, cfg.centrifugeMaxStacks);

    // ── W_EXECUTE 처형 ────────────────────────────────────────────────
    dctest::section("처형 — 각인이 없으면 아무 일도 없다");
    {
        // **효과가 각인 소지와 무관하게 돌면 전설이 전설이 아니게 되는데,
        // 증상은 "적이 좀 빨리 죽는다"뿐이라 눈으로는 안 보인다.**
        World w = makeWorld(1);
        const EntityId id = w.entities.spawn(mob(Fixed(1), Fixed{}, 1000), 0, 1);
        const uint32_t i = static_cast<uint32_t>(w.entities.denseOf(id));
        // 임계(400‰) 아래로 깎아 둔다 — 남은 10%
        w.entities.damageTaken[i] = Fixed(900);
        CHECK_EQ(hpPermille(w, i), 100);

        applySkillHit(w, cfg, i, Fixed(1));
        CHECK(!w.entities.deadAt(i));          // 1 피해로는 안 죽는다
        CHECK(hpPermille(w, i) > 0);
    }

    dctest::section("처형 — 임계 이하면 한 대에 죽는다");
    {
        World w = makeWorld(2);
        w.cards.legendTake(EXEC);
        const EntityId id = w.entities.spawn(mob(Fixed(1), Fixed{}, 1000), 0, 2);
        const uint32_t i = static_cast<uint32_t>(w.entities.denseOf(id));
        w.entities.damageTaken[i] = Fixed(900);

        applySkillHit(w, cfg, i, Fixed(1));
        CHECK(w.entities.deadAt(i));
        // **처치 경로를 그대로 탄다** — 남은 체력을 채워 보내므로 경험치·구슬·
        // 클리어 포인트가 따로 적히지 않는다 (두 군데에 적히면 한쪽만 고치게 된다)
        CHECK_EQ(w.run.killedTrash, 1);
        CHECK(w.run.clearPoints >= cfg.trashPoints);
        printf("    처치 처리까지 이어진다 (잡몹 %d · 게이지 %d)\n",
               w.run.killedTrash, w.run.clearPoints);
    }

    dctest::section("처형 — 임계 **바로 위**는 살아남는다 (경계)");
    {
        // 교차곱 비교의 경계다. 나눗셈으로 바꾸면 절삭 방향이 여기서 결과를 뒤집는다.
        World w = makeWorld(3);
        w.cards.legendTake(EXEC);
        const int32_t T = cfg.executeThresholdPermille;

        // 최대 1000, 임계 400‰ → 남은 체력 400이면 처형, 401이면 생존
        for (int32_t left : {T, T + 1}) {
            const EntityId id = w.entities.spawn(mob(Fixed(1), Fixed{}, 1000), 0, 3);
            const uint32_t i = static_cast<uint32_t>(w.entities.denseOf(id));
            w.entities.damageTaken[i] = Fixed(1000 - left);
            const bool killed = executeIfBelowThreshold(w, cfg, i);
            CHECK_EQ(killed, left <= T);
        }
        printf("    남은 %d‰ 처형 · %d‰ 생존\n", T, T + 1);
    }

    dctest::section("처형 — **보스는 면역이다** (페이즈 2를 지킨다)");
    {
        // 면역이 없으면 임계 400‰이 페이즈 2(체력 500‰ 시작)의 80%를 생략한다.
        // 면역을 각인이 아니라 몬스터 플래그로 둔 덕에 여기서 플래그만 본다.
        World w = makeWorld(4);
        w.cards.legendTake(EXEC);
        const EntityId id = w.entities.spawn(
            mob(Fixed(1), Fixed{}, 1000, Archetype::Boss, EntityFlag::ExecuteImmune), 0, 4);
        const uint32_t i = static_cast<uint32_t>(w.entities.denseOf(id));
        w.entities.damageTaken[i] = Fixed(950);     // 남은 5% — 임계보다 한참 아래

        CHECK(!executeIfBelowThreshold(w, cfg, i));
        CHECK(!w.entities.deadAt(i));
        CHECK(!w.run.over());                        // 보스 처치 = 클리어가 아니다

        // **데이터가 실제로 플래그를 세운다.** 코드만 지원하고 데이터가 비어 있으면
        // 면역이 없는 것과 같은데, 그건 조용히 틀린다.
        CHECK((cfg.boss.flags & EntityFlag::ExecuteImmune) != 0);
    }

    dctest::section("처형 — 도트·장판은 처형하지 않는다");
    {
        // 설계가 말한 범위는 **영웅의 타격**이고 예산도 공속으로 환산했다.
        // 틱형 피해까지 포함하면 판정 빈도가 올라 예산을 넘는다.
        World w = makeWorld(5);
        w.cards.legendTake(EXEC);
        const EntityId id = w.entities.spawn(mob(Fixed(1), Fixed{}, 1000), 0, 5);
        const uint32_t i = static_cast<uint32_t>(w.entities.denseOf(id));
        w.entities.damageTaken[i] = Fixed(900);

        applySkillHit(w, cfg, i, Fixed(1), /*hooks=*/false);
        CHECK(!w.entities.deadAt(i));
        // 같은 상태에서 타격이면 죽는다 — 차이가 hooks 하나뿐임을 못 박는다
        applySkillHit(w, cfg, i, Fixed(1), /*hooks=*/true);
        CHECK(w.entities.deadAt(i));
    }

    // ── W_SHOCKWAVE 충격파 ────────────────────────────────────────────
    dctest::section("충격파 — 각인이 없으면 뒤의 적이 안 맞는다");
    {
        World w = makeWorld(6);
        w.hero.posX = Fixed{}; w.hero.posY = Fixed{};
        const EntityId front = w.entities.spawn(mob(Fixed(2), Fixed{}, 10000), 0, 6);
        const EntityId back  = w.entities.spawn(mob(Fixed(4), Fixed{}, 10000), 0, 6);
        const uint32_t f = static_cast<uint32_t>(w.entities.denseOf(front));
        const uint32_t b = static_cast<uint32_t>(w.entities.denseOf(back));

        CHECK_EQ(applyShockwave(w, cfg, f, Fixed(100)).raw, 0);
        CHECK_EQ(w.entities.damageTaken[b].raw, 0);
    }

    dctest::section("충격파 — 뒤의 적에게 **전력** 피해 (E_PIERCE는 감쇠 피해)");
    {
        World w = makeWorld(7);
        w.hero.posX = Fixed{}; w.hero.posY = Fixed{};
        w.cards.legendTake(SHOCK);
        const EntityId front = w.entities.spawn(mob(Fixed(2), Fixed{}, 10000), 0, 7);
        const EntityId back  = w.entities.spawn(mob(Fixed(4), Fixed{}, 10000), 0, 7);
        const uint32_t f = static_cast<uint32_t>(w.entities.denseOf(front));
        (void)back;

        // 본체를 먼저 때려 기준값을 잡는다 (armor 0이라 감쇠가 없다)
        const Fixed onFront = applySkillHit(w, cfg, f, Fixed(100));
        const Fixed onBack  = applyShockwave(w, cfg, f, Fixed(100));
        CHECK(onBack.raw > 0);
        CHECK_EQ(onBack.raw, onFront.raw);              // **전력이다. 감쇠가 없다**
        printf("    본체 %d raw · 뒤의 적 %d raw (같다)\n", onFront.raw, onBack.raw);
    }

    dctest::section("충격파 — 반폭 밖은 안 맞는다");
    {
        World w = makeWorld(8);
        w.hero.posX = Fixed{}; w.hero.posY = Fixed{};
        w.cards.legendTake(SHOCK);
        const EntityId front = w.entities.spawn(mob(Fixed(2), Fixed{}, 10000), 0, 8);
        // 반폭이 350‰(0.35타일)이므로 1.0타일 옆은 벗어난다
        const EntityId side  = w.entities.spawn(mob(Fixed(4), Fixed(1), 10000), 0, 8);
        const uint32_t f = static_cast<uint32_t>(w.entities.denseOf(front));
        const uint32_t sd = static_cast<uint32_t>(w.entities.denseOf(side));

        CHECK(cfg.shockwaveWidth.raw < Fixed(1).raw);    // 전제: 반폭 < 1타일
        CHECK_EQ(applyShockwave(w, cfg, f, Fixed(100)).raw, 0);
        CHECK_EQ(w.entities.damageTaken[sd].raw, 0);
    }

    dctest::section("충격파 — E_PIERCE와 **겹쳐 쌓인다**");
    {
        // 중복 규칙(§4)이 의도한 시너지다. 좁은 충격파 선은 넓은 관통 선의
        // 부분집합이라 둘 다 든 빌드에서는 뒤의 적이 두 번 맞는다.
        World w = makeWorld(9);
        w.hero.posX = Fixed{}; w.hero.posY = Fixed{};
        w.cards.legendTake(SHOCK);
        w.cards.engrave[engraveIndex(EngraveId::Pierce)] = Fixed::fromPermille(300);
        const EntityId front = w.entities.spawn(mob(Fixed(2), Fixed{}, 10000), 0, 9);
        const EntityId back  = w.entities.spawn(mob(Fixed(4), Fixed{}, 10000), 0, 9);
        const uint32_t f = static_cast<uint32_t>(w.entities.denseOf(front));
        const uint32_t b = static_cast<uint32_t>(w.entities.denseOf(back));

        applyPierce(w, cfg, f, Fixed(100));
        const int32_t afterPierce = w.entities.damageTaken[b].raw;
        applyShockwave(w, cfg, f, Fixed(100));
        const int32_t afterBoth = w.entities.damageTaken[b].raw;
        CHECK(afterPierce > 0);
        CHECK(afterBoth > afterPierce);
        printf("    관통만 %d raw → 충격파까지 %d raw\n", afterPierce, afterBoth);
    }

    // ── W_CENTRIFUGE 원심력 ───────────────────────────────────────────
    dctest::section("원심력 — 각인이 없으면 기본 반경 안만 맞는다");
    {
        World w = makeWorld(10);
        w.hero.posX = Fixed{}; w.hero.posY = Fixed{};
        const Fixed base = w.hero.stats.value(Stat::AoeRadius);
        // 기본 반경 안 1마리 + 바로 밖 1마리
        const EntityId in  = w.entities.spawn(mob(base / Fixed(2), Fixed{}, 10000), 0, 10);
        const EntityId out = w.entities.spawn(mob(base * Fixed(2), Fixed{}, 10000), 0, 10);
        const uint32_t a = static_cast<uint32_t>(w.entities.denseOf(in));
        const uint32_t b = static_cast<uint32_t>(w.entities.denseOf(out));

        CHECK(applyAoeHits(w, cfg, Fixed(100)).raw > 0);
        CHECK(w.entities.damageTaken[a].raw > 0);
        CHECK_EQ(w.entities.damageTaken[b].raw, 0);      // 밖은 안 맞는다
    }

    dctest::section("원심력 — 벤 수만큼 반경이 커져 **연쇄로** 닿는다");
    {
        World w = makeWorld(11);
        w.hero.posX = Fixed{}; w.hero.posY = Fixed{};
        w.cards.legendTake(CENTRI);
        const Fixed base = w.hero.stats.value(Stat::AoeRadius);

        // 기본 반경 안에 6마리를 넣으면 중첩 6 → 반경 ×(1 + 0.07×6) = ×1.42다.
        // **상한(×1.70)이 아니라 실제로 쌓인 중첩이 반경을 정한다** — 바깥 적은
        // 그 사이(×1.25)에 둔다. 상한까지 쌓으려면 바깥에 적이 더 있어야 하고,
        // 그 경우는 아래 상한 절이 따로 본다.
        const int32_t kInner = 6;
        for (int32_t k = 0; k < kInner; ++k) {
            w.entities.spawn(mob(base / Fixed(4) + Fixed::fromRaw(k * 64), Fixed{}, 10000), 0, 11);
        }
        const Fixed reach = Fixed::one()
            + Fixed::fromPermille(cfg.centrifugeStepPermille * kInner);
        CHECK(reach.raw > Fixed::fromPermille(1250).raw);   // 전제: ×1.25는 닿는다
        const EntityId far = w.entities.spawn(
            mob(base * Fixed::fromPermille(1250), Fixed{}, 10000), 0, 11);
        const uint32_t fi = static_cast<uint32_t>(w.entities.denseOf(far));

        // 각인 없는 월드와 나란히 비교한다 — **차이가 각인에서 온다는 직접 증거**
        World off = makeWorld(11);
        off.hero.posX = Fixed{}; off.hero.posY = Fixed{};
        for (int32_t k = 0; k < kInner; ++k) {
            off.entities.spawn(mob(base / Fixed(4) + Fixed::fromRaw(k * 64), Fixed{}, 10000), 0, 11);
        }
        const EntityId farOff = off.entities.spawn(
            mob(base * Fixed::fromPermille(1250), Fixed{}, 10000), 0, 11);
        const uint32_t fo = static_cast<uint32_t>(off.entities.denseOf(farOff));

        applyAoeHits(w, cfg, Fixed(100));
        applyAoeHits(off, cfg, Fixed(100));
        CHECK(w.entities.damageTaken[fi].raw > 0);       // 확장이 닿았다
        CHECK_EQ(off.entities.damageTaken[fo].raw, 0);   // 확장이 없으면 못 닿는다
        printf("    기본 반경의 1.25배 거리: 원심력 %d raw · 없으면 %d raw\n",
               w.entities.damageTaken[fi].raw, off.entities.damageTaken[fo].raw);
    }

    dctest::section("원심력 — 한 발동에서 **같은 적을 두 번 때리지 않는다**");
    {
        // 패스를 돌 때마다 반경 안을 다시 훑으므로, 제외 표식이 없으면 안쪽 적이
        // 패스마다 또 맞는다 — 피해가 중첩 수만큼 뻥튀기되는데 크래시는 없다.
        World w = makeWorld(12);
        w.hero.posX = Fixed{}; w.hero.posY = Fixed{};
        w.cards.legendTake(CENTRI);
        const Fixed base = w.hero.stats.value(Stat::AoeRadius);
        const EntityId inner = w.entities.spawn(mob(base / Fixed(4), Fixed{}, 100000), 0, 12);
        const uint32_t ii = static_cast<uint32_t>(w.entities.denseOf(inner));
        // 확장을 유발할 바깥 적 — 중첩이 실제로 쌓이게 한다
        for (int32_t k = 0; k < 3; ++k) {
            w.entities.spawn(mob(base * Fixed::fromPermille(1200 + k * 100), Fixed{},
                                 100000), 0, 12);
        }

        const Fixed one = Fixed(100);
        applyAoeHits(w, cfg, one);
        // armor 0이라 감쇠가 없다 → 한 번 맞았으면 정확히 one이다
        CHECK_EQ(w.entities.damageTaken[ii].raw, one.raw);
    }

    dctest::section("원심력 — 중첩 상한에서 멈춘다 (무한 확장 금지)");
    {
        // 상한이 없으면 물량↑ → 반경↑ → 더 많이 벰 → 반경↑ 이 끝나지 않는다.
        // 적을 일정 간격으로 쭉 늘어놓아 "항상 새 적이 있는" 최악을 만든다.
        World w = makeWorld(13);
        w.hero.posX = Fixed{}; w.hero.posY = Fixed{};
        w.cards.legendTake(CENTRI);
        const Fixed base = w.hero.stats.value(Stat::AoeRadius);
        for (int32_t k = 1; k <= 60; ++k) {
            w.entities.spawn(mob(base * Fixed::fromPermille(200 * k), Fixed{}, 100000), 0, 13);
        }
        applyAoeHits(w, cfg, Fixed(100));

        // 최대 반경 = base × (1 + step×상한). 그 밖의 적은 **한 대도 안 맞아야 한다**
        const Fixed maxR = base * (Fixed::one()
            + Fixed::fromPermille(cfg.centrifugeStepPermille * cfg.centrifugeMaxStacks));
        int32_t hitOutside = 0, hitInside = 0;
        for (uint32_t i = 0; i < w.entities.count(); ++i) {
            const int64_t d2 = distanceSq(w.entities.posX[i], w.entities.posY[i],
                                          w.hero.posX, w.hero.posY);
            const int64_t r2 = static_cast<int64_t>(maxR.raw) * maxR.raw;
            const bool hit = w.entities.damageTaken[i].raw > 0;
            if (d2 > r2) { if (hit) ++hitOutside; }
            else if (hit) ++hitInside;
        }
        CHECK(hitInside > 0);
        CHECK_EQ(hitOutside, 0);
        printf("    최대 반경 %d‰타일 안 %d마리 피격 · 밖 %d마리\n",
               maxR.raw * 1000 / Fixed::ONE_RAW, hitInside, hitOutside);
    }

    // ── 결정론 ────────────────────────────────────────────────────────
    dctest::section("세 각인을 다 들고도 **결정론이 유지된다**");
    {
        // 원심력이 다중 패스를 돌므로 순회 순서가 결과에 새는지가 여기서 드러난다.
        uint64_t first = 0;
        for (int32_t run = 0; run < 3; ++run) {
            World w = makeWorld(777);
            w.cards.legendTake(EXEC);
            w.cards.legendTake(SHOCK);
            w.cards.legendTake(CENTRI);
            static SimScratch sc;
            for (int32_t t = 0; t < 600; ++t) stepWorld(w, cfg, dev::data().table, sc);
            if (run == 0) first = w.checksum();
            else CHECK_EQU(w.checksum(), first);
        }
        printf("    같은 시드 3회 × 600틱 일치 (%llu)\n",
               static_cast<unsigned long long>(first));
    }

    dctest::section("★미구현 3종은 **수치만 적재되고 효과가 없다**");
    {
        // 어느 날 효과가 붙으면 이 절이 먼저 깨진다 — 그때 위쪽처럼 전용 절을
        // 쓰게 된다. "미구현"을 주석에만 적어두면 구현된 뒤에도 아무 일이 없다.
        CHECK(cfg.vortexDurationTicks > 0);
        CHECK(cfg.vortexDpsPermille > 0);
        CHECK(cfg.aftershockDamagePermille > 0);
        CHECK(cfg.aftershockFuseTicks > 0);
        CHECK(cfg.fissureSlowPermille > 0);
        CHECK(cfg.fissureRadius.raw > 0);
        CHECK(cfg.fissureDurationTicks > 0);

        // **틱 루프로 재면 안 된다.** 전설을 쥐면 그 칸이 카드 풀에서 빠져
        // (§4 중복 규칙) 추첨이 달라지고 런 전체가 갈라진다 — 효과가 0이어도
        // 체크섬은 움직인다. 그래서 스킬 실행 한 번만 떼어 비교한다.
        for (uint32_t sk = 0; sk < cfg.skillCount; ++sk) {
            int32_t total[2] = {0, 0};
            for (int32_t k = 0; k < 2; ++k) {
                World w = makeWorld(888);
                w.hero.posX = Fixed{}; w.hero.posY = Fixed{};
                if (k == 1) {
                    w.cards.legendTake(legendIndexOf(LegendId::Vortex));
                    w.cards.legendTake(legendIndexOf(LegendId::Aftershock));
                    w.cards.legendTake(legendIndexOf(LegendId::Fissure));
                }
                // 단일기·광역기 양쪽에 걸리도록 앞뒤·옆으로 흩뿌린다
                for (int32_t d = 1; d <= 5; ++d) {
                    w.entities.spawn(mob(Fixed::fromPermille(d * 400), Fixed{}, 100000), 0, 888);
                    w.entities.spawn(mob(Fixed::fromPermille(d * 400),
                                         Fixed::fromPermille(300), 100000), 0, 888);
                }
                w.hero.target = w.entities.idAt(0);
                executeSkill(w, cfg, sk, QteGrade::Miss);
                for (uint32_t i = 0; i < w.entities.count(); ++i) {
                    total[k] += w.entities.damageTaken[i].raw;
                }
            }
            CHECK_EQ(total[0], total[1]);
        }
        printf("    세 각인 소지가 스킬 %u종의 피해를 바꾸지 않는다 (설계대로)\n",
               cfg.skillCount);
    }

    return dctest::summary("test_legend");
}
