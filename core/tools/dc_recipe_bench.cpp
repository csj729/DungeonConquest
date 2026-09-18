// 조합식 조회 구조 재측정 — `tools/recipe_structure_bench.cpp`를 **실제 코어 타입**으로.
//
// 원래 벤치는 std::vector 기반 모델이었다. 여기서는 `RecipeTable` + `Inventory`,
// 즉 출시될 코드를 그대로 잰다. PRD에서 파이썬 모델과 C++ 구현을 따로 잰 것과 같은
// 이유다 — **모델이 맞아도 구현이 틀릴 수 있다.**
//
// std::chrono는 여기서만 허용한다. 개발 도구지 시뮬 코어가 아니다.
#include <chrono>
#include <cstdio>
#include <cstdlib>

#include "../include/dc/inventory.h"
#include "../include/dc/item.h"
#include "../include/dc/rng.h"

using namespace dc;
using Clock = std::chrono::steady_clock;

// 비교군 1 — 역인덱스 없이 전수 스캔으로 "이 아이템을 쓰는 레시피" 찾기
static uint32_t reverseScan(const RecipeTable& t, ItemId item, uint16_t* out, uint32_t max) {
    uint32_t n = 0;
    for (uint32_t r = 0; r < t.recipeCount() && n < max; ++r) {
        const RecipeData& d = t.recipe(r);
        for (uint8_t k = 0; k < d.count; ++k) {
            if (d.ingredients[k] == item) { out[n++] = static_cast<uint16_t>(r); break; }
        }
    }
    return n;
}

// 비교군 2 — 증분 캐시 없이 매번 전수 재평가
static uint32_t craftableNaive(const RecipeTable& t, const Inventory& inv) {
    uint32_t n = 0;
    for (uint32_t r = 0; r < t.recipeCount(); ++r) if (inv.evaluate(t, r)) ++n;
    return n;
}

struct Scenario {
    const char* name;
    uint32_t    items;
    uint32_t    recipes;
    bool        clustered;   // 재료 ID가 축별로 인접한가
};

static RecipeTable makeTable(const Scenario& sc, Rng& rng) {
    static RecipeData buf[config::MAX_RECIPES];
    const uint32_t base = sc.items - sc.recipes;   // 앞쪽은 재료, 뒤쪽은 결과물
    for (uint32_t r = 0; r < sc.recipes; ++r) {
        buf[r].result = static_cast<ItemId>(base + r);
        buf[r].count  = static_cast<uint8_t>(2 + rng.range(2));
        const uint32_t cluster = sc.clustered ? (rng.range(base / 16 + 1) * 16) : 0;
        for (uint8_t k = 0; k < buf[r].count; ++k) {
            const uint32_t id = sc.clustered ? (cluster + rng.range(16)) % base : rng.range(base);
            buf[r].ingredients[k] = static_cast<ItemId>(id);
        }
    }
    RecipeTable t;
    // 팬아웃이 넘치면 레시피를 줄여가며 다시 시도한다 (벤치 편의).
    uint32_t n = sc.recipes;
    while (n > 0 && t.build(buf, n, sc.items) != RecipeTableStatus::Ok) --n;
    if (t.itemTypeCount() == 0) {
        printf("  !! %s: 테이블을 만들지 못했다 (용량 한계) — 건너뜀\n", sc.name);
    }
    return t;
}

template <typename F>
static double benchNs(int iters, F&& f) {
    const auto t0 = Clock::now();
    for (int i = 0; i < iters; ++i) f(i);
    const auto t1 = Clock::now();
    return std::chrono::duration<double, std::nano>(t1 - t0).count() / iters;
}

int main() {
    const Scenario scenarios[] = {
        {"수직 슬라이스 51종/42개", 51, 42, false},
        {"10배 성장 600종/500개(무작위)", 600, 500, false},
        {"10배 성장 600종/500개(클러스터)", 600, 500, true},
    };

    printf("조합식 조회 구조 — 실제 코어 타입(RecipeTable + Inventory) 실측\n");
    printf("%-34s %14s %14s %8s\n", "시나리오", "전수 스캔", "역인덱스", "배수");
    for (const Scenario& sc : scenarios) {
        Rng rng = Rng::derive(20250918, RngStream::Items);
        const RecipeTable t = makeTable(sc, rng);
        if (t.itemTypeCount() == 0) continue;
        Inventory inv;
        inv.init(t);

        uint16_t out[64];
        volatile uint32_t sink = 0;
        const double scan = benchNs(200000, [&](int i) {
            sink += reverseScan(t, static_cast<ItemId>(i % t.itemTypeCount()), out, 64);
        });
        const double idx = benchNs(200000, [&](int i) {
            sink += inv.recipesUsing(t, static_cast<ItemId>(i % t.itemTypeCount()), out, 64);
        });
        printf("%-34s %11.0f ns %11.0f ns %7.1fx\n", sc.name, scan, idx, scan / idx);
        (void)sink;
    }

    printf("\n%-34s %14s %14s %8s\n", "시나리오", "전수 재평가", "증분 갱신", "배수");
    for (const Scenario& sc : scenarios) {
        Rng rng = Rng::derive(20250918, RngStream::Items);
        const RecipeTable t = makeTable(sc, rng);
        if (t.itemTypeCount() == 0) continue;
        Inventory inv;
        inv.init(t);
        // 절반쯤 조합 가능한 상태로 채운다 — 최악도 최선도 아닌 지점
        for (uint32_t i = 0; i < t.itemTypeCount(); ++i) {
            if (rng.chancePermille(600)) inv.add(t, static_cast<ItemId>(i), 2);
        }

        volatile uint32_t sink = 0;
        const double naive = benchNs(100000, [&](int i) {
            (void)i;
            sink += craftableNaive(t, inv);
        });
        // 아이템 1개 변경 후 갱신 — 평시 경로
        Inventory work = inv;
        const double incr = benchNs(200000, [&](int i) {
            const ItemId id = static_cast<ItemId>(i % t.itemTypeCount());
            if (i % 2 == 0) work.add(t, id, 1);
            else            work.remove(t, id, 1);
            sink += work.craftable(0) ? 1u : 0u;
        });
        printf("%-34s %11.0f ns %11.0f ns %7.1fx\n", sc.name, naive, incr, naive / incr);
        (void)sink;
    }

    printf("\n조회(craftable)는 비트 하나 읽기라 측정 대상이 아니다 — 항상 O(1).\n");
    return 0;
}
