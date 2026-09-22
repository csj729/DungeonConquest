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
    // 이 조합을 하면 재료가 사라져 **만들 수 없게 되는 다른 조합식** 수.
    // §5의 다경로 설계(재료 공유)에서 나오는 값이고, "철저한 빌드 설계"의 판단이
    // 정확히 여기다 — 지금 만들면 무엇을 포기하는가.
    uint8_t  blocksOthers = 0;

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

    // 이 조합을 실행하면 막히는 다른 조합식 세기.
    // 지금 만들 수 있는 조합식 중, 재료가 소모된 뒤에는 못 만들게 되는 것들이다.
    if (v.ready()) {
        for (uint32_t other = 0; other < table.recipeCount(); ++other) {
            if (other == recipeIndex) continue;
            if (!inv.craftable(other)) continue;
            const RecipeData& o = table.recipe(other);
            bool blocked = false;
            for (uint8_t i = 0; i < o.count && !blocked; ++i) {
                const ItemId ing = o.ingredients[i];
                const uint16_t left = static_cast<uint16_t>(inv.count(ing) - neededOf(r, ing));
                if (inv.count(ing) < neededOf(r, ing) || left < neededOf(o, ing)) blocked = true;
            }
            if (blocked && v.blocksOthers < 255) ++v.blocksOthers;
        }
    }
    return v;
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
