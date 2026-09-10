// 조합식 자료구조 벤치마크. Release(-O2) 빌드로 실측한다.
// std::chrono는 여기서만 허용 — 개발 도구지 시뮬 코어가 아니다.
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

using Clock = std::chrono::high_resolution_clock;

struct Recipe {
    uint16_t result;
    uint8_t  ingredientCount;
    uint16_t ingredients[3];
};

// ── 시나리오: 현재 수직 슬라이스(51종/42개) vs 10배 성장(600종/500개) ──
struct Scenario {
    const char* name;
    int itemCount;
    int recipeCount;
};

std::vector<Recipe> makeRecipes(int itemCount, int recipeCount, std::mt19937& rng,
                                 bool clustered = false, int clusterSize = 16) {
    std::vector<Recipe> recipes(recipeCount);
    std::uniform_int_distribution<int> itemDist(0, itemCount - 1);
    std::uniform_int_distribution<int> countDist(2, 3);
    std::uniform_int_distribution<int> clusterStart(0, std::max(1, itemCount - clusterSize));
    std::uniform_int_distribution<int> clusterOffset(0, clusterSize - 1);
    for (int r = 0; r < recipeCount; ++r) {
        recipes[r].result = static_cast<uint16_t>(itemCount + r);
        recipes[r].ingredientCount = static_cast<uint8_t>(countDist(rng));
        if (clustered) {
            // 실제 데이터처럼 재료가 같은 축(=인접 ID 구간)에서 뽑히는 상황을 흉내
            int base = clusterStart(rng);
            for (int k = 0; k < recipes[r].ingredientCount; ++k)
                recipes[r].ingredients[k] = static_cast<uint16_t>(base + clusterOffset(rng));
        } else {
            for (int k = 0; k < recipes[r].ingredientCount; ++k)
                recipes[r].ingredients[k] = static_cast<uint16_t>(itemDist(rng));
        }
    }
    return recipes;
}

// A안: 평탄 배열 그대로. 역방향 조회 = 전수 스캔
int reverseLookup_LinearScan(const std::vector<Recipe>& recipes, uint16_t itemId,
                              uint16_t* out, int outCap) {
    int n = 0;
    for (size_t r = 0; r < recipes.size() && n < outCap; ++r)
        for (int k = 0; k < recipes[r].ingredientCount; ++k)
            if (recipes[r].ingredients[k] == itemId) { out[n++] = static_cast<uint16_t>(r); break; }
    return n;
}

// B안: 역인덱스 사전 계산 (item -> recipe 목록, 고정폭 inline 배열)
struct ReverseIndex {
    static constexpr int MAX_PER_ITEM = 16;
    std::vector<std::array<uint16_t, MAX_PER_ITEM>> lists;
    std::vector<uint8_t> counts;

    void build(const std::vector<Recipe>& recipes, int itemCount) {
        lists.assign(itemCount, {});
        counts.assign(itemCount, 0);
        for (size_t r = 0; r < recipes.size(); ++r) {
            for (int k = 0; k < recipes[r].ingredientCount; ++k) {
                uint16_t item = recipes[r].ingredients[k];
                // 같은 재료가 한 레시피에 중복(3연성)이면 한 번만 기록
                bool dup = false;
                for (int j = 0; j < k; ++j) if (recipes[r].ingredients[j] == item) { dup = true; break; }
                if (dup) continue;
                if (counts[item] < MAX_PER_ITEM) lists[item][counts[item]++] = static_cast<uint16_t>(r);
            }
        }
    }
    int lookup(uint16_t itemId, uint16_t* out, int outCap) const {
        int n = std::min<int>(counts[itemId], outCap);
        std::copy(lists[itemId].begin(), lists[itemId].begin() + n, out);
        return n;
    }
};

// ── 전체 조합 가능 목록 재계산 (인벤토리 변경 후) ──

// A안: 레시피마다 카운트 배열 조회 (나이브)
int craftable_Naive(const std::vector<Recipe>& recipes, const std::vector<uint16_t>& inv,
                     uint16_t* out, int outCap) {
    int n = 0;
    for (size_t r = 0; r < recipes.size() && n < outCap; ++r) {
        bool ok = true;
        // 같은 재료 중복 요구(3연성)를 반영하려면 필요 개수를 센다
        uint16_t need[3] = {0,0,0};
        for (int k = 0; k < recipes[r].ingredientCount; ++k) {
            uint16_t item = recipes[r].ingredients[k];
            int slot = -1;
            for (int j = 0; j < 3; ++j) if (need[j] == 0 || recipes[r].ingredients[j] == item) { slot = j; break; }
            (void)slot;
        }
        // 단순화: 직접 카운트 맵 없이 매번 부분합 계산 (진짜 나이브 버전)
        for (int k = 0; k < recipes[r].ingredientCount; ++k) {
            int need_k = 0;
            for (int j = 0; j <= k; ++j) if (recipes[r].ingredients[j] == recipes[r].ingredients[k]) need_k++;
            if (inv[recipes[r].ingredients[k]] < need_k) { ok = false; break; }
        }
        if (ok) out[n++] = static_cast<uint16_t>(r);
    }
    return n;
}

// D안: 비트마스크 — 서로 다른 재료로만 이뤄진 레시피는 AND 한 번, 중복 재료 레시피만 카운트 체크
// 워드 수를 아이템 수에 맞춰 정확히 잡는다 (과대 할당하면 스캔 비용만 늘어남 — 아래 실측 참고)
struct BitmaskIndex {
    int words = 0;
    struct Entry {
        std::vector<uint64_t> mask;
        int loWord = 0, hiWord = -1; // 이 레시피가 실제로 건드리는 워드 범위만 스캔
        bool hasDuplicateIngredient = false;
        uint16_t dupItem = 0; uint8_t dupNeed = 0;
    };
    std::vector<Entry> entries;

    void build(const std::vector<Recipe>& recipes, int itemCount) {
        words = (itemCount + 63) / 64;
        entries.assign(recipes.size(), Entry{});
        for (size_t r = 0; r < recipes.size(); ++r) {
            auto& e = entries[r];
            e.mask.assign(words, 0);
            e.loWord = words; e.hiWord = -1;
            uint16_t items[3]; int cnt = recipes[r].ingredientCount;
            for (int k = 0; k < cnt; ++k) items[k] = recipes[r].ingredients[k];
            for (int k = 0; k < cnt; ++k) {
                int dupCount = 0;
                for (int j = 0; j < cnt; ++j) if (items[j] == items[k]) dupCount++;
                if (dupCount > 1) { e.hasDuplicateIngredient = true; e.dupItem = items[k]; e.dupNeed = static_cast<uint8_t>(dupCount); }
                int w = items[k] / 64;
                e.mask[w] |= (1ull << (items[k] % 64));
                e.loWord = std::min(e.loWord, w);
                e.hiWord = std::max(e.hiWord, w);
            }
        }
    }
    int craftableAll(const std::vector<uint64_t>& invPresenceMask, const std::vector<uint16_t>& invCounts,
                      uint16_t* out, int outCap) const {
        int n = 0;
        for (size_t r = 0; r < entries.size() && n < outCap; ++r) {
            const auto& e = entries[r];
            bool ok = true;
            for (int w = e.loWord; w <= e.hiWord && ok; ++w)
                if ((e.mask[w] & invPresenceMask[w]) != e.mask[w]) ok = false;
            if (ok && e.hasDuplicateIngredient && invCounts[e.dupItem] < e.dupNeed) ok = false;
            if (ok) out[n++] = static_cast<uint16_t>(r);
        }
        return n;
    }
};

// E안: 이벤트 기반 캐시 — 인벤토리 변경 시 역인덱스로 "영향받는 레시피만" 재평가
struct IncrementalCache {
    std::vector<uint8_t> craftableBit; // 레시피별 현재 조합 가능 여부
    const std::vector<Recipe>* recipes = nullptr;
    const ReverseIndex* revIdx = nullptr;

    void init(const std::vector<Recipe>& r, const ReverseIndex& idx, const std::vector<uint16_t>& inv) {
        recipes = &r; revIdx = &idx;
        craftableBit.assign(r.size(), 0);
        for (size_t i = 0; i < r.size(); ++i) craftableBit[i] = evalOne(i, inv);
    }
    bool evalOne(size_t r, const std::vector<uint16_t>& inv) const {
        const auto& rec = (*recipes)[r];
        for (int k = 0; k < rec.ingredientCount; ++k) {
            int need = 0;
            for (int j = 0; j <= k; ++j) if (rec.ingredients[j] == rec.ingredients[k]) need++;
            if (inv[rec.ingredients[k]] < need) return false;
        }
        return true;
    }
    // 아이템 하나가 변경됐을 때만 호출 — 그 아이템이 쓰이는 레시피만 재평가
    void onItemChanged(uint16_t itemId, const std::vector<uint16_t>& inv) {
        uint16_t affected[ReverseIndex::MAX_PER_ITEM];
        int n = revIdx->lookup(itemId, affected, ReverseIndex::MAX_PER_ITEM);
        for (int i = 0; i < n; ++i) craftableBit[affected[i]] = evalOne(affected[i], inv);
    }
};

template <typename F>
double benchNs(int iters, F&& f) {
    // 워밍업
    for (int i = 0; i < 1000; ++i) f();
    auto start = Clock::now();
    for (int i = 0; i < iters; ++i) f();
    auto end = Clock::now();
    return std::chrono::duration<double, std::nano>(end - start).count() / iters;
}

int main() {
    std::vector<Scenario> scenarios = {
        {"현재 수직 슬라이스 (51종/42개)", 51, 42},
        {"10배 성장 - 재료 ID 무작위 분산 (600종/500개)", 600, 500},
        {"10배 성장 - 재료 ID 축별 클러스터 (600종/500개)", 600, 500},
    };

    for (auto& sc : scenarios) {
        std::mt19937 rng(12345);
        bool clustered = (std::string(sc.name).find("클러스터") != std::string::npos);
        auto recipes = makeRecipes(sc.itemCount, sc.recipeCount, rng, clustered);

        ReverseIndex revIdx; revIdx.build(recipes, sc.itemCount);
        BitmaskIndex bmIdx; bmIdx.build(recipes, sc.itemCount);

        std::vector<uint16_t> inv(sc.itemCount);
        std::uniform_int_distribution<int> invDist(0, 4);
        for (auto& c : inv) c = static_cast<uint16_t>(invDist(rng));

        std::vector<uint64_t> presenceMask((sc.itemCount + 63) / 64, 0);
        for (int i = 0; i < sc.itemCount; ++i) if (inv[i] > 0) presenceMask[i/64] |= (1ull << (i%64));

        IncrementalCache cache; cache.init(recipes, revIdx, inv);

        uint16_t out[64];
        std::uniform_int_distribution<int> itemPick(0, sc.itemCount - 1);

        printf("\n=== %s ===\n", sc.name);

        double t1 = benchNs(200000, [&]{
            uint16_t item = static_cast<uint16_t>(itemPick(rng));
            volatile int n = reverseLookup_LinearScan(recipes, item, out, 64);
            (void)n;
        });
        printf("역방향 조회 (아이템→레시피): 전수 스캔        %8.1f ns\n", t1);

        double t2 = benchNs(200000, [&]{
            uint16_t item = static_cast<uint16_t>(itemPick(rng));
            volatile int n = revIdx.lookup(item, out, 64);
            (void)n;
        });
        printf("역방향 조회 (아이템→레시피): 역인덱스         %8.1f ns  (%.0fx)\n", t2, t1/t2);

        double t3 = benchNs(50000, [&]{
            volatile int n = craftable_Naive(recipes, inv, out, 64);
            (void)n;
        });
        printf("전체 조합 가능 목록: 나이브 전수 평가          %8.1f ns\n", t3);

        double t4 = benchNs(50000, [&]{
            volatile int n = bmIdx.craftableAll(presenceMask, inv, out, 64);
            (void)n;
        });
        printf("전체 조합 가능 목록: 비트마스크                %8.1f ns  (%.1fx)\n", t4, t3/t4);

        double t5 = benchNs(200000, [&]{
            uint16_t item = static_cast<uint16_t>(itemPick(rng));
            cache.onItemChanged(item, inv);
        });
        printf("증분 갱신: 아이템 1개 변경 시 캐시 갱신        %8.1f ns  (전체 재평가 대비 %.0fx)\n", t5, t3/t5);
    }
    return 0;
}
