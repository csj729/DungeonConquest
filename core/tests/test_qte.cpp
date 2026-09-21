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
#include "../tools/dev_data.h"
#include "test_main.h"

using namespace dc;

static World makeWorld(uint64_t seed = 1) {
    World w;
    w.init(seed);
    dev::applyHeroBaseline(w);
    return w;
}

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
    const SimConfig cfg = dev::devConfig();

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
                stepWorld(w, cfg, sc);
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
                stepWorld(w, cfg, sc);
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
        CHECK_EQ(success.cc, 0);
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
            stepWorld(w, cfg, sc);
            if (w.hero.qte.kind == QteKind::CrisisEvade)  sawCrisis = true;
            if (w.hero.qte.kind == QteKind::SkillAmplify) sawSkill = true;
        }
        CHECK(sawCrisis);
        CHECK(!sawSkill);      // 쿨다운이 계속 남아 스킬 증폭은 한 번도 안 열린다
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
        for (int i = 0; i < 20; ++i) stepWorld(w, cfg, sc);
        CHECK_EQ(w.hero.corruption.raw, before);   // 그로기 동안 때리지 못한다
        for (int i = 0; i < 60; ++i) stepWorld(w, cfg, sc);
        CHECK(w.hero.corruption.raw > before);     // 풀리면 다시 때린다
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
                stepWorld(w, cfg, sc);
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
            stepWorld(w, cfg, sc);
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
