#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

// `data/*.json` 로더.
//
// **값을 여기 적지 않는다.** 적는 순간 이 테스트가 두 번째 진실 원천이 되고,
// 그게 바로 `dev_data.h`를 지운 이유다. 대신 **수치가 무엇이든 성립해야 하는
// 관계**를 본다 — 칸이 밀리거나 단위가 틀리면 관계가 먼저 깨진다.
//
// 전환이 시뮬 결과를 바꾸지 않았다는 증거는 여기가 아니라 `test_checksum`의
// 고정 체크섬 절에 있다 (전환 전 main에서 뜬 값과 대조한다).
//
// **진짜 data/*.json을 읽는다.** 축소판 픽스처로 테스트하면 실제 파일이 로더를
// 통과하는지는 영영 모른다 — 경로는 CMake가 DC_DATA_DIR로 넘긴다.
#include "dc/config_loader.h"
#include "dc/world.h"
#include "../tools/data_files.h"
#include "test_main.h"

using namespace dc;


static bool slurp(const char* path, std::vector<char>* out) {
    FILE* f = std::fopen(path, "rb");
    if (f == nullptr) return false;
    std::fseek(f, 0, SEEK_END);
    const long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (n < 0) { std::fclose(f); return false; }
    out->resize(static_cast<size_t>(n));
    const size_t got = out->empty() ? 0 : std::fread(out->data(), 1, out->size(), f);
    std::fclose(f);
    return got == out->size();
}

// 실제 데이터로 로더를 돌린다. 버퍼는 호출자가 살려 둬야 한다 —
// 파서가 문자열을 복사하지 않기 때문이다.
static bool loadReal(std::vector<std::vector<char>>* bufs, ConfigLoader* ld,
                     SimConfig* cfg, RecipeTable* rt, HeroBaseline* hb) {
    bufs->resize(DATA_FILE_COUNT);
    for (uint32_t i = 0; i < DATA_FILE_COUNT; ++i) {
        const DataFile f = static_cast<DataFile>(i);
        const std::string path = std::string(DC_DATA_DIR) + "/" + dataFileName(f);
        if (!slurp(path.c_str(), &(*bufs)[i])) {
            printf("    파일을 못 읽었다: %s\n", path.c_str());
            return false;
        }
        ld->set(f, (*bufs)[i].data(), (*bufs)[i].size());
    }
    if (!ld->load(cfg, rt, hb)) {
        printf("    로드 실패 [%s] %s (키 %s) @%zu\n",
               dataFileName(ld->errorFile()), ld->error(),
               ld->errorKey() != nullptr ? ld->errorKey() : "-", ld->errorOffset());
        return false;
    }
    return true;
}

int main() {
    printf("=== 설정 로더 (data/*.json) ===\n");

    std::vector<std::vector<char>> bufs;
    ConfigLoader ld;
    SimConfig    loaded;
    RecipeTable  rt;
    HeroBaseline hb;

    dctest::section("진짜 data/*.json 9개가 로드된다");
    const bool loadedOk = loadReal(&bufs, &ld, &loaded, &rt, &hb);
    CHECK(loadedOk);
    if (!loadedOk) return dctest::summary("test_config_loader");

    CHECK(loaded.dataHash != 0);      // 지문이 실제로 계산됐다
    printf("    dataHash %llu · 아이템 %u종 · 조합식 %u개\n",
           static_cast<unsigned long long>(loaded.dataHash),
           loaded.itemTypeCount, rt.recipeCount());

    // ── 불변식 ──────────────────────────────────────────────────────────
    //
    // **값을 여기 적지 않는다.** 적는 순간 이 테스트가 두 번째 진실 원천이 되고,
    // 그게 바로 dev_data.h를 지운 이유다. 대신 **수치가 무엇이든 성립해야 하는
    // 관계**를 본다 — 칸이 밀리거나 단위가 틀리면 관계가 먼저 깨진다.
    dctest::section("로드 결과가 구조적으로 성립한다");
    {
        CHECK(loaded.tickHz > 0);
        CHECK(loaded.segmentsPerMap > 0);
        CHECK(loaded.totalSegments >= loaded.segmentsPerMap);
        CHECK(loaded.armorK > 0);                 // mitigate의 분모다

        // 레벨 곡선은 단조 증가여야 한다 — 뒤집히면 레벨업이 역행한다
        for (uint32_t i = 1; i < MAX_LEVEL_NEED; ++i) {
            CHECK(loaded.levelNeed[i] > loaded.levelNeed[i - 1]);
        }
        // 동시 생존 상한도 마찬가지다 (§2 난이도 램프)
        for (int32_t i = 1; i < loaded.totalSegments; ++i) {
            CHECK(loaded.capBySegment[i] >= loaded.capBySegment[i - 1]);
        }
        // 구간 시작 포인트는 오름차순이고 클리어 목표 안에 있다
        for (int32_t i = 1; i < loaded.segmentsPerMap; ++i) {
            CHECK(loaded.segmentStartPoints[i] > loaded.segmentStartPoints[i - 1]);
        }
        CHECK(loaded.segmentStartPoints[loaded.segmentsPerMap - 1] < loaded.clearTargetPoints);
        // 엘리트 등장 지점도 오름차순이고, 가리키는 엘리트가 실재해야 한다
        for (uint32_t i = 0; i < loaded.eliteSpawnCount; ++i) {
            if (i > 0) {
                CHECK(loaded.eliteSpawns[i].atClearPoints
                      >= loaded.eliteSpawns[i - 1].atClearPoints);
            }
            CHECK(loaded.eliteSpawns[i].eliteIndex < loaded.eliteCount);
        }
        // 카드 등급 확률의 합은 1000permille이어야 한다
        int32_t rateSum = 0;
        for (uint32_t i = 0; i < MAX_CARD_GRADES; ++i) rateSum += loaded.cardGradeRate[i];
        CHECK_EQ(rateSum, 1000);
        // 스킬 가중치도 마찬가지다
        int32_t weightSum = 0;
        for (uint32_t i = 0; i < loaded.skillCount; ++i) weightSum += loaded.skills[i].weight;
        CHECK_EQ(weightSum, 1000);

        // 몬스터 — 체력·사거리가 0이면 시뮬이 성립하지 않는다
        CHECK(loaded.trash.hp.raw > 0);
        CHECK(loaded.trash.attackRange.raw > 0);
        CHECK(loaded.trash.approachSpeed.raw > 0);
        CHECK(loaded.eliteCount > 0);
        for (uint32_t i = 0; i < loaded.eliteCount; ++i) {
            CHECK(loaded.elites[i].hp.raw > 0);
            CHECK(loaded.elites[i].cooldownTicks > 0);
            CHECK(loaded.elites[i].ccGaugeMax.raw > 0);
            CHECK(loaded.elites[i].typeId != 0);      // 0은 잡몹 자리다
        }
        CHECK(loaded.boss.hp.raw > loaded.elites[0].hp.raw);
        CHECK(loaded.boss.ccGaugeMax.raw > 0);        // 0이면 QTE 완벽이 그로기를 못 만든다

        // 보스 패턴 — 표가 비면 보스가 허수아비가 된다
        CHECK(loaded.bossPatternCount > 0);
        int32_t telegraphs = 0;
        for (uint32_t i = 0; i < loaded.bossPatternCount; ++i) {
            CHECK(loaded.bossPatterns[i].damage.raw > 0);
            CHECK(loaded.bossPatterns[i].cooldownTicks > 0);
            if (loaded.bossPatterns[i].windupTicks > 0) ++telegraphs;
        }
        // **텔레그래프가 하나뿐이라야 보스전 QTE 빈도가 예산 안에 든다** (§3)
        CHECK_EQ(telegraphs, 1);
        // 0번 패턴이 스폰 기본값에 실려 있다
        CHECK_EQ(loaded.boss.damage.raw, loaded.bossPatterns[0].damage.raw);
        CHECK_EQ(loaded.boss.cooldownTicks, loaded.bossPatterns[0].cooldownTicks);

        // 아이템 — 재료 인덱스가 전부 실재해야 한다
        CHECK(loaded.itemTypeCount > 0);
        CHECK(loaded.commonPoolSize > 0);
        CHECK(loaded.commonPoolSize < loaded.itemTypeCount);   // 조합 결과가 있다
        for (uint32_t i = 0; i < loaded.commonPoolSize; ++i) {
            CHECK(loaded.commonPool[i] < loaded.itemTypeCount);
        }
        for (uint32_t r = 0; r < rt.recipeCount(); ++r) {
            const RecipeData& rd = rt.recipe(r);
            CHECK(rd.result < loaded.itemTypeCount);
            CHECK(rd.count > 0);
            for (uint32_t k = 0; k < rd.count; ++k) {
                CHECK(rd.ingredients[k] < loaded.itemTypeCount);
                // **재료는 결과보다 앞선다** — 사다리가 순환하면 조합이 맴돈다
                CHECK(rd.ingredients[k] < rd.result);
            }
        }
        // 모든 아이템이 스탯을 하나는 준다 (칸이 밀리면 0만 남은 아이템이 생긴다)
        for (uint32_t i = 0; i < loaded.itemTypeCount; ++i) {
            int32_t sum = loaded.itemSlowAura[i];
            for (uint32_t s = 0; s < STAT_COUNT; ++s) sum += loaded.itemStats[i][s];
            CHECK(sum > 0);
        }
        // 영웅 기준선 — 0이면 맨몸으로 싸운다
        CHECK(hb.bases[statIndex(Stat::AttackPower)].raw > 0);
        CHECK(hb.bases[statIndex(Stat::AttackSpeed)].raw > 0);
        CHECK(hb.bases[statIndex(Stat::Range)].raw > 0);
        CHECK(hb.bases[statIndex(Stat::CorruptionMax)].raw > 0);
        // 0이면 corruptionPermille()의 나눗셈이 죽는다
        CHECK(hb.bounds[statIndex(Stat::CorruptionMax)].minValue.raw > 0);
    }

    dctest::section("메타(축·등급) — 하네스 정책이 읽는 값");
    {
        ItemMeta meta;
        ConfigLoader ld2;
        for (uint32_t i = 0; i < DATA_FILE_COUNT; ++i) {
            ld2.set(static_cast<DataFile>(i), bufs[i].data(), bufs[i].size());
        }
        SimConfig c2; RecipeTable r2; HeroBaseline h2;
        CHECK(ld2.load(&c2, &r2, &h2, &meta));
        CHECK(c2.itemTierCount > 0);
        for (uint32_t i = 1; i < c2.itemTierCount; ++i) {
            CHECK(meta.tierPower[i] > meta.tierPower[i - 1]);   // 사다리는 오른다
        }
        // 뽑기 풀은 전부 최하 등급이다 — **조합으로만 올라간다**는 규칙 (§5)
        for (uint32_t i = 0; i < c2.commonPoolSize; ++i) {
            CHECK_EQ(c2.itemTier[c2.commonPool[i]], 0);
        }
        // 조합 결과는 재료보다 등급이 높다
        for (uint32_t r = 0; r < r2.recipeCount(); ++r) {
            const RecipeData& rd = r2.recipe(r);
            for (uint32_t k = 0; k < rd.count; ++k) {
                CHECK(c2.itemTier[rd.result] > c2.itemTier[rd.ingredients[k]]);
            }
        }
    }

    dctest::section("등급별 CSR — RL_FORGE가 O(1)로 뽑는 근거");
    {
        // CSR이 **아이템 전부를 정확히 한 번씩** 담아야 한다. 한 칸이 비면
        // 그 아이템은 화로에서 영영 안 나오는데 크래시는 없다.
        CHECK_EQ(loaded.tierStart[0], 0u);
        CHECK_EQ(loaded.tierStart[loaded.itemTierCount], loaded.itemTypeCount);

        uint32_t seen[DC_MAX_ITEM_TYPES_CFG] = {0};
        for (uint32_t t = 0; t < loaded.itemTierCount; ++t) {
            CHECK(loaded.tierItemCount(t) > 0);          // 빈 등급은 데이터 오류다
            for (uint32_t k = loaded.tierStart[t]; k < loaded.tierStart[t + 1]; ++k) {
                const uint32_t id = loaded.tierItems[k];
                CHECK(id < loaded.itemTypeCount);
                CHECK_EQ(loaded.itemTier[id], static_cast<uint8_t>(t));   // 제자리에 있다
                ++seen[id];
            }
        }
        for (uint32_t i = 0; i < loaded.itemTypeCount; ++i) CHECK_EQ(seen[i], 1u);

        // 최하 등급 칸 수 = 뽑기 풀 크기. 두 경로가 같은 "흔함"을 보고 있다
        CHECK_EQ(loaded.tierItemCount(0), loaded.commonPoolSize);

        // 계수 정렬은 등급 안에서 **인덱스 순서를 보존**한다 (결정론 전제)
        for (uint32_t t = 0; t < loaded.itemTierCount; ++t) {
            for (uint32_t k = loaded.tierStart[t] + 1; k < loaded.tierStart[t + 1]; ++k) {
                CHECK(loaded.tierItems[k] > loaded.tierItems[k - 1]);
            }
        }

        // 화로 확률 — 합 1000이고, 확률이 붙은 등급에는 아이템이 있다
        int32_t sum = 0;
        for (uint32_t t = 0; t < MAX_ITEM_TIERS; ++t) {
            sum += loaded.forgeTierRate[t];
            if (loaded.forgeTierRate[t] > 0) CHECK(loaded.tierItemCount(t) > 0);
        }
        CHECK_EQ(sum, 1000);
        printf("    등급 %u단 · 칸 수", loaded.itemTierCount);
        for (uint32_t t = 0; t < loaded.itemTierCount; ++t) {
            printf(" %u(%d‰)", loaded.tierItemCount(t), loaded.forgeTierRate[t]);
        }
        printf("\n");
    }

    dctest::section("**깨진 데이터는 조용히 통과하지 않는다**");
    {
        // 키 하나를 지우면 어느 파일 어느 키인지 말해야 한다
        std::vector<std::vector<char>> b2 = bufs;
        std::vector<char>& h = b2[static_cast<uint32_t>(DataFile::Hero)];
        std::string text(h.begin(), h.end());
        const size_t at = text.find("\"attack_power\"");
        CHECK(at != std::string::npos);
        text.replace(at + 1, 12, "attack_powe_");     // 키 이름을 망가뜨린다
        std::vector<char> broken(text.begin(), text.end());

        ConfigLoader bad;
        for (uint32_t i = 0; i < DATA_FILE_COUNT; ++i) {
            bad.set(static_cast<DataFile>(i), b2[i].data(), b2[i].size());
        }
        bad.set(DataFile::Hero, broken.data(), broken.size());
        SimConfig c2; RecipeTable r2; HeroBaseline h2;
        CHECK(!bad.load(&c2, &r2, &h2));
        CHECK(bad.errorFile() == DataFile::Hero);
        CHECK(std::string(bad.errorKey() != nullptr ? bad.errorKey() : "") == "attack_power");
        printf("    [%s] %s (키 %s)\n", dataFileName(bad.errorFile()), bad.error(),
               bad.errorKey());
    }
    {
        // 소수는 파싱 단계에서 죽는다 — 규약을 로더가 강제한다
        std::vector<char>& h = bufs[static_cast<uint32_t>(DataFile::Hero)];
        std::string text(h.begin(), h.end());
        const size_t at = text.find("\"armor\": 0,");
        CHECK(at != std::string::npos);
        text.replace(at, 11, "\"armor\": 0.5,");
        std::vector<char> broken(text.begin(), text.end());

        ConfigLoader bad;
        for (uint32_t i = 0; i < DATA_FILE_COUNT; ++i) {
            bad.set(static_cast<DataFile>(i), bufs[i].data(), bufs[i].size());
        }
        bad.set(DataFile::Hero, broken.data(), broken.size());
        SimConfig c2; RecipeTable r2; HeroBaseline h2;
        CHECK(!bad.load(&c2, &r2, &h2));
        CHECK(bad.errorFile() == DataFile::Hero);
    }
    {
        // 버퍼를 안 물리면 그 사실을 말한다 (널 참조로 죽지 않는다)
        ConfigLoader bad;
        SimConfig c2; RecipeTable r2; HeroBaseline h2;
        CHECK(!bad.load(&c2, &r2, &h2));
    }

    dctest::section("dataHash — 데이터가 다르면 지문이 다르다");
    {
        // 수치 하나만 바꿔도 지문이 갈린다. **서버와 클라가 다른 데이터를
        // 로드했는지 가리는 값이다** (§5 Inventory::tableHash_와 같은 장치).
        std::vector<char>& m = bufs[static_cast<uint32_t>(DataFile::Monsters)];
        std::string text(m.begin(), m.end());
        const size_t at = text.find("\"slice_boss_hp\": 2086");
        CHECK(at != std::string::npos);
        text.replace(at, 21, "\"slice_boss_hp\": 2087");
        std::vector<char> tweaked(text.begin(), text.end());

        ConfigLoader ld2;
        for (uint32_t i = 0; i < DATA_FILE_COUNT; ++i) {
            ld2.set(static_cast<DataFile>(i), bufs[i].data(), bufs[i].size());
        }
        ld2.set(DataFile::Monsters, tweaked.data(), tweaked.size());
        SimConfig c2; RecipeTable r2; HeroBaseline h2;
        CHECK(ld2.load(&c2, &r2, &h2));
        CHECK(c2.dataHash != loaded.dataHash);
    }

    return dctest::summary("test_config_loader");
}
