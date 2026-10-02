// QTE 테스트 (§3).
//
// 두 종류는 **실패의 의미가 정반대**라 검증도 갈린다:
//   스킬 증폭 — 실패해도 페널티 없음. 보너스만 사라진다
//   위기 회피 — 실패하면 확정 피격. QTE가 회피 판정 그 자체다
//
// 그리고 시뮬이 20Hz라 **판정 해상도의 하한이 50ms**다. 등급은 프레젠테이션이
// 매기고 시뮬은 틱으로 검증만 한다 — 그 검증이 실제로 도는지가 핵심이다.
#include <initializer_list>

#include "../include/dc/sim.h"
#include "../tools/data_files.h"
#include "test_main.h"

using namespace dc;

static World makeWorld(uint64_t seed = 1) {
    World w;
    initWorld(w, seed, dev::data().cfg, dev::data().table, dev::data().hero);
    return w;
}

// **hp는 Fixed(20.12) 범위 안이어야 한다** (±524,287). 1000000을 쓰면 UBSan이
// `1000000 * 4096`에서 부호 있는 오버플로를 잡는다 — "안 죽는 샌드백"을 원해서
// 큰 값을 적고 싶어지는 자리다.
static SpawnDesc mob(Archetype a, int32_t hp, int32_t windup, int32_t dmg) {
    SpawnDesc d;
    d.posX = Fixed(1);
    d.maxHp = Fixed(hp);
    d.archetype = a;
    d.attackRange = Fixed(2);
    d.attackDamage = Fixed(dmg);
    d.windupTicks = windup;
    d.ccGaugeMax = Fixed(100);
    d.targetPriority = (a == Archetype::Trash) ? 0 : 35;
    return d;
}

int main() {
    printf("test_qte\n");
    const SimConfig& cfg = dev::data().cfg;

    dctest::section("판정 검증 — 시뮬은 등급을 믿지 않는다");
    {
        QteWindow q;
        q.kind = QteKind::SkillAmplify;
        q.openTick = 100; q.closeTick = 108;
        q.perfectFrom = 106; q.perfectTo = 107;   // 입력 가능한 마지막 틱은 107

        // 창 밖은 무조건 Miss — 프레젠테이션이 뭘 보내든.
        CHECK_EQ(static_cast<int32_t>(q.judge(99, QteGrade::Perfect)),
                 static_cast<int32_t>(QteGrade::Miss));
        // **closeTick에는 이미 판정이 끝나 있다** — 그 틱의 입력은 늦은 것이다.
        CHECK_EQ(static_cast<int32_t>(q.judge(108, QteGrade::Perfect)),
                 static_cast<int32_t>(QteGrade::Miss));

        // 창 안이지만 완벽 구간 밖 → **Success로 강등**한다.
        CHECK_EQ(static_cast<int32_t>(q.judge(103, QteGrade::Perfect)),
                 static_cast<int32_t>(QteGrade::Success));
        // 완벽 구간 안이면 그대로 통과
        CHECK_EQ(static_cast<int32_t>(q.judge(106, QteGrade::Perfect)),
                 static_cast<int32_t>(QteGrade::Perfect));
        CHECK_EQ(static_cast<int32_t>(q.judge(107, QteGrade::Perfect)),
                 static_cast<int32_t>(QteGrade::Perfect));
        // Success 주장은 창 안이면 그대로
        CHECK_EQ(static_cast<int32_t>(q.judge(101, QteGrade::Success)),
                 static_cast<int32_t>(QteGrade::Success));

        // 닫힌 창은 아무것도 받지 않는다
        QteWindow closed;
        CHECK_EQ(static_cast<int32_t>(closed.judge(0, QteGrade::Perfect)),
                 static_cast<int32_t>(QteGrade::Miss));
    }

    dctest::section("스킬 증폭 — 등급이 위력을 바꾸되 실패에 페널티는 없다");
    {
        auto play = [&](bool sendInput, QteGrade grade) {
            World w = makeWorld(7);
            SpawnDesc d = mob(Archetype::Trash, 100000, 0, 0);   // 죽지 않는 표적
            const EntityId t = w.entities.spawn(d, 0, 7);
            w.hero.target = t;
            SimScratch sc;
            for (int i = 0; i < 600; ++i) {
                if (sendInput && w.hero.qte.open()
                    && w.hero.qte.kind == QteKind::SkillAmplify
                    && w.tickCount() == w.hero.qte.perfectTo) {
                    InputEvent e; e.tick = w.tickCount();
                    e.kind = InputKind::QteGrade; e.value = static_cast<uint32_t>(grade);
                    (void)w.applyInput(e);
                }
                stepWorld(w, cfg, dev::data().table, sc);
            }
            const int32_t dense = w.entities.denseOf(t);
            return w.entities.damageTaken[static_cast<uint32_t>(dense)].raw;
        };
        const int32_t miss    = play(false, QteGrade::Miss);
        const int32_t success = play(true,  QteGrade::Success);
        const int32_t perfect = play(true,  QteGrade::Perfect);
        printf("    600틱 누적 피해  실패 %d · 성공 %d · 완벽 %d raw\n", miss, success, perfect);

        // **실패는 페널티가 아니라 보너스 없음이다** — 기본 위력은 그대로 들어간다.
        CHECK(miss > 0);
        CHECK(success > miss);
        CHECK(perfect > success);
        // 완벽이 실패 대비 총 피해를 2배 이상 올리면 실력이 빌드를 압도한다.
        CHECK(perfect < miss * 2);
    }

    dctest::section("위기 회피 — 실패하면 확정 피격");
    {
        auto play = [&](bool sendInput, QteGrade grade) {
            World w = makeWorld(11);
            const EntityId e = w.entities.spawn(mob(Archetype::Elite, 100000, 60, 167), 0, 11);
            w.hero.target = e;
            SimScratch sc;
            int32_t resolved = 0;
            for (int i = 0; i < 400; ++i) {
                if (w.hero.qte.open() && w.hero.qte.kind == QteKind::CrisisEvade
                    && w.tickCount() == w.hero.qte.perfectTo) {
                    if (sendInput) {
                        InputEvent ev; ev.tick = w.tickCount();
                        ev.kind = InputKind::QteGrade; ev.value = static_cast<uint32_t>(grade);
                        (void)w.applyInput(ev);
                    }
                    ++resolved;
                }
                stepWorld(w, cfg, dev::data().table, sc);
            }
            struct R { int32_t corruption, cc, groggy, count; };
            const uint32_t d = static_cast<uint32_t>(w.entities.denseOf(e));
            return R{w.hero.corruption.raw, w.entities.ccTriggerCount[d],
                     w.entities.groggyLeft[d], resolved};
        };
        const auto miss    = play(false, QteGrade::Miss);
        const auto success = play(true,  QteGrade::Success);
        const auto perfect = play(true,  QteGrade::Perfect);
        printf("    400틱  실패 잠식 %d · 성공 %d · 완벽 %d (그로기 %d회)\n",
               miss.corruption, success.corruption, perfect.corruption, perfect.cc);

        CHECK(miss.count > 0);
        CHECK(miss.corruption > 0);                 // 실패 → 확정 피격
        CHECK(success.corruption < miss.corruption);// 성공 → 무피해
        CHECK(perfect.corruption < miss.corruption);
        // 완벽 → CC 게이지 만충 → 그로기 (§3 완벽 판정의 보상)
        CHECK(perfect.cc > 0);
        // **"성공은 CC를 올리지 않는다"를 여기서 0으로 볼 수 없게 됐다.** CC 출처가
        // 둘이 되면서(위기 회피 완벽 + CC 스킬) 400틱 안에 대지 가르기가 끼어든다.
        // 등급 차이만 보려면 **완벽이 성공보다 더 올린다**를 봐야 한다 — QTE 경로
        // 자체는 아래 전용 절이 격리해서 본다.
        CHECK(perfect.cc > success.cc);
    }

    dctest::section("등급별 CC — **성공은 올리지 않고 완벽만 올린다** (스킬 격리)");
    {
        // 위 절은 전투를 통째로 돌리므로 CC 스킬이 섞인다. 여기서는 `qteRun`이
        // 쓰는 경로만 직접 불러 등급 규칙을 격리한다.
        for (QteGrade g : {QteGrade::Miss, QteGrade::Success, QteGrade::Perfect}) {
            World w = makeWorld(10);
            const EntityId e = w.entities.spawn(mob(Archetype::Elite, 100000, 0, 167), 0, 10);
            const uint32_t d = static_cast<uint32_t>(w.entities.denseOf(e));
            (void)applyCc(w, cfg, d, g == QteGrade::Perfect ? cfg.qtePerfectCcGainPermille : 0);
            CHECK_EQ(w.entities.ccTriggerCount[d], g == QteGrade::Perfect ? 1 : 0);
        }
    }

    dctest::section("쿨다운 — 위기 회피는 막지 않는다");
    {
        // 놓치면 확정 피격인 QTE를 쿨다운으로 막으면 플레이어가 통제할 수 없는
        // 이유로 맞게 된다. 스킬 증폭만 막고 위기 회피는 통과시킨다.
        World w = makeWorld(3);
        w.hero.qteCooldown = 999;
        const EntityId e = w.entities.spawn(mob(Archetype::Elite, 100000, 60, 167), 0, 3);
        w.hero.target = e;
        SimScratch sc;
        bool sawCrisis = false, sawSkill = false;
        for (int i = 0; i < 400; ++i) {
            stepWorld(w, cfg, dev::data().table, sc);
            if (w.hero.qte.kind == QteKind::CrisisEvade)  sawCrisis = true;
            if (w.hero.qte.kind == QteKind::SkillAmplify) sawSkill = true;
        }
        CHECK(sawCrisis);
        (void)sawSkill;

        // **`!sawSkill`을 보던 자리다. 규칙이 아니라 우연이었다** — 위기 회피가
        // 열릴 때 쿨다운을 `qteCooldownTicks`(120)로 **되돌리므로**, 처음에 999를
        // 꽂아도 400틱 안에 스킬 증폭이 열릴 수 있다. 전에는 그 시퀀스가 어쩌다
        // 안 겹쳤을 뿐이고, 광역 중심이 바뀌어 전투 리듬이 흔들리자 깨졌다.
        //
        // 규칙 자체를 직접 본다: 쿨다운이 남아 있으면 스킬 증폭은 거부되고,
        // 위기 회피는 그와 무관하게 열린다.
        World w2 = makeWorld(31);
        w2.hero.qteCooldown = 50;
        CHECK(!openSkillQte(w2, cfg, 0));                  // 쿨다운 중 — 거부
        CHECK(!w2.hero.qte.open());
        w2.hero.qteCooldown = 0;
        CHECK(openSkillQte(w2, cfg, 0));                   // 풀리면 열린다
        CHECK_EQ(static_cast<int32_t>(w2.hero.qte.kind),
                 static_cast<int32_t>(QteKind::SkillAmplify));
        // 그리고 열자마자 쿨다운이 다시 채워진다 — 빈도 상한이 여기서 걸린다
        CHECK_EQ(w2.hero.qteCooldown, cfg.qteCooldownTicks);
    }

    dctest::section("그로기 — 행동 불가");
    {
        World w = makeWorld(5);
        const EntityId e = w.entities.spawn(mob(Archetype::Elite, 100000, 0, 167), 0, 5);
        const uint32_t d = static_cast<uint32_t>(w.entities.denseOf(e));
        w.entities.groggyLeft[d] = 20;
        w.hero.target = e;
        SimScratch sc;
        const int32_t before = w.hero.corruption.raw;
        for (int i = 0; i < 20; ++i) stepWorld(w, cfg, dev::data().table, sc);
        CHECK_EQ(w.hero.corruption.raw, before);   // 그로기 동안 때리지 못한다
        for (int i = 0; i < 60; ++i) stepWorld(w, cfg, dev::data().table, sc);
        CHECK(w.hero.corruption.raw > before);     // 풀리면 다시 때린다
    }

    dctest::section("그로기 — **플래그가 내려간다** · 이동도 멈춘다");
    {
        // 전에는 플래그를 올리기만 하고 내리지 않아서, 한 번 기절한 적이
        // `groggyLeft`가 0으로 돌아간 뒤에도 영원히 Groggy로 표시됐다. 아무도
        // 플래그를 읽지 않아 증상이 없었지만 **체크섬에 들어가는 상태**였다.
        World w = makeWorld(6);
        const EntityId e = w.entities.spawn(mob(Archetype::Elite, 100000, 0, 167), 0, 6);
        const uint32_t d = static_cast<uint32_t>(w.entities.denseOf(e));
        w.entities.groggyLeft[d] = 5;
        w.entities.flags[d] = static_cast<uint8_t>(w.entities.flags[d] | EntityFlag::Groggy);
        w.hero.target = e;
        SimScratch sc;

        // **멈춤 거리 밖에 놓는다.** mob()의 기본 위치(1.0타일)는 사거리 2.0에서
        // 접근 여유를 뺀 1.6보다 안쪽이라 그로기와 무관하게 움직이지 않는다 —
        // 그 상태로는 "이동이 멈췄다"를 증명하지 못한다.
        w.entities.posX[d] = Fixed(8);
        // mob()은 approachSpeed를 세우지 않는다 (0이면 movementRun이 건너뛴다).
        // 이동이 멈추는 것을 보려면 먼저 **움직일 수 있어야** 한다.
        w.entities.approachSpeed[d] = Fixed(2);
        const int32_t x0 = w.entities.posX[d].raw;
        for (int i = 0; i < 4; ++i) stepWorld(w, cfg, dev::data().table, sc);
        CHECK_EQ(w.entities.posX[d].raw, x0);
        CHECK((w.entities.flags[d] & EntityFlag::Groggy) != 0);

        stepWorld(w, cfg, dev::data().table, sc);          // 마지막 틱에 풀린다
        CHECK_EQ(w.entities.groggyLeft[d], 0);
        CHECK((w.entities.flags[d] & EntityFlag::Groggy) == 0);
        // 풀리면 다시 움직인다
        for (int i = 0; i < 5; ++i) stepWorld(w, cfg, dev::data().table, sc);
        CHECK(w.entities.posX[d].raw != x0);
    }

    dctest::section("CC 스킬 — **잡몹은 즉시, 엘리트는 게이지**");
    {
        // 분기는 archetype이 아니라 **게이지 유무**다 (§4). 그래야 "일반 몹에게는
        // 무조건 걸린다"가 데이터로 성립하고, 게이지를 가진 잡몹을 나중에
        // 만들어도 코어를 고치지 않는다.
        World w = makeWorld(7);
        SpawnDesc t = mob(Archetype::Trash, 100000, 0, 5);
        t.ccGaugeMax = Fixed{};                 // 잡몹은 게이지가 없다
        const EntityId tr = w.entities.spawn(t, 0, 7);
        const EntityId el = w.entities.spawn(mob(Archetype::Elite, 100000, 0, 167), 0, 7);
        const uint32_t ti = static_cast<uint32_t>(w.entities.denseOf(tr));
        const uint32_t ei = static_cast<uint32_t>(w.entities.denseOf(el));

        // 잡몹 — 1회로 즉시 기절. 게이지는 건드리지 않는다
        CHECK(applyCc(w, cfg, ti, 1000));
        CHECK_EQ(w.entities.groggyLeft[ti], cfg.groggyTicks);
        CHECK_EQ(w.entities.ccGauge[ti].raw, 0);

        // 엘리트 — 절반만 채우면 아직 아니다
        CHECK(!applyCc(w, cfg, ei, 500));
        CHECK_EQ(w.entities.groggyLeft[ei], 0);
        CHECK(w.entities.ccGauge[ei].raw > 0);
        // 남은 절반이 들어오면 발동하고 리셋된다
        CHECK(applyCc(w, cfg, ei, 500));
        CHECK_EQ(w.entities.groggyLeft[ei], cfg.groggyTicks);
        CHECK_EQ(w.entities.ccGauge[ei].raw, 0);
        CHECK_EQ(w.entities.ccTriggerCount[ei], 1);
        printf("    잡몹 즉시 · 엘리트 1000‰ 누적에 발동 (기준 게이지 %d)\n",
               toInt(w.entities.ccGaugeMax[ei]));
    }

    dctest::section("CC 스킬 — **엘리트는 고정 임계, 보스는 상승**");
    {
        // 엘리트는 CC 빌드로 계속 묶어둘 수 있는 중간 위협이고, 보스는 초반엔
        // 먹히다가 후반엔 안 먹혀 결국 화력으로 승부해야 한다 (§4).
        World w = makeWorld(8);
        const EntityId el = w.entities.spawn(mob(Archetype::Elite, 100000, 0, 167), 0, 8);
        SpawnDesc b = mob(Archetype::Boss, 100000, 0, 300);
        b.archetype = Archetype::Boss;
        const EntityId bs = w.entities.spawn(b, 0, 8);
        const uint32_t ei = static_cast<uint32_t>(w.entities.denseOf(el));
        const uint32_t bi = static_cast<uint32_t>(w.entities.denseOf(bs));

        // 엘리트 — 매번 1000‰에 발동한다
        for (int32_t k = 1; k <= 3; ++k) {
            CHECK(applyCc(w, cfg, ei, 1000));
            CHECK_EQ(w.entities.ccTriggerCount[ei], k);
        }
        // 보스 — 저항 상승 1000‰이면 n번째 발동에 n회분이 든다 → 누적 1·3·6
        int32_t applied = 0, triggers = 0;
        for (int32_t k = 0; k < 6; ++k) {
            ++applied;
            if (applyCc(w, cfg, bi, 1000)) ++triggers;
        }
        CHECK_EQ(applied, 6);
        CHECK_EQ(triggers, 3);                       // 1·3·6회째
        CHECK_EQ(w.entities.ccTriggerCount[bi], 3);
        printf("    엘리트 3/3회 발동 · 보스 6회 적용에 %d회 발동 (누적 1·3·6)\n",
               triggers);
    }

    dctest::section("CC 스킬 — 대지 가르기가 **데이터로** CC 스킬이다");
    {
        // 코어는 "어느 스킬이 CC인지" 모른다. cc_gain_permille > 0인 스킬이 CC다.
        int32_t ccSkills = 0;
        for (uint32_t k = 0; k < cfg.skillCount; ++k) {
            if (cfg.skills[k].ccGainPermille > 0) ++ccSkills;
        }
        CHECK_EQ(ccSkills, 1);                       // 전사는 대지 가르기 하나
        // 그 스킬은 광역기이고, QTE 완벽 판정과 **같은 단위**를 쓴다
        for (uint32_t k = 0; k < cfg.skillCount; ++k) {
            if (cfg.skills[k].ccGainPermille <= 0) continue;
            CHECK(cfg.skills[k].aoe);
            CHECK_EQ(cfg.skills[k].ccGainPermille, cfg.qtePerfectCcGainPermille);
        }

        // 실제로 스킬을 쏘면 범위 안 적이 기절한다
        World w = makeWorld(9);
        w.hero.posX = Fixed{}; w.hero.posY = Fixed{};
        uint32_t cleave = 0;
        for (uint32_t k = 0; k < cfg.skillCount; ++k) {
            if (cfg.skills[k].ccGainPermille > 0) { cleave = k; break; }
        }
        SpawnDesc t = mob(Archetype::Trash, 100000, 0, 5);
        t.posX = Fixed::fromPermille(500);
        t.ccGaugeMax = Fixed{};
        const EntityId tr = w.entities.spawn(t, 0, 9);
        const uint32_t ti = static_cast<uint32_t>(w.entities.denseOf(tr));
        executeSkill(w, cfg, cleave, QteGrade::Miss);
        CHECK_EQ(w.entities.groggyLeft[ti], cfg.groggyTicks);

        // 다른 스킬은 기절시키지 않는다
        for (uint32_t k = 0; k < cfg.skillCount; ++k) {
            if (k == cleave) continue;
            World w2 = makeWorld(9);
            w2.hero.posX = Fixed{}; w2.hero.posY = Fixed{};
            const EntityId t2 = w2.entities.spawn(t, 0, 9);
            const uint32_t i2 = static_cast<uint32_t>(w2.entities.denseOf(t2));
            w2.hero.target = t2;
            executeSkill(w2, cfg, k, QteGrade::Miss);
            CHECK_EQ(w2.entities.groggyLeft[i2], 0);
        }
        printf("    대지 가르기만 기절시킨다 (CC 스킬 %d종)\n", ccSkills);
    }

    dctest::section("광역 중심 — **전방 범위는 영웅 중심이 아니다**");
    {
        // 설계가 회전 베기를 "주변 전방위", 대지 가르기를 "전방 범위"로 정했는데
        // 코어는 둘 다 영웅 중심으로 돌렸다. 그래서 대지 가르기가 자기 타겟을
        // 때릴 수 없었다 — 영웅은 타겟에서 2.6타일 앞에 멈추고 반경은 1.5다.
        World w = makeWorld(11);
        w.hero.posX = Fixed{}; w.hero.posY = Fixed{};
        const Fixed r = Fixed::fromPermille(1500);

        // 타겟을 +x 방향 멀리 둔다
        SpawnDesc t = mob(Archetype::Elite, 100000, 0, 167);
        t.posX = Fixed(4); t.posY = Fixed{};
        const EntityId e = w.entities.spawn(t, 0, 11);
        w.hero.target = e;

        for (uint32_t k = 0; k < cfg.skillCount; ++k) {
            Fixed cx{}, cy{};
            aoeCenterOf(w, cfg.skills[k], r, &cx, &cy);
            if (cfg.skills[k].center == AoeCenter::Hero) {
                CHECK_EQ(cx.raw, w.hero.posX.raw);
                CHECK_EQ(cy.raw, w.hero.posY.raw);
            } else {
                // **타겟 방향으로 정확히 반경만큼** 앞 → 닿는 거리 2 × 반경
                CHECK_EQ(cx.raw, r.raw);
                CHECK_EQ(cy.raw, 0);
            }
        }

        // 전방 중심을 쓰는 스킬이 정확히 하나이고, 그게 CC 스킬이다
        uint32_t fwd = 0, fwdIdx = 0;
        for (uint32_t k = 0; k < cfg.skillCount; ++k) {
            if (cfg.skills[k].center == AoeCenter::Forward) { ++fwd; fwdIdx = k; }
        }
        CHECK_EQ(fwd, 1u);
        CHECK(cfg.skills[fwdIdx].aoe);                       // 전방 "범위"다
        CHECK(cfg.skills[fwdIdx].ccGainPermille > 0);        // 그게 CC 스킬이다
        printf("    전방 중심 1종 · 중심 오프셋 = 반경 %d raw → 닿는 거리 %d raw\n",
               r.raw, r.raw * 2);
    }

    dctest::section("광역 중심 — 타겟이 없으면 **영웅 중심으로 되돌아간다**");
    {
        // 방향을 정할 수 없다. 조용히 영웅 앞 어딘가를 고르면 그게 숨은 규칙이 된다.
        World w = makeWorld(12);
        w.hero.posX = Fixed(5); w.hero.posY = Fixed(7);
        const Fixed r = Fixed::fromPermille(1500);
        for (uint32_t k = 0; k < cfg.skillCount; ++k) {
            Fixed cx{}, cy{};
            aoeCenterOf(w, cfg.skills[k], r, &cx, &cy);   // 타겟 없음
            CHECK_EQ(cx.raw, w.hero.posX.raw);
            CHECK_EQ(cy.raw, w.hero.posY.raw);
        }
        // 타겟이 영웅과 겹쳐도 방향이 없다 → 영웅 중심
        SpawnDesc t = mob(Archetype::Elite, 100000, 0, 167);
        t.posX = w.hero.posX; t.posY = w.hero.posY;
        const EntityId e = w.entities.spawn(t, 0, 12);
        w.hero.target = e;
        for (uint32_t k = 0; k < cfg.skillCount; ++k) {
            Fixed cx{}, cy{};
            aoeCenterOf(w, cfg.skills[k], r, &cx, &cy);
            CHECK_EQ(cx.raw, w.hero.posX.raw);
            CHECK_EQ(cy.raw, w.hero.posY.raw);
        }
    }

    dctest::section("광역 중심 — **전방 광역이 영웅의 타겟에 닿는다** (CC까지)");
    {
        // 이게 이번 변경의 요점이다. 영웅이 멈추는 거리(사거리 − 접근 여유)에 있는
        // 타겟을 전방 광역이 때릴 수 있어야 한다.
        const Fixed range = dev::data().hero.bases[static_cast<uint32_t>(Stat::Range)];
        const Fixed stop = range - Fixed::fromPermille(cfg.approachMarginMilli);
        const Fixed aoe = dev::data().hero.bases[static_cast<uint32_t>(Stat::AoeRadius)];
        // 전제: 영웅 중심으로는 닿지 않고, 전방 중심(2 × 반경)으로는 닿는다
        CHECK(stop.raw > aoe.raw);
        CHECK(stop.raw <= aoe.raw * 2);

        uint32_t cleave = 0;
        for (uint32_t k = 0; k < cfg.skillCount; ++k) {
            if (cfg.skills[k].center == AoeCenter::Forward) { cleave = k; break; }
        }
        int32_t hit[2] = {0, 0};
        for (int32_t k = 0; k < 2; ++k) {
            SimConfig c = cfg;
            c.skills[cleave].center = k == 0 ? AoeCenter::Hero : AoeCenter::Forward;
            World w = makeWorld(13);
            w.hero.posX = Fixed{}; w.hero.posY = Fixed{};
            SpawnDesc t = mob(Archetype::Elite, 100000, 0, 167);
            t.posX = stop; t.posY = Fixed{};
            const EntityId e = w.entities.spawn(t, 0, 13);
            const uint32_t i = static_cast<uint32_t>(w.entities.denseOf(e));
            w.hero.target = e;
            executeSkill(w, c, cleave, QteGrade::Miss);
            hit[k] = w.entities.damageTaken[i].raw;
            // CC도 같이 간다 — 게이지가 찼으면 기절, 아니면 게이지만
            if (k == 1) CHECK(w.entities.ccGauge[i].raw > 0 || w.entities.groggyLeft[i] > 0);
        }
        CHECK_EQ(hit[0], 0);        // 영웅 중심 — 멈춤 거리의 타겟에 닿지 않는다
        CHECK(hit[1] > 0);          // 전방 중심 — 닿는다
        printf("    멈춤 거리 %d raw 타겟: 영웅 중심 %d raw · 전방 중심 %d raw\n",
               stop.raw, hit[0], hit[1]);
    }

    dctest::section("입력 로그 — 모든 입력이 한 형식");
    {
        InputLog log;
        InputEvent a{10, InputKind::QteGrade, 2};
        InputEvent b{10, InputKind::ManualTarget, 0x1001};
        InputEvent c{25, InputKind::QteGrade, 1};
        CHECK(log.record(a));
        CHECK(log.record(b));     // 같은 틱 여러 개 허용
        CHECK(log.record(c));
        CHECK_EQ(log.count(), 3u);

        // **틱 역행은 거부한다** — 재생이 커서 하나로 도는 전제가 깨진다.
        CHECK(!log.record(InputEvent{5, InputKind::QteGrade, 0}));

        InputEvent out;
        log.rewind(0);
        CHECK(log.next(10, &out) && out.value == 2u);
        CHECK(log.next(10, &out) && out.value == 0x1001u);
        CHECK(!log.next(10, &out));
        CHECK(!log.next(11, &out));
        CHECK(log.next(25, &out) && out.value == 1u);

        // 되감기 — 스냅샷 로드 후 같은 틱부터 다시 먹인다.
        log.rewind(25);
        CHECK(log.next(25, &out) && out.value == 1u);

        InputLog same;
        same.record(a); same.record(b); same.record(c);
        CHECK_EQU(log.hash(), same.hash());
        InputLog diff;
        diff.record(a); diff.record(b);
        CHECK(diff.hash() != log.hash());
    }

    dctest::section("입력은 상태가 아니다 — 같은 로그면 같은 체크섬");
    {
        // 서버 리플레이 검증의 실체. 로그를 그대로 재생하면 체크섬이 일치해야 한다.
        auto play = [&](InputLog& log, bool record) {
            World w = makeWorld(20250921);
            SimScratch sc;
            Rng r = Rng::derive(99, RngStream::Events);
            if (!record) log.rewind(0);
            for (int i = 0; i < 1500; ++i) {
                if (record) {
                    if (w.hero.qte.open() && w.tickCount() == w.hero.qte.perfectTo) {
                        InputEvent e;
                        e.tick  = w.tickCount();
                        e.kind  = InputKind::QteGrade;
                        e.value = r.range(3);
                        if (w.applyInput(e)) (void)log.record(e);
                    }
                } else {
                    InputEvent e;
                    while (log.next(w.tickCount(), &e)) (void)w.applyInput(e);
                }
                stepWorld(w, cfg, dev::data().table, sc);
            }
            return w.checksum();
        };
        static InputLog log;
        const uint64_t live   = play(log, true);
        const uint64_t replay = play(log, false);
        printf("    입력 %u개 기록 후 재생 — 체크섬 %s\n", log.count(),
               live == replay ? "일치" : "불일치");
        CHECK(log.count() > 0);
        CHECK_EQU(live, replay);
    }

    dctest::section("QTE는 [상태] — 창이 체크섬에 들어간다");
    {
        World a = makeWorld(4), b = makeWorld(4);
        CHECK_EQU(a.checksum(), b.checksum());
        a.hero.qte.kind = QteKind::CrisisEvade;
        a.hero.qte.closeTick = 50;
        CHECK(a.checksum() != b.checksum());

        // 등급 입력도 상태다 — 빠지면 서버가 다른 결과를 낸다.
        World c = makeWorld(4), d = makeWorld(4);
        c.hero.qte.kind = QteKind::SkillAmplify; c.hero.qte.closeTick = 50;
        d.hero.qte.kind = QteKind::SkillAmplify; d.hero.qte.closeTick = 50;
        CHECK_EQU(c.checksum(), d.checksum());
        c.hero.qte.input = QteGrade::Perfect; c.hero.qte.hasInput = 1;
        CHECK(c.checksum() != d.checksum());
    }

    dctest::section("QTE 빈도 — 구간 예산 3~5회");
    {
        // 실제 시뮬에서 구간 하나 동안 QTE가 몇 번 열리는지 센다.
        World w = makeWorld(20250921);
        SimScratch sc;
        int32_t skill = 0, crisis = 0;
        QteKind last = QteKind::None;
        const int32_t SEGMENT_TICKS = 500;    // 구간 1 목표
        for (int32_t i = 0; i < SEGMENT_TICKS; ++i) {
            stepWorld(w, cfg, dev::data().table, sc);
            if (w.hero.qte.kind != last && w.hero.qte.open()) {
                if (w.hero.qte.kind == QteKind::SkillAmplify) ++skill; else ++crisis;
            }
            last = w.hero.qte.kind;
        }
        printf("    구간 1(%d틱) — 스킬 증폭 %d회 · 위기 회피 %d회 · 합 %d회\n",
               SEGMENT_TICKS, skill, crisis, skill + crisis);
        // 쿨다운 6초가 상한을 잡으므로 25초 구간에서 4회를 넘을 수 없다.
        CHECK(skill + crisis <= SEGMENT_TICKS / cfg.qteCooldownTicks + 1);
    }

    return dctest::summary("test_qte");
}
