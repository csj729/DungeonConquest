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

// **hp는 Fixed(20.12) 범위 안이어야 한다** (±524,287). 처음에 1000000을 썼다가
// UBSan이 `1000000 * 4096`에서 부호 있는 오버플로를 잡았다 — 테스트가 "안 죽는
// 샌드백"을 원해서 큰 값을 적고 싶어지는 자리라 여기 적어둔다.
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
    dctest::section("충격파 — **각인이 없으면** 뒤의 적이 안 맞는다 (executeSkill 경로)");
    {
        // `applyShockwave`는 더 이상 스스로 소지를 묻지 않는다 — 어느 스킬에
        // 붙는지는 `executeSkill`이 `uniqueSkill`로 안다. 그래서 게이트가
        // 실제로 닫히는지는 **스킬 실행 경로로** 봐야 한다.
        const uint32_t smash = static_cast<uint32_t>(cfg.uniqueSkill[SHOCK - 3]);
        CHECK(smash < cfg.skillCount);
        CHECK(!cfg.skills[smash].aoe);        // 충격파는 단일기에 붙는다

        int32_t back[2] = {0, 0};
        for (int32_t k = 0; k < 2; ++k) {
            World w = makeWorld(6);
            w.hero.posX = Fixed{}; w.hero.posY = Fixed{};
            if (k == 1) w.cards.legendTake(SHOCK);
            const EntityId f = w.entities.spawn(mob(Fixed(2), Fixed{}, 100000), 0, 6);
            const EntityId b = w.entities.spawn(mob(Fixed(4), Fixed{}, 100000), 0, 6);
            w.hero.target = f;
            executeSkill(w, cfg, smash, QteGrade::Miss);
            back[k] = w.entities.damageTaken[static_cast<uint32_t>(w.entities.denseOf(b))].raw;
        }
        CHECK_EQ(back[0], 0);
        CHECK(back[1] > 0);
        printf("    각인 없음 %d raw → 있음 %d raw\n", back[0], back[1]);
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

        CHECK(applyAoeHits(w, cfg, w.hero.posX, w.hero.posY, wideRadius(w, aoeRadiusOf(w)),
                           Fixed(100), /*centrifuge=*/false, /*ccGain=*/0).raw > 0);
        CHECK(w.entities.damageTaken[a].raw > 0);
        CHECK_EQ(w.entities.damageTaken[b].raw, 0);      // 밖은 안 맞는다
    }

    dctest::section("원심력 — 벤 수만큼 반경이 커져 **연쇄로** 닿는다");
    {
        World w = makeWorld(11);
        w.hero.posX = Fixed{}; w.hero.posY = Fixed{};
        w.cards.legendTake(CENTRI);
        const Fixed base = w.hero.stats.value(Stat::AoeRadius);

        // **상한이 아니라 실제로 쌓인 중첩이 반경을 정한다.** 그걸 보려면 안쪽
        // 적을 상한 **아래로** 둬야 한다 — 상한 3에서 6마리를 넣으면 첫 패스에
        // 바로 물려 중첩과 상한을 구분할 수 없다. 2마리면 ×(1 + 0.07×2) = ×1.14다.
        //
        // 상한을 내리면 이 전제가 조용히 깨지므로 **코드에서 읽어 단정한다**.
        const int32_t kInner = 2;
        CHECK(kInner < cfg.centrifugeMaxStacks);   // 전제: 상한에 물리지 않는다
        for (int32_t k = 0; k < kInner; ++k) {
            w.entities.spawn(mob(base / Fixed(4) + Fixed::fromRaw(k * 64), Fixed{}, 10000), 0, 11);
        }
        const Fixed reach = Fixed::one()
            + Fixed::fromPermille(cfg.centrifugeStepPermille * kInner);
        // 바깥 적은 기본 반경과 `reach` 사이에 둔다 — 확장 없이는 못 닿는 거리다
        const int32_t farAt = 1000 + (reach.raw * 1000 / Fixed::ONE_RAW - 1000) / 2;
        CHECK(farAt > 1000 && Fixed::fromPermille(farAt).raw < reach.raw);
        const EntityId far = w.entities.spawn(
            mob(base * Fixed::fromPermille(farAt), Fixed{}, 10000), 0, 11);
        const uint32_t fi = static_cast<uint32_t>(w.entities.denseOf(far));

        // 각인 없는 월드와 나란히 비교한다 — **차이가 각인에서 온다는 직접 증거**
        World off = makeWorld(11);
        off.hero.posX = Fixed{}; off.hero.posY = Fixed{};
        for (int32_t k = 0; k < kInner; ++k) {
            off.entities.spawn(mob(base / Fixed(4) + Fixed::fromRaw(k * 64), Fixed{}, 10000), 0, 11);
        }
        const EntityId farOff = off.entities.spawn(
            mob(base * Fixed::fromPermille(farAt), Fixed{}, 10000), 0, 11);
        const uint32_t fo = static_cast<uint32_t>(off.entities.denseOf(farOff));

        applyAoeHits(w, cfg, w.hero.posX, w.hero.posY, wideRadius(w, aoeRadiusOf(w)),
                     Fixed(100), /*centrifuge=*/true, /*ccGain=*/0);
        applyAoeHits(off, cfg, off.hero.posX, off.hero.posY, wideRadius(off, aoeRadiusOf(off)),
                     Fixed(100), /*centrifuge=*/false, /*ccGain=*/0);
        CHECK(w.entities.damageTaken[fi].raw > 0);       // 확장이 닿았다
        CHECK_EQ(off.entities.damageTaken[fo].raw, 0);   // 확장이 없으면 못 닿는다
        printf("    중첩 %d → 반경 ×%d‰. 거리 ×%d‰: 원심력 %d raw · 없으면 %d raw\n",
               kInner, reach.raw * 1000 / Fixed::ONE_RAW, farAt,
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

        // **바깥 적은 패스마다 딱 한 마리씩 새로 닿게 놓는다.** 중첩 j까지 쌓였을
        // 때의 반경(1 + step×j)보다 조금 안쪽이다.
        //
        // 아무 데나 두면 안 된다 — 확장 반경 밖에 두면 2패스가 **아예 돌지 않고**
        // 그래도 이 테스트는 통과한다(중복이 없으니까). 검사가 조용히 멈추는 것이다.
        // 그래서 위치를 step에서 도출하고, 끝에서 **실제로 닿았는지** 단정한다.
        const int32_t step = cfg.centrifugeStepPermille;
        const int32_t outerN = cfg.centrifugeMaxStacks - 1;
        CHECK(outerN >= 1);
        uint32_t outer[16];
        for (int32_t j = 1; j <= outerN && j <= 16; ++j) {
            const int32_t at = 1000 + step * j - step / 3;   // 중첩 j의 반경 안쪽
            CHECK(at > 1000 + step * (j - 1));               // 전제: 이전 패스엔 못 닿는다
            const EntityId e = w.entities.spawn(
                mob(base * Fixed::fromPermille(at), Fixed{}, 100000), 0, 12);
            outer[j - 1] = static_cast<uint32_t>(w.entities.denseOf(e));
        }

        const Fixed one = Fixed(100);
        applyAoeHits(w, cfg, w.hero.posX, w.hero.posY, wideRadius(w, aoeRadiusOf(w)),
                     one, /*centrifuge=*/true, /*ccGain=*/0);
        // armor 0이라 감쇠가 없다 → 한 번 맞았으면 정확히 one이다
        CHECK_EQ(w.entities.damageTaken[ii].raw, one.raw);
        // **패스가 실제로 돌았다는 증거.** 이게 없으면 위 단정이 공짜로 통과한다
        for (int32_t j = 0; j < outerN && j < 16; ++j) {
            CHECK_EQ(w.entities.damageTaken[outer[j]].raw, one.raw);
        }
        printf("    안쪽 1마리가 %d패스에 걸쳐 훑였고 피해는 %d raw 한 번뿐\n",
               outerN + 1, w.entities.damageTaken[ii].raw);
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
        applyAoeHits(w, cfg, w.hero.posX, w.hero.posY, wideRadius(w, aoeRadiusOf(w)),
                     Fixed(100), /*centrifuge=*/true, /*ccGain=*/0);

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

    // ── 스킬 바인딩 (1차 구현의 버그를 고친 자리) ─────────────────────
    dctest::section("**각인은 지정된 스킬에서만 터진다** (예산 221% 버그)");
    {
        // 1차 구현은 `aoe` 여부로 갈라서 원심력이 회전 베기 **와** 대지 가르기
        // 양쪽에 걸렸다. 둘 다 aoe이기 때문이다 — 예산이 99%에서 221%가 됐는데
        // 크래시가 없어 테스트가 전부 초록이었다.
        //
        // 이제 `uniqueSkill`이 데이터에서 온다. 각 각인이 **정확히 한 스킬**을
        // 가리키고, 서로 다른 각인이 같은 스킬 칸을 공유하는 것은 설계대로다
        // (스킬당 2종).
        for (uint32_t u = 0; u < 6; ++u) {
            CHECK(cfg.uniqueSkill[u] >= 0);
            CHECK(static_cast<uint32_t>(cfg.uniqueSkill[u]) < cfg.skillCount);
        }
        // 처형·충격파는 같은 스킬, 소용돌이·원심력도, 여진·균열도
        CHECK_EQ(cfg.uniqueSkill[0], cfg.uniqueSkill[1]);
        CHECK_EQ(cfg.uniqueSkill[2], cfg.uniqueSkill[3]);
        CHECK_EQ(cfg.uniqueSkill[4], cfg.uniqueSkill[5]);
        // 세 짝은 서로 다른 스킬이어야 한다
        CHECK(cfg.uniqueSkill[0] != cfg.uniqueSkill[2]);
        CHECK(cfg.uniqueSkill[2] != cfg.uniqueSkill[4]);

        // **원심력이 다른 광역기에서는 반경을 키우지 않는다.** 이게 그 버그다.
        const uint32_t whirl  = static_cast<uint32_t>(cfg.uniqueSkill[CENTRI - 3]);
        const uint32_t cleave = static_cast<uint32_t>(cfg.uniqueSkill[4]);   // 여진 쪽
        CHECK(cfg.skills[whirl].aoe);
        CHECK(cfg.skills[cleave].aoe);        // 전제: 둘 다 광역기다

        const Fixed base = dev::data().hero.bases[static_cast<uint32_t>(Stat::AoeRadius)];
        int32_t far[2] = {0, 0};
        for (int32_t k = 0; k < 2; ++k) {
            World w = makeWorld(20);
            w.hero.posX = Fixed{}; w.hero.posY = Fixed{};
            w.cards.legendTake(CENTRI);
            for (int32_t m = 0; m < 6; ++m) {
                w.entities.spawn(mob(base / Fixed(4) + Fixed::fromRaw(m * 64), Fixed{},
                                     100000), 0, 20);
            }
            // 확장 최대 반경(1 + step×상한) **안쪽**이어야 한다. 상한을 내리면
            // 1.25배 같은 고정값은 밖으로 밀려나고, 그러면 이 테스트는 "각인이
            // 안 붙었다"가 아니라 "애초에 닿지 않는다"를 보게 된다.
            const int32_t at = 1000 + cfg.centrifugeStepPermille
                                    * cfg.centrifugeMaxStacks
                             - cfg.centrifugeStepPermille / 3;
            CHECK(at > 1000);
            const EntityId f = w.entities.spawn(
                mob(base * Fixed::fromPermille(at), Fixed{}, 100000), 0, 20);
            executeSkill(w, cfg, k == 0 ? whirl : cleave, QteGrade::Miss);
            far[k] = w.entities.damageTaken[static_cast<uint32_t>(w.entities.denseOf(f))].raw;
        }
        CHECK(far[0] > 0);          // 회전 베기 — 확장이 닿는다
        CHECK_EQ(far[1], 0);        // 대지 가르기 — 확장이 없다
        printf("    원심력: 회전 베기 %d raw · 대지 가르기 %d raw (붙지 않는다)\n",
               far[0], far[1]);
    }

    // ── 지역 효과 풀 ──────────────────────────────────────────────────
    dctest::section("소용돌이 — 즉발을 **대체한다** (장판 하나가 깔린다)");
    {
        const uint32_t whirl = static_cast<uint32_t>(cfg.uniqueSkill[legendIndexOf(LegendId::Vortex) - 3]);
        World w = makeWorld(30);
        w.hero.posX = Fixed{}; w.hero.posY = Fixed{};
        w.cards.legendTake(legendIndexOf(LegendId::Vortex));
        const EntityId t = w.entities.spawn(mob(Fixed::fromPermille(500), Fixed{}, 100000), 0, 30);
        const uint32_t i = static_cast<uint32_t>(w.entities.denseOf(t));

        executeSkill(w, cfg, whirl, QteGrade::Miss);
        // **즉발 피해가 없다** — 장판으로 바뀌었다
        CHECK_EQ(w.entities.damageTaken[i].raw, 0);
        CHECK_EQ(w.zones.count, 1u);
        CHECK_EQ(static_cast<int32_t>(w.zones.kind[0]), static_cast<int32_t>(ZoneKind::Vortex));
        CHECK(w.zones.value[0].raw > 0);
        CHECK_EQ(w.zones.expireTick[0], w.tickCount() + cfg.vortexDurationTicks);

        // **zoneRun만 돌린다.** stepWorld를 쓰면 영웅 기본 공격이 같은 적을
        // 때려(장판 반경 1.5 < 영웅 사거리 3.0이라 분리할 수 없다) 장판 피해와
        // 섞인다 — 측정이 아니라 "뭔가 맞았다"가 된다.
        w.tick();                              // 틱만 전진
        zoneRun(w, cfg);
        CHECK_EQ(w.entities.damageTaken[i].raw, w.zones.value[0].raw);
        printf("    즉발 0 → 장판 1개(%d raw/틱) · 지속 %d틱\n",
               w.zones.value[0].raw, cfg.vortexDurationTicks);
    }

    dctest::section("소용돌이 — 지속이 끝나면 **사라지고 피해도 멈춘다**");
    {
        const uint32_t whirl = static_cast<uint32_t>(cfg.uniqueSkill[legendIndexOf(LegendId::Vortex) - 3]);
        World w = makeWorld(31);
        w.hero.posX = Fixed{}; w.hero.posY = Fixed{};
        w.cards.legendTake(legendIndexOf(LegendId::Vortex));
        const EntityId t = w.entities.spawn(mob(Fixed::fromPermille(500), Fixed{}, 100000), 0, 31);
        const uint32_t i = static_cast<uint32_t>(w.entities.denseOf(t));
        executeSkill(w, cfg, whirl, QteGrade::Miss);

        // 틱 전진 + zoneRun만. 전투를 섞지 않는 이유는 위 절과 같다.
        const int32_t perTick = w.zones.value[0].raw;
        for (int32_t k = 0; k < cfg.vortexDurationTicks; ++k) { w.tick(); zoneRun(w, cfg); }
        CHECK_EQ(w.zones.count, 0u);
        // **만료 틱에도 때린 뒤 사라진다** → 지속 80틱이 정확히 80회 피해다.
        // zoneRun이 피해를 먼저 한 바퀴 돌리고 제거를 나중에 하는 순서가 이 성질을
        // 만든다. 반대로 두면 "4초 지속"이 79틱이 되어 데이터와 한 틱 어긋난다.
        const int32_t frozen = w.entities.damageTaken[i].raw;
        CHECK_EQ(frozen, perTick * cfg.vortexDurationTicks);
        for (int32_t k = 0; k < 20; ++k) { w.tick(); zoneRun(w, cfg); }
        CHECK_EQ(w.entities.damageTaken[i].raw, frozen);
        printf("    지속 %d틱 = 정확히 %d회 타격 후 소멸 · 이후 0\n",
               cfg.vortexDurationTicks, cfg.vortexDurationTicks);
    }

    dctest::section("여진 — 도화선이 끝나는 **그 틱에 한 번** 터진다");
    {
        const uint32_t cleave = static_cast<uint32_t>(cfg.uniqueSkill[legendIndexOf(LegendId::Aftershock) - 3]);
        World w = makeWorld(32);
        w.hero.posX = Fixed{}; w.hero.posY = Fixed{};
        w.cards.legendTake(legendIndexOf(LegendId::Aftershock));
        const EntityId t = w.entities.spawn(mob(Fixed::fromPermille(500), Fixed{}, 100000), 0, 32);
        const uint32_t i = static_cast<uint32_t>(w.entities.denseOf(t));

        executeSkill(w, cfg, cleave, QteGrade::Miss);
        // **가산이다** — 본타가 그대로 들어간 뒤 폭발이 예약된다
        const int32_t direct = w.entities.damageTaken[i].raw;
        CHECK(direct > 0);
        CHECK_EQ(w.zones.count, 1u);
        CHECK_EQ(static_cast<int32_t>(w.zones.kind[0]), static_cast<int32_t>(ZoneKind::Aftershock));

        // 도화선 중에는 아무 일도 없다 (틱 전진 + zoneRun만 — 전투는 섞지 않는다)
        for (int32_t k = 0; k < cfg.aftershockFuseTicks - 1; ++k) { w.tick(); zoneRun(w, cfg); }
        CHECK_EQ(w.entities.damageTaken[i].raw, direct);
        CHECK_EQ(w.zones.count, 1u);
        // 그 틱에 터지고 사라진다
        w.tick(); zoneRun(w, cfg);
        const int32_t afterBoom = w.entities.damageTaken[i].raw;
        CHECK(afterBoom > direct);
        CHECK_EQ(w.zones.count, 0u);
        // **두 번 터지지 않는다**
        for (int32_t k = 0; k < 20; ++k) { w.tick(); zoneRun(w, cfg); }
        CHECK_EQ(w.entities.damageTaken[i].raw, afterBoom);
        printf("    본타 %d raw → %d틱 뒤 폭발 %d raw · 한 번만\n",
               direct, cfg.aftershockFuseTicks, afterBoom - direct);
    }

    dctest::section("균열 — 기절을 **유지하고** 둔화를 더한다 (피해는 0)");
    {
        const uint32_t cleave = static_cast<uint32_t>(cfg.uniqueSkill[legendIndexOf(LegendId::Fissure) - 3]);
        World w = makeWorld(33);
        w.hero.posX = Fixed{}; w.hero.posY = Fixed{};
        w.cards.legendTake(legendIndexOf(LegendId::Fissure));
        // 균열 반경 안 · 밖에 한 마리씩
        const EntityId in  = w.entities.spawn(mob(cfg.fissureRadius / Fixed(2), Fixed{}, 100000), 0, 33);
        const EntityId out = w.entities.spawn(mob(cfg.fissureRadius * Fixed(3), Fixed{}, 100000), 0, 33);
        const uint32_t a = static_cast<uint32_t>(w.entities.denseOf(in));
        const uint32_t b = static_cast<uint32_t>(w.entities.denseOf(out));

        executeSkill(w, cfg, cleave, QteGrade::Miss);
        CHECK_EQ(w.zones.count, 1u);
        CHECK_EQ(static_cast<int32_t>(w.zones.kind[0]), static_cast<int32_t>(ZoneKind::Fissure));

        // **반경 안은 느려지고 밖은 그대로다.** 피해는 둘 다 균열에서 오지 않는다
        const Fixed inMult  = slowMult(w, cfg, a);
        const Fixed outMult = slowMult(w, cfg, b);
        CHECK(inMult.raw < Fixed::one().raw);
        CHECK_EQ(outMult.raw, Fixed::one().raw);
        CHECK_EQ(inMult.raw, (Fixed::one() - Fixed::fromPermille(cfg.fissureSlowPermille)).raw);

        // 장판이 피해를 주지 않는다 — zoneRun을 돌려도 균열은 때리지 않는다
        const int32_t before = w.entities.damageTaken[a].raw;
        zoneRun(w, cfg);
        CHECK_EQ(w.entities.damageTaken[a].raw, before);
        printf("    반경 안 이동 배율 %d‰ · 밖 1000‰ · 균열 피해 0\n",
               inMult.raw * 1000 / Fixed::ONE_RAW);
    }

    dctest::section("균열 — 겹치면 **합산이 아니라 큰 쪽**이다");
    {
        // 합산하면 같은 각인을 두 번 쓴 플레이어가 100% 둔화(정지)를 공짜로 얻는다.
        World w = makeWorld(34);
        w.hero.posX = Fixed{}; w.hero.posY = Fixed{};
        const EntityId t = w.entities.spawn(mob(Fixed::fromPermille(500), Fixed{}, 1000), 0, 34);
        const uint32_t i = static_cast<uint32_t>(w.entities.denseOf(t));
        const Fixed slow = Fixed::fromPermille(cfg.fissureSlowPermille);
        for (int32_t k = 0; k < 3; ++k) {
            w.zones.push(ZoneKind::Fissure, Fixed{}, Fixed{}, cfg.fissureRadius, slow, 9999);
        }
        CHECK_EQ(w.zones.count, 3u);
        CHECK_EQ(slowMult(w, cfg, i).raw, (Fixed::one() - slow).raw);
    }

    dctest::section("지역 효과 풀 — 가득 차면 **가장 먼저 사라질 것을 밀어낸다**");
    {
        World w = makeWorld(35);
        for (uint32_t k = 0; k < ZoneState::MAX_ZONES; ++k) {
            w.zones.push(ZoneKind::Vortex, Fixed{}, Fixed{}, Fixed(1), Fixed(1),
                         500 + static_cast<int32_t>(k));
        }
        CHECK_EQ(w.zones.count, ZoneState::MAX_ZONES);
        CHECK_EQ(w.zones.expireTick[0], 500);
        w.zones.push(ZoneKind::Fissure, Fixed{}, Fixed{}, Fixed(2), Fixed(1), 9999);
        CHECK_EQ(w.zones.count, ZoneState::MAX_ZONES);
        CHECK_EQ(w.zones.expireTick[0], 501);                      // 가장 이른 것이 밀렸다
        CHECK_EQ(static_cast<int32_t>(w.zones.kind[ZoneState::MAX_ZONES - 1]),
                 static_cast<int32_t>(ZoneKind::Fissure));         // 새 것이 들어왔다
        // **반경 0은 거부한다** — 아무도 못 맞히는 장판은 상태만 먹는다
        const uint32_t before = w.zones.count;
        CHECK(!w.zones.push(ZoneKind::Vortex, Fixed{}, Fixed{}, Fixed{}, Fixed(1), 9999));
        CHECK_EQ(w.zones.count, before);
    }

    // ── 결정론 ────────────────────────────────────────────────────────
    dctest::section("여섯 각인을 다 들고도 **결정론이 유지된다**");
    {
        // 원심력의 다중 패스와 지역 효과 풀의 안정 압축이 순회 순서를 결과에
        // 흘리는지가 여기서 드러난다.
        uint64_t first = 0;
        for (int32_t run = 0; run < 3; ++run) {
            World w = makeWorld(777);
            for (uint32_t u = legendIndexOf(LegendId::UniqueFirst); u < 9; ++u) {
                w.cards.legendTake(u);     // 여섯 각인 전부
            }
            static SimScratch sc;
            for (int32_t t = 0; t < 600; ++t) stepWorld(w, cfg, dev::data().table, sc);
            if (run == 0) first = w.checksum();
            else CHECK_EQU(w.checksum(), first);
        }
        printf("    같은 시드 3회 × 600틱 일치 (%llu)\n",
               static_cast<unsigned long long>(first));
    }

    dctest::section("전설 풀 9칸이 **전부 효과를 갖는다**");
    {
        // 수치가 비면 효과가 조용히 0이 된다. 먼저 값이 살아 있는지 본다.
        CHECK(cfg.vortexDurationTicks > 0);
        CHECK(cfg.vortexDpsPermille > 0);
        CHECK(cfg.aftershockDamagePermille > 0);
        CHECK(cfg.aftershockFuseTicks > 0);
        CHECK(cfg.fissureSlowPermille > 0);
        CHECK(cfg.fissureRadius.raw > 0);
        CHECK(cfg.fissureDurationTicks > 0);

        // **모든 각인이 자기 스킬에서 무언가를 바꾼다.** 한 칸이라도 효과가
        // 0이면 전설 기대값이 그만큼 낮아지는데, 증상은 "전설이 시원찮다"뿐이라
        // 눈으로는 안 보인다. 그래서 칸마다 "들면 결과가 달라진다"를 직접 본다.
        //
        // **틱 루프로 재면 안 된다.** 전설을 쥐면 그 칸이 카드 풀에서 빠져
        // (§4 중복 규칙) 추첨이 달라지고 런 전체가 갈라진다. 스킬 실행 한 번만
        // 떼어 비교한다.
        for (uint32_t u = legendIndexOf(LegendId::UniqueFirst); u < 9; ++u) {
            const uint32_t sk = static_cast<uint32_t>(cfg.uniqueSkill[u - 3]);
            int64_t sig[2] = {0, 0};
            for (int32_t k = 0; k < 2; ++k) {
                World w = makeWorld(888);
                w.hero.posX = Fixed{}; w.hero.posY = Fixed{};
                if (k == 1) w.cards.legendTake(u);
                // 앞뒤·옆으로 흩뿌려 단일기·광역기 양쪽에 걸리게 한다.
                // 체력은 처형 임계에 걸릴 만큼 낮게 둔다 (처형 칸도 보려면 필요)
                for (int32_t d = 1; d <= 5; ++d) {
                    w.entities.spawn(mob(Fixed::fromPermille(d * 400), Fixed{}, 40), 0, 888);
                    w.entities.spawn(mob(Fixed::fromPermille(d * 400),
                                         Fixed::fromPermille(300), 40), 0, 888);
                }
                w.hero.target = w.entities.idAt(0);
                executeSkill(w, cfg, sk, QteGrade::Miss);
                // 피해 + 장판 상태를 함께 본다 — 균열은 피해가 0이고 소용돌이는
                // 즉발이 0이므로 피해만 보면 둘 다 "효과 없음"으로 읽힌다
                for (uint32_t i = 0; i < w.entities.count(); ++i) {
                    sig[k] += w.entities.damageTaken[i].raw;
                    sig[k] += w.entities.deadAt(i) ? 1 : 0;
                }
                sig[k] += static_cast<int64_t>(w.zones.count) * 1000003;
            }
            CHECK(sig[0] != sig[1]);
        }
        printf("    고유 각인 6칸이 각자 지정된 스킬에서 결과를 바꾼다\n");
    }

    return dctest::summary("test_legend");
}
