// 아이템과 조합식 (§5).
//
// ## RecipeTable은 시뮬 상태가 아니다
//
// 조합식은 런마다 변하지 않는 **정적 데이터**다. 그래서 `World` 밖에 살고,
// 역인덱스도 여기 있다. `World`에 넣으면
//   - 스냅샷·체크섬 비용이 런마다 같은 값을 반복해서 문다
//   - 8KB짜리 역인덱스가 매 스냅샷에 복사된다
//
// 대신 `Inventory`가 `dataHash()`를 들고 다닌다 — **서버와 클라가 다른
// items.json을 로드했으면 틱 0에서 체크섬이 갈린다.** StatBounds를 해시에
// 넣은 것과 같은 이유다.
//
// ## 포인터 대신 인자로 넘긴다
//
// `Inventory`가 `const RecipeTable*`를 멤버로 들면 `World`가 trivially copyable을
// 잃고, memcpy 스냅샷을 복원했을 때 매달린 포인터가 된다. 그래서 조회가 필요한
// 함수마다 테이블을 인자로 받는다.
#ifndef DC_ITEM_H
#define DC_ITEM_H

#include <cstdint>

#include "checksum.h"
#include "config.h"

namespace dc {

using ItemId = uint16_t;
constexpr ItemId ITEM_NONE = 0xFFFF;

// 등급 (§5). 전체 9단계 중 수직 슬라이스가 쓰는 5단계까지 정의한다.
// **뒤에만 덧붙인다** — 값이 바뀌면 리플레이가 깨진다.
enum class Grade : uint8_t {
    Common      = 0,   // 흔함 — 뽑기에서 나오는 유일한 등급
    Uncommon    = 1,   // 안흔함
    Special     = 2,   // 특별함
    Rare        = 3,   // 희귀함
    Legendary   = 4,   // 전설적인
    Hidden      = 5,   // 히든
    Transcend   = 6,   // 초월적인
    Immortal    = 7,   // 불멸의
    Eternal     = 8,   // 영원함
    Count       = 9,
};

// 데이터 저작 포맷 그대로 (§5). 같은 ItemId를 여러 번 넣으면 **동일 등급 3연성**이
// 표현되므로 스키마 변경이 필요 없다.
struct RecipeData {
    ItemId  result = ITEM_NONE;
    uint8_t count  = 0;
    ItemId  ingredients[config::MAX_RECIPE_INGREDIENTS] = {ITEM_NONE, ITEM_NONE, ITEM_NONE};
};

enum class RecipeTableStatus : int32_t {
    Ok             = 0,
    TooManyRecipes = 1,
    TooManyItems   = 2,
    BadIngredient  = 3,   // 재료 수가 0이거나 범위를 벗어난 ItemId
    BadResult      = 4,
    FanoutOverflow = 5,   // 한 아이템이 config::MAX_RECIPES_PER_ITEM개를 넘는 조합식에 등장
};

inline const char* recipeTableStatusName(RecipeTableStatus s) {
    switch (s) {
        case RecipeTableStatus::Ok:             return "ok";
        case RecipeTableStatus::TooManyRecipes: return "조합식 수 초과";
        case RecipeTableStatus::TooManyItems:   return "아이템 종류 수 초과";
        case RecipeTableStatus::BadIngredient:  return "재료 ID 오류";
        case RecipeTableStatus::BadResult:      return "결과물 ID 오류";
        case RecipeTableStatus::FanoutOverflow: return "역인덱스 팬아웃 초과";
    }
    return "?";
}

// 범위를 벗어난 조회가 돌려받는 값. count == 0 이라 어떤 인벤토리로도 조합되지
// 않으므로, 실수로 접근해도 **조용히 다른 레시피를 쓰는 일이 없다.**
inline constexpr RecipeData kInvalidRecipe{};

class RecipeTable {
public:
    // 로드 시 1회. 역인덱스를 함께 구축한다.
    //
    // **정렬 호출이 없다.** 레시피를 인덱스 순서(0..R-1)대로 훑으며 채우므로
    // 역인덱스 안의 순서가 곧 레시피 정의 순서다 — 비교자나 정렬 안정성에
    // 의존하지 않는다는 뜻이고, 그만큼 결정론 표면이 준다.
    RecipeTableStatus build(const RecipeData* recipes, uint32_t recipeCount,
                            uint32_t itemTypeCount) {
        if (recipeCount > config::MAX_RECIPES)     return RecipeTableStatus::TooManyRecipes;
        if (itemTypeCount > config::MAX_ITEM_TYPES) return RecipeTableStatus::TooManyItems;

        recipeCount_    = 0;
        itemTypeCount_  = itemTypeCount;
        for (uint32_t i = 0; i < config::MAX_ITEM_TYPES; ++i) fanout_[i] = 0;

        for (uint32_t r = 0; r < recipeCount; ++r) {
            const RecipeData& d = recipes[r];
            if (d.count == 0 || d.count > config::MAX_RECIPE_INGREDIENTS) {
                return RecipeTableStatus::BadIngredient;
            }
            if (d.result >= itemTypeCount) return RecipeTableStatus::BadResult;
            for (uint8_t k = 0; k < d.count; ++k) {
                if (d.ingredients[k] >= itemTypeCount) return RecipeTableStatus::BadIngredient;
            }
            recipes_[r] = d;

            // 같은 재료가 두 번 나와도 역인덱스에는 한 번만 넣는다 —
            // 3연성 조합식이 같은 레시피를 세 번 재평가하게 만들 이유가 없다.
            for (uint8_t k = 0; k < d.count; ++k) {
                const ItemId ing = d.ingredients[k];
                bool dup = false;
                for (uint8_t j = 0; j < k; ++j) if (d.ingredients[j] == ing) dup = true;
                if (dup) continue;
                if (fanout_[ing] >= config::MAX_RECIPES_PER_ITEM) {
                    return RecipeTableStatus::FanoutOverflow;
                }
                reverse_[ing][fanout_[ing]++] = static_cast<uint16_t>(r);
            }
        }
        recipeCount_ = recipeCount;
        computeHash();
        return RecipeTableStatus::Ok;
    }

    uint32_t recipeCount() const   { return recipeCount_; }
    uint32_t itemTypeCount() const { return itemTypeCount_; }
    const RecipeData& recipe(uint32_t i) const {
        return (i < recipeCount_ && i < config::MAX_RECIPES) ? recipes_[i] : kInvalidRecipe;
    }

    // "이 아이템으로 뭘 만들 수 있지?" — §5 조합 UI가 실제로 쓰는 연산.
    uint32_t recipesUsing(ItemId item, const uint16_t** out) const {
        if (item >= itemTypeCount_) { *out = nullptr; return 0; }
        *out = reverse_[item];
        return fanout_[item];
    }
    uint32_t fanout(ItemId item) const {
        return item < itemTypeCount_ ? fanout_[item] : 0u;
    }

    // 로드한 데이터의 지문. Inventory가 들고 다니며 체크섬에 넣는다.
    uint64_t dataHash() const { return dataHash_; }

private:
    void computeHash() {
        Hasher h;
        h.feed(recipeCount_);
        h.feed(itemTypeCount_);
        for (uint32_t r = 0; r < recipeCount_; ++r) {
            h.feed(recipes_[r].result);
            h.feed(recipes_[r].count);
            for (uint8_t k = 0; k < recipes_[r].count; ++k) h.feed(recipes_[r].ingredients[k]);
        }
        dataHash_ = h.value();
    }

    RecipeData recipes_[config::MAX_RECIPES]{};
    uint16_t   reverse_[config::MAX_ITEM_TYPES][config::MAX_RECIPES_PER_ITEM]{};
    uint8_t    fanout_[config::MAX_ITEM_TYPES]{};
    uint32_t   recipeCount_   = 0;
    uint32_t   itemTypeCount_ = 0;
    uint64_t   dataHash_      = 0;
};

}  // namespace dc

#endif  // DC_ITEM_H
