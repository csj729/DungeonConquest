// 인벤토리 + 조합 가능 캐시 (§5·§9).
//
// ## 갱신 전략이 모디파이어와 반대다 — 의도적이다
//
// | | 갱신 시점 | 이유 |
// |---|---|---|
// | 조건부 모디파이어 (`StatBlock`) | **지연(lazy)** — dirty만 세우고 읽을 때 재계산 | 쓰기가 잦고 읽기는 그때그때 |
// | 조합 가능 목록 (여기) | **즉시(eager)** — 인벤이 바뀌는 순간 재평가 | UI가 매 프레임 참조한다 |
//
// 헷갈리지 않도록 이름을 갈랐다. `StatBlock`은 `dirty()`를 노출하고, 여기는
// `craftable()`이 항상 최신값을 준다 — **여기에는 dirty 개념이 없다.**
//
// 공통 원칙은 같다: **전수 평가 금지, 영향받는 것만 다시 계산.**
// 역인덱스가 "이 아이템이 재료인 조합식"을 O(1)로 주므로, 아이템 하나가 바뀌면
// 최대 `config::MAX_RECIPES_PER_ITEM`개만 재평가한다.
//
// ## 체크섬
//
//   [상태] counts_ · tableHash_
//   [파생] craftableBits_   ← counts_와 RecipeTable에서 복원된다
#ifndef DC_INVENTORY_H
#define DC_INVENTORY_H

#include <cstdint>

#include "checksum.h"
#include "config.h"
#include "item.h"

namespace dc {

class Inventory {
public:
    static constexpr uint32_t BITS_WORDS = (config::MAX_RECIPES + 63) / 64;

    void init(const RecipeTable& table) {
        for (uint32_t i = 0; i < config::MAX_ITEM_TYPES; ++i) counts_[i] = 0;
        for (uint32_t i = 0; i < BITS_WORDS; ++i) craftableBits_[i] = 0;
        tableHash_     = table.dataHash();
        itemTypeCount_ = table.itemTypeCount();
    }

    uint16_t count(ItemId item) const {
        return item < config::MAX_ITEM_TYPES ? counts_[item] : uint16_t{0};
    }
    uint64_t tableHash() const { return tableHash_; }

    // init에 쓴 테이블과 같은가. **모든 table 인자 API가 먼저 이걸 본다** —
    // 캐시(craftableBits_)가 그 테이블 기준으로만 유효하기 때문이다.
    bool matches(const RecipeTable& table) const { return table.dataHash() == tableHash_; }

    // **인벤 슬롯은 제한하지 않는다**(§5). 상한은 종류당 개수(uint16)뿐이고,
    // 넘칠 것 같으면 **상태를 바꾸지 않고** false를 돌려준다.
    bool add(const RecipeTable& table, ItemId item, uint16_t n = 1) {
        if (!matches(table)) return false;
        if (item >= table.itemTypeCount() || n == 0) return false;
        if (static_cast<uint32_t>(counts_[item]) + n > 0xFFFFu) return false;
        counts_[item] = static_cast<uint16_t>(counts_[item] + n);
        refreshItem(table, item);
        return true;
    }

    bool remove(const RecipeTable& table, ItemId item, uint16_t n = 1) {
        if (!matches(table)) return false;
        if (item >= table.itemTypeCount() || n == 0) return false;
        if (counts_[item] < n) return false;
        counts_[item] = static_cast<uint16_t>(counts_[item] - n);
        refreshItem(table, item);
        return true;
    }

    // 캐시 조회 — O(1). UI가 매 프레임 부르는 경로다.
    bool craftable(uint32_t recipeIndex) const {
        if (recipeIndex >= config::MAX_RECIPES) return false;
        return (craftableBits_[recipeIndex >> 6] & (1ull << (recipeIndex & 63))) != 0;
    }

    // 캐시를 무시한 직접 계산. 테스트가 캐시와 대조하는 기준이다.
    //
    // 재료가 ≤3개라 중복을 세는 데 O(n²)를 써도 9번이다. **비트마스크를 쓰지
    // 않는 이유가 이것이다** — 카운트 배열 직접 조회가 이미 더 싸다
    // (실측: docs/portfolio.md §4).
    bool evaluate(const RecipeTable& table, uint32_t recipeIndex) const {
        if (!matches(table)) return false;
        if (recipeIndex >= table.recipeCount()) return false;
        const RecipeData& r = table.recipe(recipeIndex);
        if (r.count == 0) return false;
        for (uint8_t i = 0; i < r.count; ++i) {
            const ItemId ing = r.ingredients[i];
            uint16_t need = 0;
            for (uint8_t k = 0; k < r.count; ++k) if (r.ingredients[k] == ing) ++need;
            if (counts_[ing] < need) return false;
        }
        return true;
    }

    // 조합 실행 — 재료 소모 + 결과물 생성 (§5: 골드가 들지 않는다).
    // **결과물 오버플로를 먼저 확인한다** — 재료만 사라지는 상태를 만들지 않기 위해.
    bool craft(const RecipeTable& table, uint32_t recipeIndex) {
        // **테이블이 다르면 캐시가 거짓말을 한다.** `craftableBits_`는 init 때의
        // 테이블 기준이라, 더 작은 테이블을 넘기면 범위 밖 인덱스가 true로 남아
        // `recipe()`가 `kInvalidRecipe`를 준다 — 그 result는 ITEM_NONE(0xFFFF)이고
        // 곧바로 `counts_[65535]` 읽기·쓰기가 된다(counts_는 MAX_ITEM_TYPES칸).
        // ASan에서 SEGV로 재현된 경로다. 두 겹으로 막는다.
        if (!matches(table)) return false;
        if (recipeIndex >= table.recipeCount()) return false;
        if (!craftable(recipeIndex)) return false;
        const RecipeData& r = table.recipe(recipeIndex);
        if (r.count == 0 || r.result >= config::MAX_ITEM_TYPES) return false;
        if (static_cast<uint32_t>(counts_[r.result]) + 1u > 0xFFFFu) return false;

        for (uint8_t i = 0; i < r.count; ++i) --counts_[r.ingredients[i]];
        ++counts_[r.result];

        // 바뀐 아이템 전부에 대해 역인덱스로 재평가한다. 결과물이 다른 조합식의
        // 재료일 수 있으므로 결과물도 포함한다 (§5 다경로 설계의 직접 귀결).
        for (uint8_t i = 0; i < r.count; ++i) {
            bool dup = false;
            for (uint8_t k = 0; k < i; ++k) if (r.ingredients[k] == r.ingredients[i]) dup = true;
            if (!dup) refreshItem(table, r.ingredients[i]);
        }
        refreshItem(table, r.result);
        return true;
    }

    // §5 조합 UI — "이 아이템을 재료로 쓰는 조합식" 목록.
    // 보유 여부와 무관하게 전부 돌려준다 (없는 재료는 흑백 표시).
    // 순서는 레시피 정의 순서다.
    uint32_t recipesUsing(const RecipeTable& table, ItemId item,
                          uint16_t* out, uint32_t outMax) const {
        const uint16_t* list = nullptr;
        const uint32_t n = table.recipesUsing(item, &list);
        const uint32_t take = n < outMax ? n : outMax;
        for (uint32_t i = 0; i < take; ++i) out[i] = list[i];
        return take;
    }

    // 전수 재평가. 스냅샷 로드 직후나 데이터 재로드 후에만 쓴다.
    // **평시 경로가 아니다** — 평시에는 refreshItem이 영향받는 것만 본다.
    void rebuildCraftable(const RecipeTable& table) {
        for (uint32_t i = 0; i < BITS_WORDS; ++i) craftableBits_[i] = 0;
        for (uint32_t r = 0; r < table.recipeCount(); ++r) setBit(r, evaluate(table, r));
        tableHash_     = table.dataHash();
        itemTypeCount_ = table.itemTypeCount();
    }

    uint32_t craftableCount(const RecipeTable& table) const {
        uint32_t n = 0;
        for (uint32_t r = 0; r < table.recipeCount(); ++r) if (craftable(r)) ++n;
        return n;
    }

    // [파생]인 craftableBits_는 넣지 않는다 — 지연/즉시 평가 차이가 거짓 양성이 된다.
    // tableHash_는 넣는다 — 서버와 클라가 다른 items.json을 로드했으면 여기서 갈린다.
    void hashInto(Hasher& h) const {
        h.feed(tableHash_);
        h.feed(itemTypeCount_);
        // **용량 전체가 아니라 실제 아이템 종류 수만큼만 훑는다.**
        // 1024칸을 매번 접으면 체크섬 비용이 엔티티 전체와 맞먹는다.
        const uint32_t n = itemTypeCount_ < config::MAX_ITEM_TYPES
                         ? itemTypeCount_ : config::MAX_ITEM_TYPES;
        for (uint32_t i = 0; i < n; ++i) h.feed(counts_[i]);
    }

private:
    void setBit(uint32_t i, bool v) {
        const uint64_t mask = 1ull << (i & 63);
        if (v) craftableBits_[i >> 6] |= mask;
        else   craftableBits_[i >> 6] &= ~mask;
    }

    // 아이템 하나가 바뀌었을 때 영향받는 조합식만 재평가한다.
    void refreshItem(const RecipeTable& table, ItemId item) {
        const uint16_t* list = nullptr;
        const uint32_t n = table.recipesUsing(item, &list);
        for (uint32_t i = 0; i < n; ++i) setBit(list[i], evaluate(table, list[i]));
    }

    // [상태]
    uint16_t counts_[config::MAX_ITEM_TYPES]{};
    uint64_t tableHash_     = 0;
    uint32_t itemTypeCount_ = 0;
    // [파생]
    uint64_t craftableBits_[BITS_WORDS]{};
};

}  // namespace dc

#endif  // DC_INVENTORY_H
