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
        // **아이템 뽑기 칸을 끈다.** 이 절은 성장 카드 추첨만 검증한다 —
        // 0번 고정 칸이 있으면 offers[0]이 아이템이라 등급 검사가 어긋난다.
        legendOnly.commonPoolSize = 0;
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
            CHECK(chooseCard(w, legendOnly, dev::devRecipeTable(), 0));
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
        CHECK(chooseCard(w, cfg, dev::devRecipeTable(), 0));
        const int32_t once = w.cards.engrave[2].raw;
        CHECK(once > 0);

        w.cards.offer.offers[0] = c; w.cards.offer.count = 1;
        CHECK(chooseCard(w, cfg, dev::devRecipeTable(), 0));
        CHECK_EQ(w.cards.engrave[2].raw, once * 2);   // **정확히 두 배** — 덧셈이다
        w.cards.offer.offers[0] = c; w.cards.offer.count = 1;
        CHECK(chooseCard(w, cfg, dev::devRecipeTable(), 0));
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
            CHECK(chooseCard(w, cfg, dev::devRecipeTable(), 0));
        }
        CHECK_EQ(w.cards.pendingLevelUps, 0);
        CHECK(!w.cards.offer.open());
        CHECK(!chooseCard(w, cfg, dev::devRecipeTable(), 0));      // 열린 화면이 없으면 거부
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
                        if (applyInput(w, cfg, dev::devRecipeTable(), e)) (void)log.record(e);
                    }
                } else {
                    InputEvent e;
                    while (log.next(w.tickCount(), &e)) (void)applyInput(w, cfg, dev::devRecipeTable(), e);
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
        CHECK(!chooseCard(w, cfg, dev::devRecipeTable(), 99));
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

    dctest::section("각인 효과 — 수치형 넷");
    {
        // 각인은 **전부 w.cards.engrave[]를 읽는다.** 각 테스트는 "각인이 없을 때"와
        // "있을 때"를 같은 상황에서 재서, 효과가 조용히 0이면 실패하게 만든다.
        auto trash = [&](World& w, Fixed x, int32_t hp, int32_t armor) {
            SpawnDesc d;
            d.posX = x; d.posY = Fixed{};
            d.maxHp = Fixed(hp); d.armor = Fixed(armor);
            d.archetype = Archetype::Trash; d.attackRange = Fixed(1);
            return w.entities.spawn(d, 0, 7);
        };

        // ── E_REND(파쇄) — 방어 무시 ──
        {
            World a; a.init(7); dev::applyHeroBaseline(a);
            const EntityId ta = trash(a, Fixed(1), 100000, 200);
            const Fixed plain = applySkillHit(a, cfg, static_cast<uint32_t>(a.entities.denseOf(ta)),
                                              Fixed(100));
            World b; b.init(7); dev::applyHeroBaseline(b);
            b.cards.engrave[engraveIndex(EngraveId::Rend)] = Fixed::fromPermille(500);
            const EntityId tb = trash(b, Fixed(1), 100000, 200);
            const Fixed rend = applySkillHit(b, cfg, static_cast<uint32_t>(b.entities.denseOf(tb)),
                                             Fixed(100));
            CHECK(rend.raw > plain.raw);
            // Armor 200 → 100이면 감쇠가 100/300 → 100/200이다
            printf("    E_REND 50%%: Armor 200에서 피해 %.1f → %.1f\n",
                   (double)plain.raw / Fixed::ONE_RAW, (double)rend.raw / Fixed::ONE_RAW);
        }

        // ── E_SWARM(군집) — 주변 적 수에 비례 ──
        {
            World w; w.init(7); dev::applyHeroBaseline(w);
            CHECK_EQ(swarmMult(w, cfg).raw, Fixed::one().raw);     // 각인 없으면 1.0배
            w.cards.engrave[engraveIndex(EngraveId::Swarm)] = Fixed::fromPermille(90);
            CHECK_EQ(swarmMult(w, cfg).raw, Fixed::one().raw);     // 적이 없으면 여전히 1.0배
            for (int32_t k = 0; k < 4; ++k) trash(w, Fixed(1), 100, 0);
            const Fixed four = swarmMult(w, cfg);
            CHECK(four.raw > Fixed::one().raw);
            // **상한을 넘지 않는다** — 반경 안에 상한의 3배를 넣어도 같다
            for (int32_t k = 0; k < cfg.swarmMaxStacks * 3; ++k) trash(w, Fixed(1), 100, 0);
            const Fixed many = swarmMult(w, cfg);
            const Fixed cap = Fixed::one() + Fixed::fromPermille(90) * cfg.swarmMaxStacks;
            CHECK(many.raw <= cap.raw);
            CHECK(many.raw > four.raw);
            printf("    E_SWARM 9%%: 4마리 %.2f배 · 상한(%d) %.2f배\n",
                   (double)four.raw / Fixed::ONE_RAW, cfg.swarmMaxStacks,
                   (double)many.raw / Fixed::ONE_RAW);
        }

        // ── E_CRIT(예리함) — 치확 100% 초과분이 치피로 간다 ──
        {
            World w; w.init(7); dev::applyHeroBaseline(w);
            Fixed ch = Fixed::fromPermille(100), mu = Fixed::fromPermille(1500);
            critWithEngrave(w, &ch, &mu);
            CHECK_EQ(ch.raw, Fixed::fromPermille(100).raw);        // 각인 없으면 그대로
            CHECK_EQ(mu.raw, Fixed::fromPermille(1500).raw);

            w.cards.engrave[engraveIndex(EngraveId::Crit)] = Fixed::fromPermille(200);
            ch = Fixed::fromPermille(100); mu = Fixed::fromPermille(1500);
            critWithEngrave(w, &ch, &mu);
            CHECK_EQ(ch.raw, Fixed::fromPermille(300).raw);        // 치확 +20%p
            CHECK_EQ(mu.raw, Fixed::fromPermille(1900).raw);       // 치피 +40%p (2배)

            // **초과분 전환** — 죽은 수치가 생기면 빌드 종속 카드로 기능하지 못한다
            w.cards.engrave[engraveIndex(EngraveId::Crit)] = Fixed::fromPermille(1000);
            ch = Fixed::fromPermille(500); mu = Fixed::fromPermille(1500);
            critWithEngrave(w, &ch, &mu);
            CHECK_EQ(ch.raw, Fixed::one().raw);                    // 100%에서 멈춘다
            // 치피 = 1500 + 2000(각인) + 500(초과분 전환) = 4000
            CHECK_EQ(mu.raw, Fixed::fromPermille(4000).raw);
            printf("    E_CRIT: 치확 150%%가 될 값이 100%%로 잘리고 초과 50%%p가 치피로\n");
        }

        // ── E_WIDE(확장) — 광역 반경 ──
        {
            World w; w.init(7); dev::applyHeroBaseline(w);
            CHECK_EQ(wideRadius(w, cfg.aoeRadius).raw, cfg.aoeRadius.raw);
            w.cards.engrave[engraveIndex(EngraveId::Wide)] = Fixed::fromPermille(200);
            CHECK(wideRadius(w, cfg.aoeRadius).raw > cfg.aoeRadius.raw);
            CHECK_EQ(wideRadius(w, cfg.aoeRadius).raw,
                     (cfg.aoeRadius * Fixed::fromPermille(1200)).raw);
        }
    }

    dctest::section("각인 효과 — 훅형 셋");
    {
        auto spawnAt = [&](World& w, Fixed x, Fixed y, int32_t hp) {
            SpawnDesc d;
            d.posX = x; d.posY = y;
            d.maxHp = Fixed(hp);
            d.archetype = Archetype::Trash; d.attackRange = Fixed(1);
            return w.entities.spawn(d, 0, 8);
        };

        // ── E_DECAY(부식) — 도트가 시간에 걸쳐 들어간다 ──
        {
            World w; w.init(8); dev::applyHeroBaseline(w);
            w.cards.engrave[engraveIndex(EngraveId::Decay)] = Fixed::fromPermille(300);
            const EntityId t = spawnAt(w, Fixed(1), Fixed{}, 100000);
            const uint32_t i = static_cast<uint32_t>(w.entities.denseOf(t));
            applySkillHit(w, cfg, i, Fixed(1));
            CHECK_EQ(w.entities.decayLeft[i], cfg.decayTicks);
            CHECK(w.entities.decayPerTick[i].raw > 0);

            const Fixed before = w.entities.damageTaken[i];
            for (int32_t k = 0; k < cfg.decayTicks; ++k) decayRun(w, cfg);
            CHECK(w.entities.damageTaken[i].raw > before.raw);
            CHECK_EQ(w.entities.decayLeft[i], 0);
            // 지속이 끝나면 더 들어오지 않는다
            const Fixed after = w.entities.damageTaken[i];
            for (int32_t k = 0; k < 40; ++k) decayRun(w, cfg);
            CHECK_EQ(w.entities.damageTaken[i].raw, after.raw);

            // **갱신이지 중첩이 아니다** — 다시 맞아도 틱당 피해가 커지지 않는다
            World w2; w2.init(8); dev::applyHeroBaseline(w2);
            w2.cards.engrave[engraveIndex(EngraveId::Decay)] = Fixed::fromPermille(300);
            const EntityId t2 = spawnAt(w2, Fixed(1), Fixed{}, 100000);
            const uint32_t j = static_cast<uint32_t>(w2.entities.denseOf(t2));
            applySkillHit(w2, cfg, j, Fixed(1));
            const Fixed once = w2.entities.decayPerTick[j];
            for (int32_t k = 0; k < 5; ++k) applySkillHit(w2, cfg, j, Fixed(1));
            CHECK_EQ(w2.entities.decayPerTick[j].raw, once.raw);   // 중첩 없음
            CHECK_EQ(w2.entities.decayLeft[j], cfg.decayTicks);    // 지속은 갱신
            printf("    E_DECAY 30%%: %d틱 동안 %.1f 피해 · 재타격은 갱신만\n",
                   cfg.decayTicks,
                   (double)(after.raw - before.raw) / Fixed::ONE_RAW);
        }

        // ── E_PIERCE(관통) — 타겟 뒤 직선만 맞는다 ──
        {
            World w; w.init(8); dev::applyHeroBaseline(w);
            w.hero.posX = Fixed{}; w.hero.posY = Fixed{};
            w.cards.engrave[engraveIndex(EngraveId::Pierce)] = Fixed::fromPermille(250);

            const EntityId tgt    = spawnAt(w, Fixed(2), Fixed{},  100000);   // 타겟
            const EntityId behind = spawnAt(w, Fixed(4), Fixed{},  100000);   // 뒤 — 맞는다
            const EntityId side   = spawnAt(w, Fixed(4), Fixed(3), 100000);   // 옆 — 안 맞는다
            const EntityId front  = spawnAt(w, Fixed(1), Fixed{},  100000);   // 앞 — 안 맞는다

            applyPierce(w, cfg, static_cast<uint32_t>(w.entities.denseOf(tgt)), Fixed(100));
            CHECK(w.entities.damageTaken[static_cast<uint32_t>(w.entities.denseOf(behind))].raw > 0);
            CHECK_EQ(w.entities.damageTaken[static_cast<uint32_t>(w.entities.denseOf(side))].raw, 0);
            CHECK_EQ(w.entities.damageTaken[static_cast<uint32_t>(w.entities.denseOf(front))].raw, 0);
            // 타겟 자신은 관통으로 또 맞지 않는다
            CHECK_EQ(w.entities.damageTaken[static_cast<uint32_t>(w.entities.denseOf(tgt))].raw, 0);
            printf("    E_PIERCE 25%%: 뒤만 맞고 옆·앞·본체는 안 맞는다\n");
        }

        // ── E_CHAIN(연타) — 총 피해가 오르고 2회로 나뉜다 ──
        {
            auto totalDamage = [&](int32_t chainPermille) {
                World w; w.init(8); dev::applyHeroBaseline(w);
                if (chainPermille > 0) {
                    w.cards.engrave[engraveIndex(EngraveId::Chain)] =
                        Fixed::fromPermille(chainPermille);
                }
                const EntityId t = spawnAt(w, Fixed(1), Fixed{}, 100000);
                w.hero.target = t;
                executeSkill(w, cfg, 0, QteGrade::Miss);            // 0번은 단일기
                return w.entities.damageTaken[static_cast<uint32_t>(w.entities.denseOf(t))];
            };
            const Fixed plain = totalDamage(0);
            const Fixed chain = totalDamage(450);
            CHECK(plain.raw > 0);
            // 총 피해 +45%. 2회로 나뉘어도 합은 같다 (고정소수점 오차 1% 안)
            const double ratio = (double)chain.raw / plain.raw;
            CHECK(ratio > 1.43 && ratio < 1.47);
            printf("    E_CHAIN 45%%: 총 피해 %.2f배 (2회 분할)\n", ratio);
        }
    }

    dctest::section("유물 효과 — 전장에 규칙을 더한다");
    {
        auto spawnAt = [&](World& w, Fixed x, Fixed y, Archetype a, int32_t hp) {
            SpawnDesc d;
            d.posX = x; d.posY = y;
            d.maxHp = Fixed(hp);
            d.archetype = a; d.attackRange = Fixed(1);
            d.approachSpeed = cfg.trash.approachSpeed;
            d.attackDamage        = cfg.trash.damage;        // 없으면 피격이 0이라 R_RAGE가 안 쌓인다
            d.attackCooldownTicks = cfg.trash.cooldownTicks;
            return w.entities.spawn(d, 0, 9);
        };

        // ── R_BEACON(추적의 신호탄) — 엘리트·보스에만 붙는다 ──
        {
            World w; w.init(9); dev::applyHeroBaseline(w);
            CHECK_EQ(beaconMult(w, Archetype::Trash).raw, Fixed::one().raw);
            w.cards.relic[relicIndex(RelicId::Beacon)] = Fixed::fromPermille(150);
            // **잡몹에는 무용지물** — 이게 물량 유물들과 정반대 축이라는 근거다
            CHECK_EQ(beaconMult(w, Archetype::Trash).raw, Fixed::one().raw);
            CHECK_EQ(beaconMult(w, Archetype::Elite).raw, Fixed::fromPermille(1150).raw);
            CHECK_EQ(beaconMult(w, Archetype::Boss).raw,  Fixed::fromPermille(1150).raw);
        }

        // ── R_RAGE(분노의 토템) — 피격 중첩, 상한 고정, 통째 만료 ──
        {
            World w; w.init(9); dev::applyHeroBaseline(w);
            w.cards.relic[relicIndex(RelicId::Rage)] = Fixed::fromPermille(10);
            CHECK_EQ(heroPowerMult(w, cfg).raw, Fixed::one().raw);   // 안 맞으면 1.0배

            // 영웅에 붙여 놓고 맞게 한다
            spawnAt(w, Fixed{}, Fixed{}, Archetype::Trash, 100000);
            for (int32_t t = 0; t < cfg.trash.cooldownTicks * 30; ++t) {
                w.beginTick(); combatRun(w, cfg); w.endTick();
            }
            // **상한을 넘지 않는다** (등급과 무관하게 고정)
            CHECK_EQ(w.hero.rageStacks, cfg.rageMaxStacks);
            const Fixed capped = heroPowerMult(w, cfg);
            CHECK_EQ(capped.raw, (Fixed::one() + Fixed::fromPermille(10) * cfg.rageMaxStacks).raw);

            // **만료는 통째로** — 지속이 지나면 중첩이 한 번에 0이 된다
            w.entities.markDead(w.entities.idAt(0));
            w.applyDeaths();
            for (int32_t t = 0; t < cfg.rageDurationTicks + 2; ++t) {
                w.beginTick(); combatRun(w, cfg); w.endTick();
            }
            CHECK_EQ(w.hero.rageStacks, 0);
            CHECK_EQ(heroPowerMult(w, cfg).raw, Fixed::one().raw);
            printf("    R_RAGE 1%%: 상한 %d중첩에서 %.2f배 · 지속 후 통째 해제\n",
                   cfg.rageMaxStacks, (double)capped.raw / Fixed::ONE_RAW);
        }

        // ── R_TIDE(밀물의 인장) — 구간 경과에 비례, 구간 넘어가면 리셋 ──
        {
            World w; w.init(9); dev::applyHeroBaseline(w);
            w.cards.relic[relicIndex(RelicId::Tide)] = Fixed::fromPermille(3);
            w.run.segmentStartTick = 0;
            CHECK_EQ(heroPowerMult(w, cfg).raw, Fixed::one().raw);

            for (int32_t t = 0; t < cfg.tickHz * 30; ++t) { w.beginTick(); w.endTick(); }
            const Fixed at30 = heroPowerMult(w, cfg);
            // 30초 × 0.3% = +9%
            CHECK_EQ(at30.raw, (Fixed::one() + Fixed::fromPermille(3) * 30).raw);

            // **구간이 넘어가면 리셋된다** — 그게 "구간 후반에 가장 강하다"의 조건이다
            w.run.clearPoints = cfg.segmentStartPoints[1];
            progressRun(w, cfg);
            CHECK_EQ(w.run.segmentStartTick, w.tickCount());
            CHECK_EQ(heroPowerMult(w, cfg).raw, Fixed::one().raw);
            printf("    R_TIDE 0.3%%/초: 30초에 %.2f배 · 구간 전환에 리셋\n",
                   (double)at30.raw / Fixed::ONE_RAW);
        }

        // ── R_BOLT(뇌전의 성물) — 주기마다 한 마리 ──
        {
            World w; w.init(9); dev::applyHeroBaseline(w);
            w.cards.relic[relicIndex(RelicId::Bolt)] = Fixed::fromPermille(400);
            for (int32_t k = 0; k < 5; ++k) spawnAt(w, Fixed(50), Fixed(50), Archetype::Trash, 100000);

            auto totalTaken = [&]() {
                Fixed t{};
                for (uint32_t i = 0; i < w.entities.count(); ++i) t += w.entities.damageTaken[i];
                return t;
            };
            // 주기 전에는 아무 일도 없다
            boltRun(w, cfg);                       // 첫 호출은 발동 (boltNextTick = 0)
            const Fixed first = totalTaken();
            CHECK(first.raw > 0);
            for (int32_t t = 0; t < cfg.boltIntervalTicks - 1; ++t) {
                w.beginTick(); w.endTick(); boltRun(w, cfg);
            }
            CHECK_EQ(totalTaken().raw, first.raw);   // 주기 안에서는 추가 방전 없음
            w.beginTick(); w.endTick(); boltRun(w, cfg);
            CHECK(totalTaken().raw > first.raw);     // 주기가 돌면 다시 친다
            printf("    R_BOLT 40%%: %d틱 주기로 한 마리씩\n", cfg.boltIntervalTicks);
        }

        // ── R_GREED(탐욕의 주머니) — 경험치 획득 증가 ──
        {
            // 골드가 아니라 경험치다 (골드 시스템 미구현). **전투와 경쟁하지 않는
            // 유일한 축**이라 카드를 더 자주 뽑게 해 선택지 품질을 산다.
            World plain; plain.init(9); dev::applyHeroBaseline(plain);
            gainExp(plain, cfg, 1000);

            World greedy; greedy.init(9); dev::applyHeroBaseline(greedy);
            greedy.cards.relic[relicIndex(RelicId::Greed)] = Fixed::fromPermille(200);
            gainExp(greedy, cfg, 1000);

            // 같은 획득량에 +20%면 레벨이 같거나 앞서고, 누적 경험치가 더 많다
            const int64_t plainTotal = plain.hero.exp
                + [&]{ int64_t t = 0; for (int32_t l = 1; l < plain.hero.level; ++l) t += cfg.needFor(l); return t; }();
            const int64_t greedTotal = greedy.hero.exp
                + [&]{ int64_t t = 0; for (int32_t l = 1; l < greedy.hero.level; ++l) t += cfg.needFor(l); return t; }();
            CHECK_EQ(plainTotal, 1000);
            // **정확히 1200은 아니다.** 20%를 20.12 고정소수점으로 담으면
            // 819/4096 = 0.19995라 0.024% 모자란다 — 프로젝트 전반과 같은 성질이라
            // 허용 오차로 본다. 밴드를 좁게 잡아 "효과 없음"이나 "2배"는 걸린다.
            CHECK(greedTotal >= 1198 && greedTotal <= 1200);
            CHECK(greedy.hero.level >= plain.hero.level);

            // **경험치는 int64다** — Fixed 범위(±524,288)를 넘는 값에서도 배율이 맞아야 한다
            World big; big.init(9); dev::applyHeroBaseline(big);
            big.cards.relic[relicIndex(RelicId::Greed)] = Fixed::fromPermille(200);
            gainExp(big, cfg, 100000000LL);
            int64_t bigTotal = big.hero.exp;
            for (int32_t l = 1; l < big.hero.level; ++l) bigTotal += cfg.needFor(l);
            CHECK(bigTotal >= 119900000LL && bigTotal <= 120000000LL);
            printf("    R_GREED 20%%: 경험치 1000 → %lld · 1억 → %lld (고정소수점 오차 0.024%%)\n",
                   static_cast<long long>(greedTotal), static_cast<long long>(bigTotal));
        }

        // ── R_FROST(서리 오라) — 반경 안만 둔화 ──
        {
            World w; w.init(9); dev::applyHeroBaseline(w);
            w.hero.posX = Fixed{}; w.hero.posY = Fixed{};
            const Fixed r = cfg.frostRadius;
            spawnAt(w, r - Fixed(1), Fixed{}, Archetype::Trash, 100000);   // 안
            spawnAt(w, r + Fixed(5), Fixed{}, Archetype::Trash, 100000);   // 밖
            CHECK_EQ(frostMult(w, cfg, 0).raw, Fixed::one().raw);          // 유물 없으면 1.0

            w.cards.relic[relicIndex(RelicId::Frost)] = Fixed::fromPermille(100);
            CHECK_EQ(frostMult(w, cfg, 0).raw,
                     (Fixed::one() - Fixed::fromPermille(100)).raw);   // 반경 안 −10%
            CHECK_EQ(frostMult(w, cfg, 1).raw, Fixed::one().raw);               // 반경 밖 그대로

            // 100%를 넘겨도 역주행하지 않는다 (정지까지만)
            w.cards.relic[relicIndex(RelicId::Frost)] = Fixed::fromPermille(1500);
            CHECK_EQ(frostMult(w, cfg, 0).raw, 0);
            printf("    R_FROST 10%%: 반경 %.1f타일 안만 0.90배 · 밖은 1.00배\n",
                   (double)r.raw / Fixed::ONE_RAW);
        }
    }

    dctest::section("전설 — 판의 규칙을 바꾸는 급");
    {
        auto spawnAt = [&](World& w, Fixed x, Fixed y, int32_t hp) {
            SpawnDesc d;
            d.posX = x; d.posY = y;
            d.maxHp = Fixed(hp);
            d.archetype = Archetype::Trash; d.attackRange = Fixed(1);
            return w.entities.spawn(d, 0, 12);
        };

        // ── RL_ECHO(무한의 메아리) — 기본 공격이 한 번 더 ──
        {
            auto damageIn = [&](bool echo) {
                World w; w.init(12); dev::applyHeroBaseline(w);
                if (echo) w.cards.legendTake(legendIndexOf(LegendId::Echo));
                const EntityId t = spawnAt(w, Fixed(1), Fixed{}, 100000);
                w.hero.target = t;
                w.beginTick(); combatRun(w, cfg); w.endTick();
                return w.entities.damageTaken[static_cast<uint32_t>(w.entities.denseOf(t))];
            };
            const Fixed plain = damageIn(false);
            const Fixed echo  = damageIn(true);
            CHECK(plain.raw > 0);
            // 기본 공격이 정확히 2회분이 된다 (스킬 proc은 시드에 따라 섞이므로 밴드로 본다)
            const double ratio = (double)echo.raw / plain.raw;
            CHECK(ratio > 1.9 && ratio < 2.1);
            printf("    RL_ECHO: 한 틱 피해 %.2f배\n", ratio);
        }

        // ── RL_STORM(폭풍의 핵) — 반경 안 전원에게 매 틱 ──
        {
            World w; w.init(12); dev::applyHeroBaseline(w);
            w.hero.posX = Fixed{}; w.hero.posY = Fixed{};
            const EntityId inside  = spawnAt(w, cfg.stormRadius - Fixed(1), Fixed{}, 100000);
            const EntityId outside = spawnAt(w, cfg.stormRadius + Fixed(5), Fixed{}, 100000);

            stormRun(w, cfg);   // 전설이 없으면 아무 일도 없다
            CHECK_EQ(w.entities.damageTaken[static_cast<uint32_t>(w.entities.denseOf(inside))].raw, 0);

            w.cards.legendTake(legendIndexOf(LegendId::Storm));
            for (int32_t t = 0; t < cfg.tickHz; ++t) stormRun(w, cfg);   // 1초
            const Fixed hit = w.entities.damageTaken[static_cast<uint32_t>(w.entities.denseOf(inside))];
            CHECK(hit.raw > 0);
            // **반경 밖은 안 맞는다** — 밀집도 축이라는 성질이 여기서 나온다
            CHECK_EQ(w.entities.damageTaken[static_cast<uint32_t>(w.entities.denseOf(outside))].raw, 0);
            // 초당 공격력의 stormDpsPermille 만큼 (감쇠 없음 · Armor 0)
            const Fixed want = w.hero.stats.value(Stat::AttackPower)
                             * Fixed::fromPermille(cfg.stormDpsPermille);
            CHECK(hit.raw > want.raw * 9 / 10 && hit.raw < want.raw * 11 / 10);
            printf("    RL_STORM: 반경 %.1f타일 안 1초에 %.2f (목표 %.2f) · 밖은 0\n",
                   (double)cfg.stormRadius.raw / Fixed::ONE_RAW,
                   (double)hit.raw / Fixed::ONE_RAW, (double)want.raw / Fixed::ONE_RAW);
        }

        // ── 폭풍은 onHit 각인을 태우지 않는다 ──
        {
            // 매 틱 도는 지속 피해에 부식이 붙으면 각인 하나가 초당 20회 발동하는
            // 꼴이 되어 예산이 통째로 무너진다.
            World w; w.init(12); dev::applyHeroBaseline(w);
            w.hero.posX = Fixed{}; w.hero.posY = Fixed{};
            w.cards.legendTake(legendIndexOf(LegendId::Storm));
            w.cards.engrave[engraveIndex(EngraveId::Decay)] = Fixed::fromPermille(300);
            const EntityId t = spawnAt(w, Fixed(1), Fixed{}, 100000);
            stormRun(w, cfg);
            CHECK_EQ(w.entities.decayLeft[static_cast<uint32_t>(w.entities.denseOf(t))], 0);
        }

        // ── 전설 풀에는 아직 효과 없는 칸이 있다 ──
        {
            // 3~8번은 직업 고유 각인 자리인데 수치가 설계되지 않았다.
            // **풀에는 남아 있어 뽑히므로 그만큼 전설 기대값이 낮다** — 이게 지표 1의
            // 해석을 흐리는 요인이라 테스트로 사실을 박아둔다.
            CHECK_EQ(cfg.legendPoolSize, 9u);
            CHECK_EQ(legendIndexOf(LegendId::UniqueFirst), 3u);
            printf("    전설 풀 %u칸 중 효과 구현 2칸(ECHO·STORM) · FORGE와 고유 각인 6칸은 미구현\n",
                   cfg.legendPoolSize);
        }
    }

    return dctest::summary("test_card");
}
