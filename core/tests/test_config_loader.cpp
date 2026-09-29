#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

// `data/*.json` 로더.
//
// **이 파일의 존재 이유는 한 절이다 — "로더가 만든 설정이 dev_data.h와 같은가".**
// 그게 참이면 전환(다음 단계)은 값이 바뀌지 않는 순수 리팩터링이 되고, 거짓이면
// 어느 필드가 다른지 이름으로 나온다. 값을 눈으로 대조하는 대신 기계가 대조한다.
//
// **진짜 data/*.json을 읽는다.** 축소판 픽스처로 테스트하면 실제 파일이 로더를
// 통과하는지는 영영 모른다 — 경로는 CMake가 DC_DATA_DIR로 넘긴다.
#include "dc/config_loader.h"
#include "dc/world.h"
#include "../tools/dev_data.h"
#include "test_main.h"

using namespace dc;

// 아래 "필드 목록"과 짝을 이루는 잠금값. **구조체가 커지면 실패한다** —
// 목록은 손으로 적은 것이라 새 필드가 조용히 빠질 수 있고, 그러면 로더가 그
// 필드를 안 읽어도 "전환이 값을 바꾸지 않는다"가 초록불로 통과한다.
//
// x86-64 LP64 기준값이다. 다른 ABI(32비트 등)를 CI 축으로 더하면 여기서 한 번
// 걸리는데, 그때 고칠 것은 값이지 검사가 아니다.
constexpr long long DC_SIMCONFIG_SIZE_LOCK = 5048;

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

// 필드 비교 — 이름을 붙여 어긋난 자리를 바로 말하게 한다
#define SAME(field)     CHECK_EQ(static_cast<long long>(a.field), static_cast<long long>(b.field))
#define SAME_FX(field)  CHECK_EQ(static_cast<long long>(a.field.raw), static_cast<long long>(b.field.raw))

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

    // ── 본론 ────────────────────────────────────────────────────────────
    dctest::section("**로더 == dev_data.h** — 전환이 값을 바꾸지 않는다");
    {
        const SimConfig& a = loaded;
        const SimConfig  b = dev::devConfig();

        // 진행 · 잠식
        SAME(tickHz); SAME(totalSegments); SAME(segmentsPerMap); SAME(armorK);
        SAME(trashHpScalePerSegmentPermille);
        SAME(corruptionThreshold); SAME(corruptionPerMobPermille);
        SAME(corruptionOverflowMultPermille);
        SAME(segmentClearPurge); SAME(expPerEhpPermille);
        // 구슬
        SAME(orbTrashDropPermille); SAME(orbTrashAmount); SAME(orbEliteAmount);
        SAME(orbPickupRadiusMilli); SAME(orbLifetimeTicks);
        // 보스 페이즈
        SAME(bossPhase2AtPermille); SAME(bossPhase2Purge); SAME(bossPhase2AuraDps);
        SAME(bossPhase2SummonCount); SAME(bossPhase2SummonPeriodTicks);
        SAME(bossCcResistStepPermille);
        // 스폰
        SAME(spawnIntervalTicks); SAME(spawnRadiusMilli); SAME(minSeparationMilli);
        SAME(directions); SAME(separationMilli); SAME(heroSeparationMilli);
        SAME(approachMarginMilli);
        for (int32_t i = 0; i < 24; ++i) SAME(capBySegment[i]);
        for (int32_t i = 0; i < 8; ++i)  SAME(batchBySegment[i]);
        // 구간
        SAME(trashPoints); SAME(elitePoints); SAME(clearTargetPoints);
        for (int32_t i = 0; i < 8; ++i) SAME(segmentStartPoints[i]);
        SAME(eliteSpawnCount);
        for (uint32_t i = 0; i < b.eliteSpawnCount; ++i) {
            SAME(eliteSpawns[i].atClearPoints);
            SAME(eliteSpawns[i].eliteIndex);
        }
        // 영웅 · QTE
        SAME(procPrdCQ16); SAME_FX(aoeRadius);
        SAME(qteCooldownTicks); SAME(qtePerfectWindowTicks);
        SAME_FX(qteSuccessMult); SAME_FX(qtePerfectMult);
        SAME(qtePerfectCcGainPermille); SAME(groggyTicks);
        SAME(targetPriorityFalloffPerTile);
        // 훅형 카드 파라미터
        SAME(decayTicks); SAME(rageDurationTicks); SAME(rageMaxStacks);
        SAME(boltIntervalTicks); SAME(stormDpsPermille); SAME(swarmMaxStacks);
        SAME_FX(frostRadius); SAME_FX(stormRadius);
        SAME_FX(pierceWidth); SAME_FX(pierceLength); SAME_FX(swarmRadius);
        // 카드
        SAME(legendPoolSize); SAME(cardsPerLevel);
        for (uint32_t i = 0; i < MAX_CARD_GRADES; ++i) {
            SAME(cardGradeRate[i]); SAME(cardGradeBudget[i]); SAME(cardGradeStep[i]);
        }
        SAME(engraveCount); SAME(relicCount);
        for (uint32_t i = 0; i < b.engraveCount; ++i) SAME(engraveBase[i]);
        for (uint32_t i = 0; i < b.relicCount; ++i)   SAME(relicBase[i]);
        SAME(statCardPoolSize);
        for (uint32_t i = 0; i < b.statCardPoolSize; ++i) SAME(statCardPool[i]);
        for (uint32_t i = 0; i < MAX_LEVEL_NEED; ++i) SAME(levelNeed[i]);
        // 스킬
        SAME(skillCount);
        for (uint32_t i = 0; i < b.skillCount; ++i) {
            SAME_FX(skills[i].mult); SAME(skills[i].weight);
            CHECK(a.skills[i].aoe == b.skills[i].aoe);
        }
        // 몬스터
        SAME_FX(trash.hp); SAME_FX(trash.damage); SAME(trash.cooldownTicks);
        SAME(trash.targetPriority); SAME_FX(trash.attackRange);
        SAME_FX(trash.approachSpeed);
        CHECK(a.trash.archetype == b.trash.archetype);
        SAME(eliteCount);
        for (uint32_t i = 0; i < b.eliteCount; ++i) {
            SAME_FX(elites[i].hp); SAME_FX(elites[i].armor); SAME_FX(elites[i].damage);
            SAME(elites[i].targetPriority); SAME(elites[i].windupTicks);
            SAME(elites[i].cooldownTicks); SAME(elites[i].typeId);
            SAME_FX(elites[i].attackRange); SAME_FX(elites[i].ccGaugeMax);
            SAME_FX(elites[i].approachSpeed);
        }
        SAME_FX(boss.hp); SAME_FX(boss.armor); SAME_FX(boss.damage);
        SAME(boss.targetPriority); SAME(boss.cooldownTicks); SAME(boss.windupTicks);
        SAME(boss.typeId); SAME_FX(boss.attackRange); SAME_FX(boss.ccGaugeMax);
        SAME_FX(boss.approachSpeed);
        SAME(bossPatternCount);
        for (uint32_t i = 0; i < b.bossPatternCount; ++i) {
            SAME_FX(bossPatterns[i].damage);
            SAME(bossPatterns[i].windupTicks); SAME(bossPatterns[i].cooldownTicks);
        }
        // 아이템
        SAME(itemTypeCount); SAME(commonPoolSize);
        for (uint32_t i = 0; i < b.commonPoolSize; ++i) SAME(commonPool[i]);
        for (uint32_t i = 0; i < b.itemTypeCount; ++i) {
            for (uint32_t s = 0; s < STAT_COUNT; ++s) SAME(itemStats[i][s]);
            SAME(itemSlowAura[i]);
        }
    }

    // **필드를 추가하면 여기서 걸린다.** 위 목록은 손으로 적은 것이라 새 필드가
    // 조용히 빠질 수 있다 — 구조체가 커지면 이 줄이 실패하면서 목록을 보게 만든다.
    // (크기만으로는 "필드가 바뀌었다"를 완벽히 잡지 못하지만, 추가는 전부 잡는다)
    dctest::section("SimConfig 크기 잠금 — 필드가 늘면 위 목록도 늘려야 한다");
    printf("    sizeof(SimConfig) = %zu\n", sizeof(SimConfig));
    CHECK_EQ(static_cast<long long>(sizeof(SimConfig)), DC_SIMCONFIG_SIZE_LOCK);

    dctest::section("조합 테이블 == dev_data.h");
    {
        const RecipeTable& other = dev::devRecipeTable();
        CHECK_EQ(rt.recipeCount(), other.recipeCount());
        CHECK_EQ(rt.itemTypeCount(), other.itemTypeCount());
        // **dataHash가 같다는 것이 곧 표 전체가 같다는 뜻이다** —
        // 이 해시는 이미 체크섬에 들어가 서버-클라 불일치를 잡는 값이다 (§5).
        CHECK_EQU(rt.dataHash(), other.dataHash());
    }

    dctest::section("영웅 기준선 == dev_data.h");
    {
        World w;
        w.init(1);
        dev::applyHeroBaseline(w);
        for (uint32_t i = 0; i < STAT_COUNT; ++i) {
            CHECK_EQ(static_cast<long long>(hb.bases[i].raw),
                     static_cast<long long>(w.hero.stats.base(static_cast<Stat>(i)).raw));
        }
        // 하한은 stats.json이 원천이다 — dev_data는 이걸 손으로 옮겨 적고 있었다
        CHECK(hb.bounds[statIndex(Stat::CorruptionMax)].minValue.raw > 0);
        CHECK(hb.bounds[statIndex(Stat::AttackSpeed)].minValue.raw > 0);
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
