// 인벤토리 + 조합식 평가 테스트 (§5·§9).
//
// 핵심은 하나다: **증분 캐시가 전수 재계산과 항상 같은가.**
// 갱신을 빠뜨리면 "조합 버튼이 안 켜진다" 또는 "눌렀는데 실패한다"가 되고,
// 증상이 인벤 조작 순서에 의존해서 재현이 어렵다.
#include <initializer_list>

#include "../include/dc/inventory.h"
#include "../include/dc/item.h"
#include "../include/dc/world.h"
#include "test_main.h"

using namespace dc;

// data/items.json의 구조를 그대로 흉내낸 최소 테이블.
//   아이템 0~4 = 흔함 C1~C5
//   5 = C1+C2, 6 = C1+C3, 7 = C2+C5, 8 = 5+6 (상위 조합), 9 = 7×3 (동일 등급 3연성)
enum : ItemId { C1 = 0, C2, C3, C4, C5, R_A, R_B, R_C, R_UP, R_TRIPLE, ITEM_COUNT };

static RecipeData mk(ItemId result, ItemId a, ItemId b, ItemId c = ITEM_NONE) {
    RecipeData d;
    d.result = result;
    d.ingredients[0] = a;
    d.ingredients[1] = b;
    d.count = 2;
    if (c != ITEM_NONE) { d.ingredients[2] = c; d.count = 3; }
    return d;
}

static RecipeTable makeTable() {
    const RecipeData recipes[] = {
        mk(R_A,      C1, C2),
        mk(R_B,      C1, C3),
        mk(R_C,      C2, C5),
        mk(R_UP,     R_A, R_B),          // 상위 조합 — 결과물이 다시 재료가 된다
        mk(R_TRIPLE, R_C, R_C, R_C),     // 동일 등급 3연성 (§5)
    };
    RecipeTable t;
    const RecipeTableStatus st = t.build(recipes, 5, ITEM_COUNT);
    if (st != RecipeTableStatus::Ok) printf("    테이블 빌드 실패: %s\n", recipeTableStatusName(st));
    return t;
}

// 캐시를 믿지 않고 전수 재계산과 대조한다.
static bool cacheAgrees(const Inventory& inv, const RecipeTable& t) {
    for (uint32_t r = 0; r < t.recipeCount(); ++r) {
        if (inv.craftable(r) != inv.evaluate(t, r)) {
            printf("    캐시 불일치: 레시피 %u (캐시 %d, 실제 %d)\n",
                   r, inv.craftable(r) ? 1 : 0, inv.evaluate(t, r) ? 1 : 0);
            return false;
        }
    }
    return true;
}

int main() {
    printf("test_inventory\n");

    const RecipeTable table = makeTable();

    dctest::section("테이블 빌드 · 역인덱스");
    {
        CHECK_EQ(table.recipeCount(), 5u);
        CHECK_EQ(table.itemTypeCount(), static_cast<uint32_t>(ITEM_COUNT));

        // C1은 레시피 0(C1+C2)과 1(C1+C3)에 재료로 들어간다.
        const uint16_t* list = nullptr;
        CHECK_EQ(table.recipesUsing(C1, &list), 2u);
        CHECK_EQ(list[0], 0u);
        CHECK_EQ(list[1], 1u);   // 순서는 레시피 정의 순서 — 정렬 호출이 없다

        // **3연성은 같은 재료를 세 번 쓰지만 역인덱스에는 한 번만 들어간다.**
        // 아니면 아이템 하나가 바뀔 때 같은 레시피를 세 번 재평가한다.
        CHECK_EQ(table.fanout(R_C), 1u);

        // 결과물로만 쓰이는 아이템은 역인덱스가 비어 있다.
        CHECK_EQ(table.fanout(R_UP), 0u);
        CHECK_EQ(table.fanout(C4), 0u);
    }

    dctest::section("테이블 빌드 거부 조건");
    {
        RecipeTable t;
        RecipeData bad = mk(R_A, C1, C2);

        bad.count = 0;
        CHECK_EQ(static_cast<int32_t>(t.build(&bad, 1, ITEM_COUNT)),
                 static_cast<int32_t>(RecipeTableStatus::BadIngredient));

        bad = mk(R_A, C1, C2);
        bad.ingredients[1] = 999;
        CHECK_EQ(static_cast<int32_t>(t.build(&bad, 1, ITEM_COUNT)),
                 static_cast<int32_t>(RecipeTableStatus::BadIngredient));

        bad = mk(999, C1, C2);
        CHECK_EQ(static_cast<int32_t>(t.build(&bad, 1, ITEM_COUNT)),
                 static_cast<int32_t>(RecipeTableStatus::BadResult));

        // 팬아웃 초과 — 한 아이템이 17개 조합식에 재료로 등장
        RecipeData many[17];
        for (uint32_t i = 0; i < 17; ++i) many[i] = mk(R_A, C1, C2);
        CHECK_EQ(static_cast<int32_t>(t.build(many, 17, ITEM_COUNT)),
                 static_cast<int32_t>(RecipeTableStatus::FanoutOverflow));
    }

    dctest::section("조합 가능 판정");
    {
        Inventory inv;
        inv.init(table);
        CHECK_EQ(inv.craftableCount(table), 0u);
        CHECK(cacheAgrees(inv, table));

        CHECK(inv.add(table, C1));
        CHECK(cacheAgrees(inv, table));
        CHECK(!inv.craftable(0));      // C1만으로는 부족

        CHECK(inv.add(table, C2));
        CHECK(inv.craftable(0));       // C1+C2 → R_A
        CHECK(!inv.craftable(1));      // C3이 없다
        CHECK(!inv.craftable(2));      // C5가 없다
        CHECK(cacheAgrees(inv, table));

        // 재료를 빼면 즉시 꺼진다 — 지연이 아니라 즉시 갱신이다.
        CHECK(inv.remove(table, C2));
        CHECK(!inv.craftable(0));
        CHECK(cacheAgrees(inv, table));
    }

    dctest::section("동일 등급 3연성 — 중복 재료 개수를 센다");
    {
        Inventory inv;
        inv.init(table);
        inv.add(table, R_C, 1);
        CHECK(!inv.craftable(4));      // 1개로는 안 된다
        inv.add(table, R_C, 1);
        CHECK(!inv.craftable(4));      // 2개도 안 된다
        inv.add(table, R_C, 1);
        CHECK(inv.craftable(4));       // 3개째에 켜진다
        CHECK(cacheAgrees(inv, table));

        CHECK(inv.craft(table, 4));
        CHECK_EQ(inv.count(R_C), 0u);  // 3개 전부 소모
        CHECK_EQ(inv.count(R_TRIPLE), 1u);
        CHECK(!inv.craftable(4));
        CHECK(cacheAgrees(inv, table));
    }

    dctest::section("조합 실행 — 결과물이 다시 재료가 된다");
    {
        Inventory inv;
        inv.init(table);
        inv.add(table, C1, 2);
        inv.add(table, C2, 1);
        inv.add(table, C3, 1);
        CHECK(inv.craftable(0) && inv.craftable(1));
        CHECK(!inv.craftable(3));      // R_UP은 R_A+R_B가 필요

        CHECK(inv.craft(table, 0));    // → R_A
        CHECK_EQ(inv.count(C1), 1u);
        CHECK_EQ(inv.count(C2), 0u);
        CHECK_EQ(inv.count(R_A), 1u);
        CHECK(!inv.craftable(0));      // C2가 떨어졌다
        CHECK(inv.craftable(1));       // C1+C3은 아직 가능
        CHECK(cacheAgrees(inv, table));

        CHECK(inv.craft(table, 1));    // → R_B
        // **여기가 핵심** — 결과물이 다른 조합식의 재료라서 R_UP이 켜져야 한다.
        CHECK(inv.craftable(3));
        CHECK(cacheAgrees(inv, table));

        CHECK(inv.craft(table, 3));    // → R_UP
        CHECK_EQ(inv.count(R_UP), 1u);
        CHECK_EQ(inv.count(R_A), 0u);
        CHECK_EQ(inv.count(R_B), 0u);
        CHECK(!inv.craftable(3));
        CHECK(cacheAgrees(inv, table));
    }

    dctest::section("거부 경로는 상태를 바꾸지 않는다");
    {
        Inventory inv;
        inv.init(table);
        inv.add(table, C1, 5);
        const uint16_t before = inv.count(C1);

        CHECK(!inv.craft(table, 0));            // 재료 부족
        CHECK_EQ(inv.count(C1), before);        // C1이 사라지지 않았다
        CHECK(!inv.remove(table, C2, 1));       // 없는 것 제거
        CHECK(!inv.add(table, 999));            // 범위 밖
        CHECK(!inv.remove(table, 999));
        CHECK(!inv.add(table, C1, 0));          // 0개 추가
        CHECK_EQ(inv.count(C1), before);

        // uint16 상한 — 넘칠 것 같으면 상태를 바꾸지 않는다.
        Inventory big;
        big.init(table);
        CHECK(big.add(table, C4, 65535));
        CHECK(!big.add(table, C4, 1));
        CHECK_EQ(big.count(C4), 65535u);
    }

    dctest::section("증분 캐시 == 전수 재계산 (무작위 조작)");
    {
        // 임의의 조작 순서에서도 캐시가 전수 재계산과 어긋나지 않아야 한다.
        // **순서 의존 버그는 이런 식으로만 잡힌다.**
        Inventory inv;
        inv.init(table);
        Rng r = Rng::derive(20250918, RngStream::Items);
        int crafted = 0;
        for (int step = 0; step < 20000; ++step) {
            switch (r.range(4)) {
                case 0: inv.add(table, static_cast<ItemId>(r.range(ITEM_COUNT)),
                                static_cast<uint16_t>(1 + r.range(3))); break;
                case 1: inv.remove(table, static_cast<ItemId>(r.range(ITEM_COUNT)),
                                   static_cast<uint16_t>(1 + r.range(2))); break;
                default: {
                    const uint32_t ri = r.range(table.recipeCount());
                    if (inv.craft(table, ri)) ++crafted;
                    break;
                }
            }
            if (!cacheAgrees(inv, table)) { CHECK(false); break; }
        }
        CHECK(crafted > 0);

        // 전수 재구축과도 일치해야 한다 (스냅샷 로드 경로).
        Inventory copy = inv;
        copy.rebuildCraftable(table);
        bool same = true;
        for (uint32_t i = 0; i < table.recipeCount(); ++i) {
            if (copy.craftable(i) != inv.craftable(i)) same = false;
        }
        CHECK(same);
        printf("    2만 회 조작 중 조합 성공 %d회, 캐시 항상 일치\n", crafted);
    }

    dctest::section("조합 UI — 이 아이템으로 뭘 만들 수 있지");
    {
        Inventory inv;
        inv.init(table);
        uint16_t out[8];
        // 보유 여부와 무관하게 전부 돌려준다 (없는 재료는 흑백 표시 — §5).
        CHECK_EQ(inv.recipesUsing(table, C1, out, 8), 2u);
        CHECK_EQ(out[0], 0u);
        CHECK_EQ(out[1], 1u);
        CHECK_EQ(inv.recipesUsing(table, C4, out, 8), 0u);

        // 출력 버퍼가 작으면 잘라서 준다 (넘치지 않는다).
        CHECK_EQ(inv.recipesUsing(table, C1, out, 1), 1u);
    }

    dctest::section("체크섬 — counts는 [상태], craftable은 [파생]");
    {
        World a, b;
        a.init(5);
        b.init(5);
        a.inventory.init(table);
        b.inventory.init(table);
        CHECK_EQU(a.checksum(), b.checksum());

        // 개수가 다르면 갈린다.
        a.inventory.add(table, C1, 3);
        CHECK(a.checksum() != b.checksum());
        b.inventory.add(table, C1, 3);
        CHECK_EQU(a.checksum(), b.checksum());

        // **캐시 상태만 다른 두 World는 같은 해시여야 한다.**
        // 한쪽만 전수 재구축해도 관측 가능한 값은 동일하므로.
        a.inventory.rebuildCraftable(table);
        CHECK_EQU(a.checksum(), b.checksum());

        // 다른 items.json을 로드하면 틱 0에서 갈린다.
        const RecipeData other[] = {mk(R_A, C1, C3)};
        RecipeTable t2;
        t2.build(other, 1, ITEM_COUNT);
        CHECK(t2.dataHash() != table.dataHash());
        b.inventory.init(t2);
        b.inventory.add(table, C1, 3);
        CHECK(a.checksum() != b.checksum());
    }

    dctest::section("테이블 불일치 — 다른 RecipeTable은 거부한다");
    {
        // **회귀 테스트.** `craftableBits_`는 init에 쓴 테이블 기준이다. 더 작은
        // 테이블을 넘기면 범위 밖 인덱스가 true로 남아 `recipe()`가 kInvalidRecipe를
        // 주고, 그 result는 ITEM_NONE(0xFFFF)이라 counts_[65535] 읽기·쓰기가 됐다.
        // counts_는 MAX_ITEM_TYPES칸뿐이라 ASan에서 SEGV로 재현되던 경로다.
        const RecipeData onlyOne[] = {mk(R_A, C1, C2)};
        RecipeTable small;
        CHECK_EQ(static_cast<int32_t>(small.build(onlyOne, 1, ITEM_COUNT)),
                 static_cast<int32_t>(RecipeTableStatus::Ok));
        CHECK(small.dataHash() != table.dataHash());

        Inventory inv;
        inv.init(table);
        inv.add(table, C1, 5);
        inv.add(table, C2, 5);
        inv.add(table, C3, 5);

        // 캐시는 큰 테이블 기준으로 true인데, 작은 테이블에는 그 레시피가 없다
        CHECK(inv.craftable(1));
        CHECK_EQ(small.recipeCount(), 1u);
        CHECK(!inv.craft(small, 1));              // 예전엔 여기서 OOB를 밟았다

        // 다른 테이블이면 전부 거부한다 — 캐시가 그 테이블 기준이 아니기 때문이다
        CHECK(!inv.matches(small));
        CHECK(!inv.add(small, C1, 1));
        CHECK(!inv.remove(small, C1, 1));
        CHECK(!inv.evaluate(small, 0));
        CHECK(!inv.craft(small, 0));

        // 같은 테이블이면 정상 동작은 그대로다
        CHECK(inv.matches(table));
        CHECK(inv.add(table, C1, 1));
        printf("    다른 테이블 5개 API 전부 거부 · 같은 테이블은 정상\n");
    }

    dctest::section("결정론 — 같은 각본이면 같은 인벤");
    {
        auto play = [&](uint64_t seed) {
            Inventory inv;
            inv.init(table);
            Rng r = Rng::derive(seed, RngStream::Items);
            for (int i = 0; i < 5000; ++i) {
                inv.add(table, static_cast<ItemId>(r.range(5)), 1);
                (void)inv.craft(table, r.range(table.recipeCount()));
            }
            Hasher h;
            inv.hashInto(h);
            return h.value();
        };
        CHECK_EQU(play(1), play(1));
        CHECK(play(1) != play(2));
    }

    return dctest::summary("test_inventory");
}
