// StatBlock + 모디파이어 테스트 (§9).
//
// 확인할 것이 셋이다:
//   1. **적용 순서가 고정되는가** — 특히 PercentMult의 sourceId 오름차순
//   2. **추가/제거가 정확한 역연산인가** — 장비 착탈을 반복해도 값이 제자리로 온다
//   3. **캐시가 거짓말하지 않는가** — 지연 평가한 값 == 직접 계산한 값
#include <initializer_list>

#include "../include/dc/stat_block.h"
#include "../include/dc/world.h"
#include "test_main.h"

using namespace dc;

static StatBlock makeBlock(Fixed base) {
    Fixed bases[STAT_COUNT];
    StatBounds bounds[STAT_COUNT];
    for (uint32_t i = 0; i < STAT_COUNT; ++i) {
        bases[i] = base;
        bounds[i] = StatBounds{};   // 하한 0, PctAdd 하한 -100%
    }
    StatBlock b;
    b.init(bases, bounds);
    return b;
}

// 캐시를 신뢰하지 않고 대조한다.
static bool cacheAgrees(const StatBlock& b) {
    for (uint32_t i = 0; i < STAT_COUNT; ++i) {
        const Stat s = static_cast<Stat>(i);
        if (b.value(s).raw != b.compute(s).raw) return false;
    }
    return true;
}

int main() {
    printf("test_stat_block\n");

    dctest::section("기본값 · 빈 상태");
    {
        StatBlock b = makeBlock(Fixed(100));
        CHECK_EQ(b.value(Stat::AttackPower).raw, Fixed(100).raw);
        CHECK_EQ(b.base(Stat::AttackPower).raw, Fixed(100).raw);
        CHECK_EQ(b.modCount(Stat::AttackPower), 0u);
        CHECK(cacheAgrees(b));

        b.setBase(Stat::AttackPower, Fixed(250));
        CHECK_EQ(b.value(Stat::AttackPower).raw, Fixed(250).raw);
    }

    dctest::section("Flat · PercentAdd 누적");
    {
        StatBlock b = makeBlock(Fixed(100));
        b.addFlat(Stat::AttackPower, Fixed(20));
        b.addFlat(Stat::AttackPower, Fixed(30));
        CHECK_EQ(b.value(Stat::AttackPower).raw, Fixed(150).raw);

        // (100 + 50) × (1 + 0.5) — **정확히 225가 아니다.**
        // fromPermille은 0방향 절삭이라 200permille = 819raw(819.2에서 절삭),
        // 300permille = 1228raw(1228.8에서). 합 2047은 0.5(2048raw)에 1 못 미친다.
        // permille을 먼저 더하고 한 번에 변환하면 정확하지만, 모디파이어는
        // 출처가 제각각이라 그럴 수 없다. 이 오차는 설계된 것이다 (§10 "1% 표현 오차 0.098%").
        b.addPctAdd(Stat::AttackPower, Fixed::fromPermille(200));
        b.addPctAdd(Stat::AttackPower, Fixed::fromPermille(300));
        const Fixed pct = Fixed::fromPermille(200) + Fixed::fromPermille(300);
        CHECK_EQ(b.value(Stat::AttackPower).raw, (Fixed(150) * (Fixed::one() + pct)).raw);
        CHECK(b.value(Stat::AttackPower).raw < Fixed(225).raw);
        CHECK(Fixed(225).raw - b.value(Stat::AttackPower).raw < 205);   // 0.05 미만
        printf("    150 × (1+0.2+0.3) = %d raw (정확값 %d, 오차 %d raw)\n",
               b.value(Stat::AttackPower).raw, Fixed(225).raw,
               Fixed(225).raw - b.value(Stat::AttackPower).raw);
        CHECK(cacheAgrees(b));

        // **Flat/PercentAdd는 순서 무관이어야 한다** — Fixed가 정수라
        // 덧셈에 결합·교환 법칙이 성립하기 때문이다.
        StatBlock c = makeBlock(Fixed(100));
        c.addPctAdd(Stat::AttackPower, Fixed::fromPermille(300));
        c.addFlat(Stat::AttackPower, Fixed(30));
        c.addPctAdd(Stat::AttackPower, Fixed::fromPermille(200));
        c.addFlat(Stat::AttackPower, Fixed(20));
        CHECK_EQ(c.value(Stat::AttackPower).raw, b.value(Stat::AttackPower).raw);
    }

    dctest::section("착탈 반복이 정확한 역연산");
    {
        // 장비를 100번 끼고 빼도 값이 제자리로 와야 한다.
        // Fixed가 정수라 덧셈의 역연산이 정확하므로 성립한다.
        StatBlock b = makeBlock(Fixed(137));
        const Fixed start = b.value(Stat::AttackPower);
        for (int i = 0; i < 100; ++i) {
            b.addFlat(Stat::AttackPower, Fixed(13));
            b.addPctAdd(Stat::AttackPower, Fixed::fromPermille(77));
            b.removePctAdd(Stat::AttackPower, Fixed::fromPermille(77));
            b.removeFlat(Stat::AttackPower, Fixed(13));
        }
        CHECK_EQ(b.value(Stat::AttackPower).raw, start.raw);

        // PercentMult도 마찬가지. **누적 곱으로 관리했다면 여기서 오차가 쌓인다** —
        // 곱셈의 역연산인 정수 나눗셈이 나머지를 버리기 때문이다.
        for (int i = 0; i < 100; ++i) {
            CHECK(b.addMult(Stat::AttackPower, 1000u + static_cast<uint32_t>(i),
                            Fixed::fromPermille(333)));
            CHECK(b.removeMult(Stat::AttackPower, 1000u + static_cast<uint32_t>(i)));
        }
        CHECK_EQ(b.value(Stat::AttackPower).raw, start.raw);
        CHECK_EQ(b.modCount(Stat::AttackPower), 0u);
    }

    dctest::section("PercentMult — sourceId 오름차순");
    {
        // 곱셈은 정수 절삭 때문에 **순서에 따라 결과가 다르다.** 그래서 순서를
        // 고정해야 하고, 이 테스트가 그 고정을 확인한다.
        StatBlock a = makeBlock(Fixed(1000));
        StatBlock b = makeBlock(Fixed(1000));

        // 같은 모디파이어 집합을 **다른 순서로 추가**한다.
        a.addMult(Stat::AttackPower, 3, Fixed::fromPermille(131));
        a.addMult(Stat::AttackPower, 1, Fixed::fromPermille(377));
        a.addMult(Stat::AttackPower, 2, Fixed::fromPermille(-211));

        b.addMult(Stat::AttackPower, 1, Fixed::fromPermille(377));
        b.addMult(Stat::AttackPower, 2, Fixed::fromPermille(-211));
        b.addMult(Stat::AttackPower, 3, Fixed::fromPermille(131));

        CHECK_EQ(a.value(Stat::AttackPower).raw, b.value(Stat::AttackPower).raw);
        printf("    추가 순서 무관: %d\n", a.value(Stat::AttackPower).raw);

        // 제거 후 재추가해도 불변식이 유지된다.
        a.removeMult(Stat::AttackPower, 2);
        a.addMult(Stat::AttackPower, 2, Fixed::fromPermille(-211));
        CHECK_EQ(a.value(Stat::AttackPower).raw, b.value(Stat::AttackPower).raw);

        // sourceId 중복은 거부한다 — 유일성이 완전 순서의 전제다.
        CHECK(!a.addMult(Stat::AttackPower, 2, Fixed::fromPermille(500)));
        CHECK(!a.addMult(Stat::AttackPower, 0, Fixed::fromPermille(500)));  // 0은 예약
        CHECK(!a.removeMult(Stat::AttackPower, 9999));                      // 없는 것
        CHECK_EQ(a.value(Stat::AttackPower).raw, b.value(Stat::AttackPower).raw);
    }

    dctest::section("PercentMult 순서가 실제로 값을 바꾼다");
    {
        // "순서를 고정한다"가 공허한 요구가 아님을 보인다. 정수 절삭 때문에
        // 곱하는 순서가 다르면 결과가 갈리는 조합이 실제로 존재한다.
        const Fixed m[3] = {Fixed::fromPermille(333), Fixed::fromPermille(-777),
                            Fixed::fromPermille(1111)};
        auto apply = [&](int i0, int i1, int i2) {
            Fixed v = Fixed(7777);
            const int order[3] = {i0, i1, i2};
            for (int k = 0; k < 3; ++k) v = v * (Fixed::one() + m[order[k]]);
            return v.raw;
        };
        const int32_t forward = apply(0, 1, 2);
        int differing = 0;
        const int perms[5][3] = {{0,2,1},{1,0,2},{1,2,0},{2,0,1},{2,1,0}};
        for (const auto& p : perms) if (apply(p[0], p[1], p[2]) != forward) ++differing;
        printf("    6순열 중 %d개가 다른 값 (절삭 때문)\n", differing + 0);
        CHECK(differing > 0);
    }

    dctest::section("Override — sourceId 최소값 하나만");
    {
        StatBlock b = makeBlock(Fixed(500));
        b.addFlat(Stat::MoveSpeed, Fixed(100));
        b.addPctAdd(Stat::MoveSpeed, Fixed::fromPermille(500));
        b.addMult(Stat::MoveSpeed, 5, Fixed::fromPermille(200));
        const Fixed before = b.value(Stat::MoveSpeed);
        CHECK(before.raw > Fixed(600).raw);

        // 속박 — 이동속도 0으로 강제. 앞 단계 계산을 전부 무시한다.
        CHECK(b.addOverride(Stat::MoveSpeed, 42, Fixed(0)));
        CHECK_EQ(b.value(Stat::MoveSpeed).raw, 0);

        // sourceId가 더 큰 Override는 무시된다.
        CHECK(b.addOverride(Stat::MoveSpeed, 99, Fixed(999)));
        CHECK_EQ(b.value(Stat::MoveSpeed).raw, 0);

        // sourceId가 더 작은 Override가 들어오면 그쪽이 이긴다.
        CHECK(b.addOverride(Stat::MoveSpeed, 7, Fixed(3)));
        CHECK_EQ(b.value(Stat::MoveSpeed).raw, Fixed(3).raw);

        // 전부 걷어내면 원래 계산으로 돌아온다.
        CHECK(b.removeOverride(Stat::MoveSpeed, 7));
        CHECK(b.removeOverride(Stat::MoveSpeed, 42));
        CHECK(b.removeOverride(Stat::MoveSpeed, 99));
        CHECK_EQ(b.value(Stat::MoveSpeed).raw, before.raw);
    }

    dctest::section("하한 클램프 — 불변식");
    {
        StatBounds bounds[STAT_COUNT];
        Fixed bases[STAT_COUNT];
        for (uint32_t i = 0; i < STAT_COUNT; ++i) {
            bases[i]  = Fixed(10);
            bounds[i] = StatBounds{Fixed(2), Fixed::fromPermille(-900)};
        }
        StatBlock b;
        b.init(bases, bounds);

        // PercentAdd 누적합이 하한 아래로 내려가도 -90%에서 멈춘다.
        // 누적합 -500%를 넣어도 -90%에서 멈춘다 → 10 × 0.1 = 1.
        // 그런데 최종값 하한이 2이므로 2로 올라온다.
        b.addPctAdd(Stat::AttackSpeed, Fixed::fromPermille(-5000));
        CHECK_EQ(b.value(Stat::AttackSpeed).raw, Fixed(2).raw);

        // 하한이 0이면 클램프 없이 -90%가 그대로 보인다 (PctAdd 하한만 걸린다).
        StatBlock loose = makeBlock(Fixed(10));
        loose.setBounds(Stat::AttackSpeed, StatBounds{Fixed(0), Fixed::fromPermille(-900)});
        loose.addPctAdd(Stat::AttackSpeed, Fixed::fromPermille(-5000));
        // 여기도 permille 절삭이 보인다: -900permille = -3686raw(-3686.4에서 0방향 절삭)
        // 이므로 1-0.9가 정확히 0.1이 아니다.
        CHECK_EQ(loose.value(Stat::AttackSpeed).raw,
                 (Fixed(10) * (Fixed::one() + Fixed::fromPermille(-900))).raw);
        CHECK(loose.value(Stat::AttackSpeed).raw > 0);
        CHECK(loose.value(Stat::AttackSpeed).raw < Fixed(2).raw);

        // **클램프는 읽을 때만 한다.** 저장값은 원래대로 남아 있으므로
        // 되돌리면 정확히 복구된다 — 조건 토글에서 값이 새지 않는다.
        b.removePctAdd(Stat::AttackSpeed, Fixed::fromPermille(-5000));
        CHECK_EQ(b.value(Stat::AttackSpeed).raw, Fixed(10).raw);

        // Override도 하한을 넘지 못한다. 불변식은 무조건 성립해야 한다
        // (나눗셈 분모가 0이 되는 것을 Override로 뚫을 수 없어야 한다).
        b.addOverride(Stat::AttackSpeed, 1, Fixed(0));
        CHECK_EQ(b.value(Stat::AttackSpeed).raw, Fixed(2).raw);
        b.addOverride(Stat::Armor, 2, Fixed(-50));
        CHECK_EQ(b.value(Stat::Armor).raw, Fixed(2).raw);
    }

    dctest::section("용량 초과는 조용히 버리지 않는다");
    {
        StatBlock b = makeBlock(Fixed(100));
        uint32_t added = 0;
        for (uint32_t i = 1; i <= 20; ++i) {
            if (b.addMult(Stat::AttackPower, i, Fixed::fromPermille(10))) ++added;
        }
        CHECK_EQ(added, 8u);     // SmallVec<SourcedMod, 8>
        CHECK_EQ(b.modCount(Stat::AttackPower), 8u);

        uint32_t ov = 0;
        for (uint32_t i = 100; i < 120; ++i) {
            if (b.addOverride(Stat::Armor, i, Fixed(1))) ++ov;
        }
        CHECK_EQ(ov, 4u);        // SmallVec<SourcedMod, 4>
    }

    dctest::section("지연 캐시가 거짓말하지 않는가");
    {
        StatBlock b = makeBlock(Fixed(1000));
        CHECK(b.dirty(Stat::AttackPower));
        (void)b.value(Stat::AttackPower);
        CHECK(!b.dirty(Stat::AttackPower));

        // 쓰기는 전부 dirty를 세워야 한다. 하나라도 빠지면 **낡은 값이 나간다.**
        struct W { const char* name; void (*apply)(StatBlock&); };
        const W writes[] = {
            {"setBase",       [](StatBlock& x){ x.setBase(Stat::AttackPower, Fixed(2)); }},
            {"addFlat",       [](StatBlock& x){ x.addFlat(Stat::AttackPower, Fixed(1)); }},
            {"removeFlat",    [](StatBlock& x){ x.removeFlat(Stat::AttackPower, Fixed(1)); }},
            {"addPctAdd",     [](StatBlock& x){ x.addPctAdd(Stat::AttackPower, Fixed::fromPermille(10)); }},
            {"removePctAdd",  [](StatBlock& x){ x.removePctAdd(Stat::AttackPower, Fixed::fromPermille(10)); }},
            {"addMult",       [](StatBlock& x){ x.addMult(Stat::AttackPower, 77, Fixed::fromPermille(10)); }},
            {"removeMult",    [](StatBlock& x){ x.removeMult(Stat::AttackPower, 77); }},
            {"addOverride",   [](StatBlock& x){ x.addOverride(Stat::AttackPower, 88, Fixed(5)); }},
            {"removeOverride",[](StatBlock& x){ x.removeOverride(Stat::AttackPower, 88); }},
            {"setBounds",     [](StatBlock& x){ x.setBounds(Stat::AttackPower, StatBounds{Fixed(1), Fixed::fromPermille(-500)}); }},
        };
        int stale = 0;
        for (const W& w : writes) {
            StatBlock x = b;
            x.warmAll();                       // 캐시를 채운 뒤
            w.apply(x);                        // 쓰기
            if (!x.dirty(Stat::AttackPower) && x.value(Stat::AttackPower).raw != x.compute(Stat::AttackPower).raw) {
                ++stale;
                printf("    낡은 캐시: %s\n", w.name);
            }
            if (x.value(Stat::AttackPower).raw != x.compute(Stat::AttackPower).raw) {
                ++stale;
                printf("    값 불일치: %s\n", w.name);
            }
        }
        CHECK_EQ(stale, 0);
        printf("    쓰기 %zu종 전부 캐시 무효화됨\n", sizeof(writes) / sizeof(writes[0]));
    }

    dctest::section("캐시는 [파생] — 체크섬에 들어가지 않는다");
    {
        // **같은 상태인데 캐시 상태만 다른 두 World의 해시가 같아야 한다.**
        // 다르면 지연 평가와 즉시 평가가 다른 체크섬을 내는 거짓 양성이 된다.
        World warm, cold;
        warm.init(4242);
        cold.init(4242);
        for (World* w : {&warm, &cold}) {
            w->hero.stats.setBase(Stat::AttackPower, Fixed(10));
            w->hero.stats.addFlat(Stat::AttackPower, Fixed(3));
            w->hero.stats.addMult(Stat::AttackPower, w->allocSourceId(), Fixed::fromPermille(150));
        }
        warm.hero.stats.warmAll();             // 한쪽만 캐시를 채운다
        CHECK(warm.hero.stats.dirty(Stat::AttackPower) == false);
        CHECK(cold.hero.stats.dirty(Stat::AttackPower) == true);
        CHECK_EQU(warm.checksum(), cold.checksum());

        // 그런데 상태가 바뀌면 반드시 갈려야 한다 (거짓 음성 방지).
        cold.hero.stats.addFlat(Stat::AttackPower, Fixed::fromRaw(1));
        CHECK(warm.checksum() != cold.checksum());
    }

    dctest::section("sourceId 발급 · 체크섬 반영");
    {
        World a, b;
        a.init(1);
        b.init(1);
        CHECK_EQ(a.peekSourceId(), 1u);
        CHECK_EQ(a.allocSourceId(), 1u);
        CHECK_EQ(a.allocSourceId(), 2u);
        CHECK_EQ(a.peekSourceId(), 3u);

        // 발급 횟수는 **다음 모디파이어의 적용 순서를 정하므로 상태다.**
        CHECK(a.checksum() != b.checksum());
        (void)b.allocSourceId();
        (void)b.allocSourceId();
        CHECK_EQU(a.checksum(), b.checksum());
    }

    dctest::section("전 스탯 독립");
    {
        // 한 스탯을 만졌는데 다른 스탯이 움직이면 인덱싱 버그다.
        StatBlock b = makeBlock(Fixed(100));
        b.addFlat(Stat::CritChance, Fixed(50));
        for (uint32_t i = 0; i < STAT_COUNT; ++i) {
            const Stat s = static_cast<Stat>(i);
            const int32_t want = (s == Stat::CritChance) ? Fixed(150).raw : Fixed(100).raw;
            if (b.value(s).raw != want) printf("    오염: %s\n", statName(s));
            CHECK_EQ(b.value(s).raw, want);
        }
    }

    return dctest::summary("test_stat_block");
}
