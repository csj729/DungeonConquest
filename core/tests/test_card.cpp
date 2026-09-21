// 레벨업 카드 테스트 (§4).
//
// **추첨이 곧 빌드다.** 등급 확률이 틀리면 성장 곡선이 통째로 밀리고,
// 비복원이 깨지면 선택지가 줄고, 전설 풀이 새면 잭팟이 잭팟이 아니게 된다.
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

int main() {
    printf("test_card\n");
    const SimConfig cfg = dev::devConfig();

    dctest::section("등급 확률 — data/cards.json과 일치");
    {
        World w = makeWorld(20250921);
        const int TRIALS = 200000;
        int32_t count[MAX_CARD_GRADES] = {0, 0, 0, 0, 0};
        for (int i = 0; i < TRIALS; ++i) {
            // 전설 풀 고갈 폴백이 끼지 않도록 매번 초기화한다
            w.cards.legendTaken = 0;
            ++count[rollGrade(w, cfg)];
        }
        const char* names[5] = {"일반", "고급", "희귀", "영웅", "전설"};
        for (uint32_t g = 0; g < MAX_CARD_GRADES; ++g) {
            const int32_t got  = count[g] * 1000 / TRIALS;
            const int32_t want = cfg.cardGradeRate[g];
            printf("    %s 실측 %4dpermille (목표 %4d)\n", names[g], got, want);
            CHECK(got >= want - 8 && got <= want + 8);
        }
    }

    dctest::section("같은 화면 중복 금지 — 비복원 추출");
    {
        World w = makeWorld(3);
        int bad = 0;
        for (int round = 0; round < 20000; ++round) {
            dealCards(w, cfg);
            const CardOfferSet& o = w.cards.offer;
            for (uint8_t i = 0; i < o.count; ++i) {
                for (uint8_t j = static_cast<uint8_t>(i + 1); j < o.count; ++j) {
                    // 스탯 카드는 종류가 하나라 중복이 허용된다 — 나머지는 금지.
                    if (o.offers[i].kind == CardKind::StatBoost) continue;
                    if (o.offers[i].kind == o.offers[j].kind
                        && o.offers[i].entryId == o.offers[j].entryId) ++bad;
                }
            }
        }
        CHECK_EQ(bad, 0);
        printf("    2만 화면 × %u장 — 중복 %d건\n", cfg.cardsPerLevel, bad);
    }

    dctest::section("전설 풀 — 획득하면 다시 나오지 않는다");
    {
        World w = makeWorld(7);
        // 전설만 나오도록 강제한다.
        SimConfig legendOnly = cfg;
        for (uint32_t g = 0; g < MAX_CARD_GRADES; ++g) legendOnly.cardGradeRate[g] = 0;
        legendOnly.cardGradeRate[4] = 1000;
        legendOnly.cardsPerLevel = 1;

        bool seen[MAX_LEGEND_POOL] = {false};
        for (uint32_t k = 0; k < cfg.legendPoolSize; ++k) {
            dealCards(w, legendOnly);
            CHECK_EQ(w.cards.offer.count, 1u);
            const CardOffer& c = w.cards.offer.offers[0];
            CHECK_EQ(static_cast<int32_t>(c.kind), static_cast<int32_t>(CardKind::Legend));
            CHECK(!seen[c.entryId]);          // **같은 전설이 두 번 나오지 않는다**
            seen[c.entryId] = true;
            w.cards.pendingLevelUps = 1;
            CHECK(chooseCard(w, legendOnly, 0));
        }
        CHECK_EQ(w.cards.legendLeft(cfg.legendPoolSize), 0u);
        printf("    전설 %u종 전부 획득 — 중복 0\n", cfg.legendPoolSize);

        // **고갈 폴백** — 풀이 비면 등급이 한 단계 내려간다 (§4).
        // 규칙이 없으면 이 판에서 터진다.
        dealCards(w, legendOnly);
        CHECK_EQ(w.cards.offer.count, 1u);
        CHECK(w.cards.offer.offers[0].kind != CardKind::Legend);
        CHECK_EQ(w.cards.offer.offers[0].grade, 3u);   // 영웅으로 강등
    }

    dctest::section("등급은 종류가 아니라 수치 티어다");
    {
        // 같은 각인이 등급만 달리 나온다 (§4). 증가량 비는 1 : 2 : 4.
        World w = makeWorld(11);
        Fixed byGrade[MAX_CARD_GRADES]{};
        int32_t seenGrade[MAX_CARD_GRADES] = {0, 0, 0, 0, 0};
        for (int i = 0; i < 40000 ; ++i) {
            dealCards(w, cfg);
            for (uint8_t k = 0; k < w.cards.offer.count; ++k) {
                const CardOffer& c = w.cards.offer.offers[k];
                if (c.kind != CardKind::Engraving || c.entryId != 0) continue;
                byGrade[c.grade] = c.value;
                ++seenGrade[c.grade];
            }
        }
        CHECK(seenGrade[1] > 0 && seenGrade[2] > 0 && seenGrade[3] > 0);
        printf("    E_PIERCE 수치  고급 %d · 희귀 %d · 영웅 %d raw\n",
               byGrade[1].raw, byGrade[2].raw, byGrade[3].raw);
        // 희귀 = 고급 × 2, 영웅 = 고급 × 4 (permille 절삭 오차 허용)
        CHECK(byGrade[2].raw >= byGrade[1].raw * 2 - 8);
        CHECK(byGrade[2].raw <= byGrade[1].raw * 2 + 8);
        CHECK(byGrade[3].raw >= byGrade[1].raw * 4 - 16);
        CHECK(byGrade[3].raw <= byGrade[1].raw * 4 + 16);
    }

    dctest::section("중복 선택은 승급이 아니라 누적");
    {
        World w = makeWorld(5);
        w.cards.pendingLevelUps = 3;
        w.cards.offer.clear();
        CardOffer c;
        c.kind = CardKind::Engraving; c.grade = 1; c.entryId = 2;
        c.value = Fixed::fromPermille(300);
        w.cards.offer.offers[0] = c;
        w.cards.offer.count = 1;
        CHECK(chooseCard(w, cfg, 0));
        const int32_t once = w.cards.engrave[2].raw;
        CHECK(once > 0);

        w.cards.offer.offers[0] = c; w.cards.offer.count = 1;
        CHECK(chooseCard(w, cfg, 0));
        CHECK_EQ(w.cards.engrave[2].raw, once * 2);   // **정확히 두 배** — 덧셈이다
        w.cards.offer.offers[0] = c; w.cards.offer.count = 1;
        CHECK(chooseCard(w, cfg, 0));
        CHECK_EQ(w.cards.engrave[2].raw, once * 3);
    }

    dctest::section("레벨업 — 곡선과 연속 레벨업");
    {
        World w = makeWorld(9);
        CHECK_EQ(w.hero.level, 1);
        CHECK(!w.cards.offer.open());

        gainExp(w, cfg, cfg.needFor(1) - 1);
        CHECK_EQ(w.hero.level, 1);
        CHECK(!w.cards.offer.open());

        gainExp(w, cfg, 1);
        CHECK_EQ(w.hero.level, 2);
        CHECK(w.cards.offer.open());
        CHECK_EQ(w.cards.pendingLevelUps, 1);

        // 한 번에 큰 경험치 — **연속 레벨업도 한 화면씩** 처리한다.
        gainExp(w, cfg, cfg.needFor(2) + cfg.needFor(3) + cfg.needFor(4));
        CHECK_EQ(w.hero.level, 5);
        CHECK_EQ(w.cards.pendingLevelUps, 4);
        CHECK_EQ(w.cards.offer.count, cfg.cardsPerLevel);

        // 선택할 때마다 하나씩 소진되고 다음 화면이 뜬다.
        for (int i = 4; i > 0; --i) {
            CHECK_EQ(w.cards.pendingLevelUps, i);
            CHECK(w.cards.offer.open());
            CHECK(chooseCard(w, cfg, 0));
        }
        CHECK_EQ(w.cards.pendingLevelUps, 0);
        CHECK(!w.cards.offer.open());
        CHECK(!chooseCard(w, cfg, 0));      // 열린 화면이 없으면 거부
    }

    dctest::section("리롤 — 성장 카드를 통째로 다시");
    {
        World w = makeWorld(13);
        gainExp(w, cfg, cfg.needFor(1));
        CHECK(w.cards.offer.open());
        CardOffer before[MAX_CARDS_PER_LEVEL];
        for (uint8_t i = 0; i < w.cards.offer.count; ++i) before[i] = w.cards.offer.offers[i];

        CHECK(rerollCards(w, cfg));
        CHECK_EQ(w.cards.rerolls, 1);
        CHECK_EQ(w.cards.offer.count, cfg.cardsPerLevel);
        bool anyDifferent = false;
        for (uint8_t i = 0; i < w.cards.offer.count; ++i) {
            if (w.cards.offer.offers[i].entryId != before[i].entryId
                || w.cards.offer.offers[i].kind != before[i].kind) anyDifferent = true;
        }
        CHECK(anyDifferent);

        w.cards.offer.clear();
        CHECK(!rerollCards(w, cfg));        // 열린 화면이 없으면 거부
    }

    dctest::section("입력 로그 — 카드 선택도 (틱, 값) 한 형식");
    {
        auto play = [&](InputLog& log, bool record) {
            World w = makeWorld(31337);
            dev::applyHeroBaseline(w);
            SimScratch sc;
            Rng r = Rng::derive(5, RngStream::Events);
            if (!record) log.rewind(0);
            for (int i = 0; i < 4000; ++i) {
                if (record) {
                    if (w.cards.offer.open()) {
                        InputEvent e;
                        e.tick  = w.tickCount();
                        e.kind  = InputKind::CardChoice;
                        e.value = r.range(w.cards.offer.count);
                        if (applyInput(w, cfg, e)) (void)log.record(e);
                    }
                } else {
                    InputEvent e;
                    while (log.next(w.tickCount(), &e)) (void)applyInput(w, cfg, e);
                }
                stepWorld(w, cfg, sc);
            }
            struct R { uint64_t sum; int32_t level; };
            return R{w.checksum(), w.hero.level};
        };
        static InputLog log;
        const auto live   = play(log, true);
        const auto replay = play(log, false);
        printf("    4000틱 — 레벨 %d · 카드 선택 %u회 · 재생 %s\n",
               live.level, log.count(), live.sum == replay.sum ? "일치" : "불일치");
        CHECK(log.count() > 0);
        CHECK_EQU(live.sum, replay.sum);

        // 범위 밖 선택은 거부한다 — 입력 로그가 오염됐을 수 있다.
        World w = makeWorld(1);
        gainExp(w, cfg, cfg.needFor(1));
        CHECK(!chooseCard(w, cfg, 99));
        CHECK(w.cards.offer.open());        // 거부해도 화면은 그대로다
    }

    dctest::section("카드 상태는 [상태] — 체크섬에 들어간다");
    {
        World a = makeWorld(4), b = makeWorld(4);
        CHECK_EQU(a.checksum(), b.checksum());

        a.cards.engrave[0] += Fixed::fromPermille(1);
        CHECK(a.checksum() != b.checksum());
        b.cards.engrave[0] += Fixed::fromPermille(1);
        CHECK_EQU(a.checksum(), b.checksum());

        // 전설 풀 비트마스크가 빠지면 같은 전설이 두 번 나오는 판이 생긴다.
        a.cards.legendTake(3);
        CHECK(a.checksum() != b.checksum());
        b.cards.legendTake(3);
        CHECK_EQU(a.checksum(), b.checksum());

        // 제시된 화면도 상태다 — 선택 전에 스냅샷을 뜨면 같은 화면이 나와야 한다.
        dealCards(a, cfg);
        CHECK(a.checksum() != b.checksum());
    }

    dctest::section("결정론 — 같은 시드면 같은 카드");
    {
        auto draw = [&](uint64_t seed) {
            World w = makeWorld(seed);
            Hasher h;
            for (int i = 0; i < 2000; ++i) {
                dealCards(w, cfg);
                w.cards.offer.hashInto(h);
            }
            return h.value();
        };
        CHECK_EQU(draw(77), draw(77));
        CHECK(draw(77) != draw(78));
    }

    dctest::section("레벨업 기본 성장 — 레벨 그 자체가 스탯을 올린다");
    {
        // **회귀 테스트.** power_per_levelup_permille(7%)와 speed_growth_share가
        // 파이썬에만 있고 코어가 읽지 않아, 레벨 18의 기본공격 DPS가 레벨 1과
        // 같은 13.0이었다. balance_baseline의 성장 곡선 전체가 이 값을 전제로 한다.
        World w;
        w.init(1);
        dev::applyHeroBaseline(w);
        const double base = static_cast<double>(w.hero.stats.value(Stat::AttackPower).raw)
                          * static_cast<double>(w.hero.stats.value(Stat::AttackSpeed).raw);

        while (w.hero.level < 15) gainExp(w, cfg, cfg.needFor(w.hero.level));
        CHECK_EQ(w.hero.level, 15);
        const double at15 = static_cast<double>(w.hero.stats.value(Stat::AttackPower).raw)
                          * static_cast<double>(w.hero.stats.value(Stat::AttackSpeed).raw);
        const double mult = at15 / base;

        // 표가 1.07^14 = 2.578배를 담는다. 고정소수점 오차 1% 안.
        CHECK(mult > 2.55 && mult < 2.61);
        // **공속과 한 대 피해가 절반씩** — 배분이 타수를 정한다 (share 50%)
        const double speedMult = static_cast<double>(w.hero.stats.value(Stat::AttackSpeed).raw)
                               / Fixed::one().raw;
        CHECK(speedMult > 1.59 && speedMult < 1.62);

        // **한 번에 여러 레벨이 올라도 정확하다** — 누적 배율을 통째로 갈아끼우므로
        // 레벨마다 곱할 때 생기는 반올림 누적이 없다
        World jump;
        jump.init(1);
        dev::applyHeroBaseline(jump);
        // 레벨 1 → 25에 필요한 경험치를 한 번에 준다
        int64_t lump = 0;
        for (int32_t lv = 1; lv < 25; ++lv) lump += cfg.needFor(lv);
        gainExp(jump, cfg, lump);
        World step;
        step.init(1);
        dev::applyHeroBaseline(step);
        while (step.hero.level < jump.hero.level) gainExp(step, cfg, cfg.needFor(step.hero.level));
        CHECK_EQ(step.hero.level, jump.hero.level);
        CHECK_EQ(step.hero.stats.value(Stat::AttackPower).raw,
                 jump.hero.stats.value(Stat::AttackPower).raw);
        CHECK_EQ(jump.hero.level, 25);
        printf("    레벨 15에서 %.2f배 (표 2.58배) · 1→%d 한 번에 올려도 한 칸씩과 동일\n",
               mult, jump.hero.level);
    }

    return dctest::summary("test_card");
}
