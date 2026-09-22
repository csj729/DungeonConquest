// 조합 UI 조회 (§5) — **프레젠테이션이 읽고 시뮬은 안 읽는다.**
//
// 인벤토리 UI는 전투 중에도 상시 떠 있고, 아이템을 누르면 그 아이템을 재료로 쓰는
// 조합식이 뜬다. 그 화면이 필요로 하는 값을 시뮬 쪽에서 한 번에 준다 — UI가
// `Inventory`·`RecipeTable`을 직접 파헤치면 같은 계산이 두 벌이 되고, 한쪽만
// 고치는 사고가 난다 (조합 가능 판정이 UI와 시뮬에서 갈리면 눌러도 안 되는
// 버튼이 생긴다).
//
// **여기에 시뮬 상태를 바꾸는 함수는 없다.** 전부 const 조회다.
#ifndef DC_CRAFT_QUERY_H
#define DC_CRAFT_QUERY_H

#include <cstdint>

#include "inventory.h"
#include "item.h"
#include "sim_config.h"

namespace dc {

// 재료 한 칸의 상태. UI가 아이콘을 어떻게 그릴지가 여기서 정해진다.
struct IngredientView {
    ItemId   item    = ITEM_NONE;
    uint16_t need    = 0;   // 이 조합식이 요구하는 개수
    uint16_t have    = 0;   // 지금 보유 개수
    bool     enough() const { return have >= need; }
};

// 조합식 하나의 상태.
//
// **`ready` 하나만 주지 않는 이유**: 플레이어가 실제로 하는 판단은 "지금 만들까"가
// 아니라 **"무엇을 노릴까"**다. 재료가 한 종류만 모자란 조합식은 "멀었음"과 전혀
// 다른 상태이므로 UI가 구분해 보여줄 수 있어야 한다.
struct RecipeView {
    uint32_t recipeIndex  = 0;
    ItemId   result       = ITEM_NONE;
    uint8_t  count        = 0;                 // 재료 칸 수
    IngredientView ingredients[config::MAX_RECIPE_INGREDIENTS]{};

    uint8_t  missingKinds = 0;   // 모자란 재료 **종류** 수 (0이면 지금 가능)
    uint16_t missingTotal = 0;   // 모자란 **개수** 합

    bool ready() const { return missingKinds == 0; }
};

inline uint16_t neededOf(const RecipeData& r, ItemId item) {
    uint16_t n = 0;
    for (uint8_t i = 0; i < r.count; ++i) if (r.ingredients[i] == item) ++n;
    return n;
}

// 조합식 하나를 UI가 쓸 형태로 편다. 중복 재료는 한 칸으로 접고 need에 수량을 담는다 —
// **같은 재료 2개를 요구하는 조합식에서 "보유/미보유" 색만으로는 거짓말이 된다.**
inline RecipeView viewRecipe(const Inventory& inv, const RecipeTable& table,
                             uint32_t recipeIndex) {
    RecipeView v;
    v.recipeIndex = recipeIndex;
    if (recipeIndex >= table.recipeCount()) return v;
    const RecipeData& r = table.recipe(recipeIndex);
    v.result = r.result;

    for (uint8_t i = 0; i < r.count; ++i) {
        const ItemId ing = r.ingredients[i];
        bool dup = false;
        for (uint8_t k = 0; k < i; ++k) if (r.ingredients[k] == ing) dup = true;
        if (dup) continue;
        IngredientView iv;
        iv.item = ing;
        iv.need = neededOf(r, ing);
        iv.have = inv.count(ing);
        if (!iv.enough()) {
            ++v.missingKinds;
            v.missingTotal = static_cast<uint16_t>(v.missingTotal + (iv.need - iv.have));
        }
        v.ingredients[v.count++] = iv;
    }

    return v;
}

// 조합하면 스탯이 얼마나 바뀌는가 — **결과물 − 재료 합**.
//
// 사다리상 항상 이득이지만(상위 1개 = 하위 2개 × 1.25), **그게 눈에 보여야**
// 플레이어가 "조합은 하면 이득"이라는 규칙을 학습한다. 숫자를 안 보여주면
// 재료를 쌓아두는 쪽이 안전해 보인다.
//
// `SimConfig`를 받는 것은 아이템 스탯 표가 거기 살기 때문이다. 재료·결과물 조회만
// 하는 `viewRecipe`와 분리해 둔다 — 스탯 표가 필요 없는 호출자가 대부분이다.
inline void craftStatDelta(const SimConfig& cfg, const RecipeTable& table,
                           uint32_t recipeIndex, int32_t* outPermille) {
    for (uint32_t s = 0; s < STAT_COUNT; ++s) outPermille[s] = 0;
    if (recipeIndex >= table.recipeCount()) return;
    const RecipeData& r = table.recipe(recipeIndex);
    const uint32_t cap = cfg.itemTypeCount < DC_MAX_ITEM_TYPES_CFG
                       ? cfg.itemTypeCount : DC_MAX_ITEM_TYPES_CFG;

    if (r.result < cap) {
        for (uint32_t s = 0; s < STAT_COUNT; ++s) outPermille[s] += cfg.itemStats[r.result][s];
    }
    for (uint8_t i = 0; i < r.count; ++i) {
        const ItemId ing = r.ingredients[i];
        if (ing >= cap) continue;
        for (uint32_t s = 0; s < STAT_COUNT; ++s) outPermille[s] -= cfg.itemStats[ing][s];
    }
}

// 한 아이템을 재료로 쓰는 조합식 전부. 아이템을 눌렀을 때 뜨는 목록이다.
// **결과를 `ready` → `missingTotal` 오름차순으로 정렬하지 않는다** — 정렬은
// 프레젠테이션의 선택이고, 여기서 정하면 UI가 다른 순서를 원할 때 갈 곳이 없다.
// 순서는 `RecipeTable`의 역인덱스 순서, 즉 조합식 정의 순서로 고정이다.
inline uint32_t viewRecipesUsing(const Inventory& inv, const RecipeTable& table,
                                 ItemId item, RecipeView* out, uint32_t outMax) {
    const uint16_t* idx = nullptr;
    const uint32_t n = table.recipesUsing(item, &idx);
    uint32_t written = 0;
    for (uint32_t i = 0; i < n && written < outMax; ++i) {
        out[written++] = viewRecipe(inv, table, idx[i]);
    }
    return written;
}

}  // namespace dc

#endif  // DC_CRAFT_QUERY_H
