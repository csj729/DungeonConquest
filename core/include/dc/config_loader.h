// `data/*.json` → `SimConfig` · `RecipeTable` · 영웅 기준선.
//
// **`core/tools/dev_data.h`를 대체한다.** 그 파일은 같은 수치를 손으로 옮겨 적은
// 것이었고, `verify_core_constants.py`가 80항목을 대조해 어긋남을 막고 있었다.
// 여기가 붙으면 그 대조가 **필요 없어진다** — 수치가 한 곳에만 살기 때문이다.
// 검사를 늘리는 것보다 검사할 이유를 없애는 쪽이 낫다.
//
// ## 파일을 읽지 않는다
//
// 호스트가 읽어서 버퍼로 넘긴다. Unity는 `TextAsset`, 서버는 `File.ReadAllBytes`,
// 도구는 `fread`다 — 코어에 파일 I/O가 들어가면 레이어 분리가 깨진다 (CLAUDE.md).
// **버퍼는 `load()`가 끝날 때까지 살아 있어야 한다** (파서가 문자열을 복사하지 않는다).
//
// ## 실패는 조용하지 않다
//
// 없는 키·타입 불일치·소수는 전부 `load()`를 false로 만들고, 어느 **파일**의 어느
// **키**인지 말한다. 기본값으로 때우면 오타 하나가 "그 수치는 0이었다"로 통과한다.
//
// ## dataHash
//
// 읽은 정수 전부를 FNV-1a로 접어 `SimConfig::dataHash`에 넣는다. 서버와 클라가
// 다른 `data/*.json`을 로드했으면 **틱 0에서 체크섬이 갈린다** — `RecipeTable`이
// 이미 같은 장치를 갖고 있고(§5 `Inventory::tableHash_`), 그걸 설정 전체로 넓힌 것이다.
// `std::hash`는 구현마다 결과가 달라 쓰지 않는다 (CLAUDE.md).
#ifndef DC_CONFIG_LOADER_H
#define DC_CONFIG_LOADER_H

#include <cstddef>
#include <cstdint>
#include <vector>

#include "card.h"
#include "item.h"
#include "json.h"
#include "sim_config.h"
#include "stat_id.h"
#include "world.h"

namespace dc {

// 호스트가 읽어 와야 하는 파일. **이름이 곧 계약이다** — Unity·서버·도구가
// 같은 목록을 쓴다.
enum class DataFile : uint8_t {
    Hero = 0, Progression, Spawn, Segments, Monsters, Cards, Skills, Items, Stats,
    Count
};
constexpr uint32_t DATA_FILE_COUNT = static_cast<uint32_t>(DataFile::Count);

inline const char* dataFileName(DataFile f) {
    switch (f) {
        case DataFile::Hero:        return "hero.json";
        case DataFile::Progression: return "progression.json";
        case DataFile::Spawn:       return "spawn.json";
        case DataFile::Segments:    return "segments.json";
        case DataFile::Monsters:    return "monsters.json";
        case DataFile::Cards:       return "cards.json";
        case DataFile::Skills:      return "skills.json";
        case DataFile::Items:       return "items.json";
        case DataFile::Stats:       return "stats.json";
        case DataFile::Count:       break;
    }
    return "?";
}

// `Stat` enum 순서와 **같은 순서**여야 한다. `verify_core_constants.py`가 이
// 표와 `data/stats.json`의 키 순서를 대조한다 — 어긋나면 아이템 스탯이 엉뚱한
// 칸으로 들어간다.
inline const char* statJsonName(uint32_t i) {
    static const char* kNames[STAT_COUNT] = {
        "attack_power", "attack_speed", "armor", "range", "corruption_max",
        "crit_chance", "crit_mult", "proc_rate", "move_speed", "aoe_radius",
    };
    return i < STAT_COUNT ? kNames[i] : "?";
}

// 아이템 메타 — **시뮬은 읽지 않는다.** 몬테카를로 하네스의 카드 정책이 "어느
// 축으로 빌드할지"를 고르는 데만 쓴다. `SimConfig`에 섞어 넣으면 시뮬 설정과
// 하네스 설정의 경계가 흐려지므로 따로 받는다 — 필요한 쪽만 요청한다.
//
// 축·등급 어휘는 데이터가 정한다 (`items.json`의 `axes` · `grades` 목록 순서).
// C++가 "화력"을 알고 있으면 그것도 하드코딩이다.
// **등급은 여기 없다.** RL_FORGE가 등급으로 추첨하면서 시뮬도 등급을 알아야
// 했으므로 `SimConfig::itemTier` 하나로 모았다 — 같은 값이 두 곳에 살면 한쪽만
// 고치는 사고가 난다 (CLAUDE.md).
struct ItemMeta {
    uint8_t  axis[DC_MAX_ITEM_TYPES_CFG]{};        // axes 목록의 인덱스
    int32_t  tierPower[MAX_ITEM_TIERS]{};          // 등급별 총 위력 permille
};

// 영웅 기준선. `World::init()` 뒤에 `StatBlock::init`으로 심는다.
struct HeroBaseline {
    Fixed      bases[STAT_COUNT]{};
    StatBounds bounds[STAT_COUNT]{};
};

class ConfigLoader {
  public:
    // 버퍼를 물린다. 전부 채운 뒤 `load()`를 부른다.
    void set(DataFile f, const char* text, size_t len) {
        const uint32_t i = static_cast<uint32_t>(f);
        if (i >= DATA_FILE_COUNT) return;
        text_[i] = text;
        len_[i]  = len;
    }

    // `meta`는 선택이다 — 하네스만 쓴다.
    bool load(SimConfig* cfg, RecipeTable* recipes, HeroBaseline* hero,
              ItemMeta* meta = nullptr);

    const char* error()     const { return err_; }
    const char* errorKey()  const { return errKey_; }
    DataFile    errorFile() const { return errFile_; }
    size_t      errorOffset() const { return errOff_; }

  private:
    // 파싱된 문서들. `load()` 안에서만 산다.
    struct Docs { json::Doc d[DATA_FILE_COUNT]; };

    bool fail(DataFile f, const char* msg, const char* key = nullptr, size_t off = 0) {
        if (err_ == nullptr) { err_ = msg; errKey_ = key; errFile_ = f; errOff_ = off; }
        return false;
    }
    // 문서 하나의 조회 오류를 걷어 온다. 조회는 실패해도 계속 진행되므로
    // **파일 하나를 다 읽은 뒤 한 번만** 확인하면 된다.
    bool sweep(DataFile f, const json::Doc& d) {
        if (d.ok()) return true;
        return fail(f, d.error(), d.errorKey());
    }

    void feed(int64_t v) {   // FNV-1a 64 — std::hash 금지 (CLAUDE.md)
        uint64_t x = static_cast<uint64_t>(v);
        for (int32_t b = 0; b < 8; ++b) {
            hash_ ^= (x >> (b * 8)) & 0xFFu;
            hash_ *= 1099511628211ull;
        }
    }
    int64_t take(const json::Value& v) { const int64_t x = v.asInt(); feed(x); return x; }
    int32_t take32(const json::Value& v) { const int32_t x = v.asI32(); feed(x); return x; }

    bool loadStats(const json::Doc& d, HeroBaseline* hero);
    bool loadHero(const json::Doc& d, SimConfig* cfg, HeroBaseline* hero);
    bool loadProgression(const json::Doc& d, SimConfig* cfg);
    bool loadSpawn(const json::Doc& d, SimConfig* cfg);
    bool loadSegments(const json::Doc& d, SimConfig* cfg);
    bool loadMonsters(const json::Doc& d, SimConfig* cfg);
    bool loadCards(const json::Doc& d, SimConfig* cfg);
    bool loadSkills(const json::Doc& d, SimConfig* cfg);
    bool loadItems(const json::Doc& d, SimConfig* cfg, RecipeTable* recipes,
                   ItemMeta* meta);

    const char* text_[DATA_FILE_COUNT] = {};
    size_t      len_[DATA_FILE_COUNT]  = {};

    const char* err_     = nullptr;
    const char* errKey_  = nullptr;
    DataFile    errFile_ = DataFile::Count;
    size_t      errOff_  = 0;
    uint64_t    hash_    = 1469598103934665603ull;   // FNV-1a offset basis
    int32_t     approachTicks_ = 0;   // spawn.json → loadMonsters가 쓴다
    // skills.json의 id 목록. **loadCards가 고유 각인의 skill 이름을 여기서 찾는다.**
    // 문자열을 복사하지 않고 원문 위치만 들고 있다 — 파싱된 문서가 load() 동안
    // 살아 있으므로 안전하고, 3종이라 선형 탐색이 해시보다 빠르다.
    json::Value skillIds_[MAX_SKILLS]{};
    uint32_t    skillIdCount_ = 0;
};

// ── 구현 ──────────────────────────────────────────────────────────────────

inline bool ConfigLoader::load(SimConfig* cfg, RecipeTable* recipes, HeroBaseline* hero,
                               ItemMeta* meta) {
    if (cfg == nullptr || recipes == nullptr || hero == nullptr) {
        return fail(DataFile::Count, "출력 포인터가 널이다");
    }
    *cfg  = SimConfig{};
    *hero = HeroBaseline{};
    if (meta != nullptr) *meta = ItemMeta{};
    hash_ = 1469598103934665603ull;

    Docs docs;
    for (uint32_t i = 0; i < DATA_FILE_COUNT; ++i) {
        const DataFile f = static_cast<DataFile>(i);
        if (text_[i] == nullptr) return fail(f, "버퍼가 물려 있지 않다");
        if (!docs.d[i].parse(text_[i], len_[i])) {
            return fail(f, docs.d[i].error(), nullptr, docs.d[i].errorOffset());
        }
    }

    // **순서가 있다.** stats가 먼저여야 하한을 깔고 hero가 그 위에 기준값을 얹는다.
    // 아이템은 마지막이다 — itemTypeCount가 RecipeTable 빌드의 입력이다.
    if (!loadStats(docs.d[static_cast<uint32_t>(DataFile::Stats)], hero)) return false;
    if (!loadHero(docs.d[static_cast<uint32_t>(DataFile::Hero)], cfg, hero)) return false;
    if (!loadProgression(docs.d[static_cast<uint32_t>(DataFile::Progression)], cfg)) return false;
    if (!loadSpawn(docs.d[static_cast<uint32_t>(DataFile::Spawn)], cfg)) return false;
    if (!loadSegments(docs.d[static_cast<uint32_t>(DataFile::Segments)], cfg)) return false;
    if (!loadMonsters(docs.d[static_cast<uint32_t>(DataFile::Monsters)], cfg)) return false;
    // **skills가 cards보다 먼저다.** 고유 각인이 "어느 스킬에 붙는지"를 스킬 id로
    // 적어 두므로, cards를 읽을 때 그 목록이 이미 있어야 인덱스로 바꿀 수 있다.
    if (!loadSkills(docs.d[static_cast<uint32_t>(DataFile::Skills)], cfg)) return false;
    if (!loadCards(docs.d[static_cast<uint32_t>(DataFile::Cards)], cfg)) return false;
    if (!loadItems(docs.d[static_cast<uint32_t>(DataFile::Items)], cfg, recipes, meta)) return false;

    cfg->dataHash = hash_;
    return true;
}

inline bool ConfigLoader::loadStats(const json::Doc& d, HeroBaseline* hero) {
    const json::Value stats = d.root()["stats"];
    for (uint32_t i = 0; i < STAT_COUNT; ++i) {
        const json::Value s = stats[statJsonName(i)];
        hero->bounds[i].minValue     = Fixed::fromPermille(take32(s["min_value_permille"]));
        hero->bounds[i].minPctAddSum = Fixed::fromPermille(take32(s["min_pct_add_permille"]));
    }
    return sweep(DataFile::Stats, d);
}

inline bool ConfigLoader::loadHero(const json::Doc& d, SimConfig* cfg, HeroBaseline* hero) {
    const json::Value r = d.root();

    // **공속은 간격에서 파생된다.** 데이터가 담는 것은 틱 간격이고 스탯은 초당
    // 횟수다 — 20 / 13 = 1.538회/초. 둘 다 담으면 한쪽만 고치는 사고가 난다.
    const int32_t interval = take32(r["attack_interval_ticks"]);
    if (interval <= 0) return fail(DataFile::Hero, "공격 간격이 0 이하다", "attack_interval_ticks");

    Fixed* b = hero->bases;
    b[statIndex(Stat::AttackPower)]   = Fixed(take32(r["attack_power"]));
    b[statIndex(Stat::AttackSpeed)]   = Fixed(20) / interval;
    b[statIndex(Stat::Armor)]         = Fixed(take32(r["armor"]));
    b[statIndex(Stat::Range)]         = Fixed(take32(r["range"]));
    b[statIndex(Stat::CorruptionMax)] = Fixed(take32(r["corruption_max"]));
    b[statIndex(Stat::CritChance)]    = Fixed::fromPermille(take32(r["crit_chance_permille"]));
    b[statIndex(Stat::CritMult)]      = Fixed::fromPermille(take32(r["crit_mult_permille"]));
    b[statIndex(Stat::MoveSpeed)]     = Fixed::fromPermille(take32(r["move_speed_permille"]));
    b[statIndex(Stat::AoeRadius)]     = Fixed::fromPermille(take32(r["aoe_radius_millitile"]));

    cfg->aoeRadius   = b[statIndex(Stat::AoeRadius)];
    cfg->procPrdCQ16 = static_cast<uint32_t>(take(r["proc_prd_c_q16"]));

    cfg->qteCooldownTicks         = take32(r["qte_cooldown_ticks"]);
    cfg->qtePerfectWindowTicks    = take32(r["qte_perfect_window_ticks"]);
    cfg->qteSuccessMult           = Fixed::fromPermille(take32(r["qte_success_mult_permille"]));
    cfg->qtePerfectMult           = Fixed::fromPermille(take32(r["qte_perfect_mult_permille"]));
    cfg->qtePerfectCcGainPermille = take32(r["qte_perfect_cc_gain_permille"]);
    cfg->groggyTicks              = take32(r["groggy_ticks"]);
    return sweep(DataFile::Hero, d);
}

inline bool ConfigLoader::loadProgression(const json::Doc& d, SimConfig* cfg) {
    const json::Value r = d.root();
    cfg->tickHz         = take32(r["tick_hz"]);
    cfg->totalSegments  = take32(r["total_segments"]);
    cfg->segmentsPerMap = take32(r["segments_per_map"]);
    cfg->armorK         = take32(r["armor_k"]);
    cfg->trashHpScalePerSegmentPermille = take32(r["trash_hp_scale_per_segment_permille"]);
    cfg->corruptionThreshold            = take32(r["corruption_threshold"]);
    cfg->corruptionPerMobPermille       = take32(r["corruption_per_mob_permille"]);
    cfg->corruptionOverflowMultPermille = take32(r["corruption_overflow_mult_permille"]);
    cfg->segmentClearPurge  = take32(r["segment_clear_purge"]);
    cfg->bossPhase2Purge    = take32(r["boss_phase2_purge"]);
    cfg->orbTrashDropPermille = take32(r["orb_trash_drop_permille"]);
    cfg->orbTrashAmount     = take32(r["orb_trash_amount"]);
    cfg->orbEliteAmount     = take32(r["orb_elite_amount"]);
    cfg->orbPickupRadiusMilli = take32(r["orb_pickup_radius_millitile"]);
    cfg->orbLifetimeTicks   = take32(r["orb_lifetime_ticks"]);
    cfg->expPerEhpPermille  = take32(r["exp_per_ehp_permille"]);

    // 레벨 필요 경험치 표. 파이썬이 지수 곡선을 미리 푼 정수 표다.
    const json::Value need = r["level_need_table"];
    const uint32_t n = need.size();
    if (n > MAX_LEVEL_NEED) {
        return fail(DataFile::Progression, "레벨 표가 상한을 넘는다", "level_need_table");
    }
    for (uint32_t i = 0; i < n; ++i) cfg->levelNeed[i] = take(need.at(i));
    return sweep(DataFile::Progression, d);
}

inline bool ConfigLoader::loadSpawn(const json::Doc& d, SimConfig* cfg) {
    const json::Value r = d.root();
    cfg->spawnIntervalTicks = take32(r["interval_ticks"]);
    cfg->spawnRadiusMilli   = take32(r["radius_millitile"]);
    cfg->minSeparationMilli = take32(r["min_separation_millitile"]);
    cfg->directions         = static_cast<uint32_t>(take32(r["directions"]));
    cfg->separationMilli     = take32(r["mob_separation_millitile"]);
    cfg->heroSeparationMilli = take32(r["hero_separation_millitile"]);
    cfg->approachMarginMilli = take32(r["approach_margin_millitile"]);
    approachTicks_ = take32(r["approach_ticks"]);

    const json::Value cap = r["concurrent_cap_by_segment"];
    if (cap.size() > 32u) {
        return fail(DataFile::Spawn, "상한 표가 구간 수를 넘는다", "concurrent_cap_by_segment");
    }
    for (uint32_t i = 0; i < cap.size(); ++i) cfg->capBySegment[i] = take32(cap.at(i));

    const json::Value batch = r["batch_by_segment"];
    if (batch.size() > 16u) {
        return fail(DataFile::Spawn, "배치 표가 맵 구간 수를 넘는다", "batch_by_segment");
    }
    for (uint32_t i = 0; i < batch.size(); ++i) cfg->batchBySegment[i] = take32(batch.at(i));
    return sweep(DataFile::Spawn, d);
}

inline bool ConfigLoader::loadSegments(const json::Doc& d, SimConfig* cfg) {
    const json::Value r = d.root();
    cfg->trashPoints        = take32(r["trash_points"]);
    cfg->elitePoints        = take32(r["elite_points"]);
    cfg->clearTargetPoints  = take32(r["clear_target_points"]);

    const json::Value starts = r["segment_start_points"];
    if (starts.size() > 16u) {
        return fail(DataFile::Segments, "구간 시작 표가 상한을 넘는다", "segment_start_points");
    }
    for (uint32_t i = 0; i < starts.size(); ++i) cfg->segmentStartPoints[i] = take32(starts.at(i));

    // 엘리트 등장 지점 — [게이지 임계, 엘리트 인덱스] 쌍의 배열.
    // **시간이 아니라 처치량에 걸린다** (§2).
    const json::Value sp = r["elite_spawn_points"];
    if (sp.size() > MAX_ELITE_SPAWNS) {
        return fail(DataFile::Segments, "엘리트 등장 지점이 상한을 넘는다", "elite_spawn_points");
    }
    cfg->eliteSpawnCount = sp.size();
    for (uint32_t i = 0; i < sp.size(); ++i) {
        cfg->eliteSpawns[i].atClearPoints = take32(sp.at(i).at(0));
        cfg->eliteSpawns[i].eliteIndex    = static_cast<uint32_t>(take(sp.at(i).at(1)));
    }
    return sweep(DataFile::Segments, d);
}

inline bool ConfigLoader::loadMonsters(const json::Doc& d, SimConfig* cfg) {
    const json::Value r = d.root();
    cfg->targetPriorityFalloffPerTile = take32(r["target_priority_falloff_per_tile"]);

    // 접근 속도는 **스폰 반경 / 접근 시간에서 파생된다** (§2). 데이터에 따로
    // 담지 않는 이유는 반경을 바꾸면 같이 움직여야 하는 값이기 때문이다.
    // **접근 틱은 spawn.json에 있다** — 반경과 함께 읽어야 뜻이 통하는 값이라
    // 거기 살고, loadSpawn이 먼저 돌면서 approachTicks_에 남겨 둔다.
    if (approachTicks_ <= 0 || cfg->tickHz <= 0) {
        return fail(DataFile::Spawn, "접근 틱이 0 이하다", "approach_ticks");
    }
    const Fixed approach = Fixed::fromRaw(
        static_cast<int32_t>((static_cast<int64_t>(cfg->spawnRadiusMilli) * Fixed::ONE_RAW)
                             / 1000 / (approachTicks_ / cfg->tickHz)));

    const json::Value t = r["trash"];
    cfg->trash.hp             = Fixed(take32(t["hp"]));
    cfg->trash.damage         = Fixed(take32(t["damage"]));
    cfg->trash.cooldownTicks  = take32(t["cooldown_ticks"]);
    cfg->trash.targetPriority = take32(t["target_priority"]);
    cfg->trash.attackRange    = Fixed::fromPermille(take32(t["attack_range_millitile"]));
    cfg->trash.archetype      = Archetype::Trash;
    cfg->trash.approachSpeed  = approach;

    // ── 엘리트 ──
    const int32_t eliteRange    = take32(r["elite_attack_range_millitile"]);
    const int32_t eliteCc       = take32(r["elite_cc_gauge_max"]);
    const json::Value es = r["elites"];
    if (es.size() > MAX_ELITE_TYPES) {
        return fail(DataFile::Monsters, "엘리트 종류가 상한을 넘는다", "elites");
    }
    cfg->eliteCount = es.size();
    for (uint32_t i = 0; i < es.size(); ++i) {
        const json::Value e = es.at(i);
        MonsterConfig& m = cfg->elites[i];
        m.hp             = Fixed(take32(e["hp"]));
        m.armor          = Fixed(take32(e["armor"]));
        m.damage         = Fixed(take32(e["damage"]));
        m.targetPriority = take32(e["target_priority"]);
        m.windupTicks    = take32(e["windup_ticks"]);
        m.typeId         = static_cast<uint16_t>(take32(e["type_id"]));
        m.archetype      = Archetype::Elite;
        m.cooldownTicks  = take32(e["cooldown_ticks"]);
        m.approachSpeed  = approach;
        m.attackRange    = Fixed::fromPermille(eliteRange);
        m.ccGaugeMax     = Fixed(eliteCc);
    }

    // ── 보스 ──
    const json::Value b = r["boss"];
    cfg->boss.hp             = Fixed(take32(r["slice_boss_hp"]));
    cfg->boss.armor          = Fixed(take32(r["slice_boss_armor"]));
    cfg->boss.targetPriority = take32(b["target_priority"]);
    cfg->boss.attackRange    = Fixed::fromPermille(take32(r["slice_boss_attack_range_millitile"]));
    cfg->boss.ccGaugeMax     = Fixed(take32(b["cc_gauge_max"]));
    cfg->boss.archetype      = Archetype::Boss;
    cfg->boss.approachSpeed  = approach;
    cfg->boss.typeId         = static_cast<uint16_t>(take32(r["slice_boss_type_id"]));
    // 처형(W_EXECUTE) 면역. 없으면 임계 400permille이 페이즈 2(체력 500permille
    // 시작, 실측 28.9초)의 80%를 생략해 전설 각인 하나가 보스 시스템을 무력화한다.
    if (r["boss_execute_immune"].asBool()) {
        cfg->boss.flags = static_cast<uint8_t>(cfg->boss.flags | EntityFlag::ExecuteImmune);
        feed(1);
    } else {
        feed(0);
    }

    cfg->bossPhase2AtPermille           = take32(b["phase2_at_permille"]);
    cfg->bossPhase2AuraDps              = take32(b["phase2_aura_dps"]);
    cfg->bossPhase2SummonCount          = take32(b["phase2_summon_count"]);
    cfg->bossPhase2SummonPeriodTicks    = take32(b["phase2_summon_period_ticks"]);
    cfg->bossCcResistStepPermille       = take32(b["cc_resist_step_permille"]);

    // 패턴 — **고정 순환이다. 랜덤이 아니다.** damage는 타수를 합쳐 싣는다
    // (mitigate가 피해에 선형이라 3×63과 189가 같은 결과다).
    const json::Value ps = b["patterns"];
    if (ps.size() > MAX_BOSS_PATTERNS) {
        return fail(DataFile::Monsters, "보스 패턴이 상한을 넘는다", "patterns");
    }
    cfg->bossPatternCount = ps.size();
    for (uint32_t i = 0; i < ps.size(); ++i) {
        const json::Value p = ps.at(i);
        cfg->bossPatterns[i].damage        = Fixed(take32(p["hits"]) * take32(p["damage"]));
        cfg->bossPatterns[i].windupTicks   = take32(p["windup_ticks"]);
        cfg->bossPatterns[i].cooldownTicks = take32(p["cooldown_ticks"]);
    }
    // 스폰 시 값은 첫 `loadBossPattern`이 덮어쓰지만, 패턴 표가 비어도 보스가
    // 무해한 허수아비가 되지는 않게 0번으로 초기화해 둔다.
    if (cfg->bossPatternCount > 0) {
        cfg->boss.damage        = cfg->bossPatterns[0].damage;
        cfg->boss.cooldownTicks = cfg->bossPatterns[0].cooldownTicks;
        cfg->boss.windupTicks   = cfg->bossPatterns[0].windupTicks;
    }
    return sweep(DataFile::Monsters, d);
}

inline bool ConfigLoader::loadCards(const json::Doc& d, SimConfig* cfg) {
    const json::Value r = d.root();
    cfg->legendPoolSize = static_cast<uint32_t>(take(r["legend_pool_size"]));
    cfg->cardsPerLevel  = static_cast<uint32_t>(take(r["cards_per_level"]));

    const json::Value grades = r["grades"];
    if (grades.size() > MAX_CARD_GRADES) {
        return fail(DataFile::Cards, "카드 등급이 상한을 넘는다", "grades");
    }
    for (uint32_t i = 0; i < grades.size(); ++i) {
        const json::Value g = grades.at(i);
        cfg->cardGradeRate[i]   = take32(g["rate_permille"]);
        cfg->cardGradeBudget[i] = take32(g["power_budget_permille"]);
        cfg->cardGradeStep[i]   = take32(g["value_unit_permille"]);
    }

    const json::Value eng = r["engravings"];
    if (eng.size() > MAX_ENGRAVINGS) {
        return fail(DataFile::Cards, "각인이 상한을 넘는다", "engraves");
    }
    cfg->engraveCount = eng.size();
    for (uint32_t i = 0; i < eng.size(); ++i) cfg->engraveBase[i] = take32(eng.at(i)["uncommon_permille"]);

    const json::Value rel = r["relics"];
    if (rel.size() > MAX_RELICS) {
        return fail(DataFile::Cards, "유물이 상한을 넘는다", "relics");
    }
    cfg->relicCount = rel.size();
    for (uint32_t i = 0; i < rel.size(); ++i) cfg->relicBase[i] = take32(rel.at(i)["uncommon_permille"]);

    // 훅형 유물·각인의 파라미터 — 수치형과 달리 스탯이 아니라 코드가 읽는다 (§4)
    cfg->decayTicks        = take32(r["decay_ticks"]);
    cfg->rageDurationTicks = take32(r["rage_duration_ticks"]);
    cfg->rageMaxStacks     = take32(r["rage_max_stacks"]);
    cfg->boltIntervalTicks = take32(r["bolt_interval_ticks"]);
    cfg->frostRadius   = Fixed::fromPermille(take32(r["frost_radius_millitile"]));
    cfg->stormRadius   = Fixed::fromPermille(take32(r["storm_radius_millitile"]));
    cfg->stormDpsPermille = take32(r["storm_dps_permille"]);
    cfg->pierceWidth   = Fixed::fromPermille(take32(r["pierce_width_millitile"]));
    cfg->pierceLength  = Fixed::fromPermille(take32(r["pierce_length_millitile"]));
    cfg->swarmRadius   = Fixed::fromPermille(take32(r["swarm_radius_millitile"]));
    cfg->swarmMaxStacks = take32(r["swarm_max_stacks"]);

    // RL_FORGE 등급 확률. **합이 1000이어야 한다** — 모자라면 아무 일도 일어나지
    // 않는 발동이 생기고, 넘치면 뒤쪽 등급이 도달 불가가 된다. 둘 다 조용히
    // 틀리는 종류라 여기서 막는다. 칸 수는 `items.json`의 등급 수와 맞물리므로
    // `loadItems`가 한 번 더 대조한다 (cards가 items보다 먼저 읽힌다).
    const json::Value fr = r["forge_tier_rate_permille"];
    if (fr.size() == 0 || fr.size() > MAX_ITEM_TIERS) {
        return fail(DataFile::Cards, "화로 등급 확률 칸 수가 범위를 벗어난다",
                    "forge_tier_rate_permille");
    }
    int32_t frSum = 0;
    for (uint32_t i = 0; i < fr.size(); ++i) {
        cfg->forgeTierRate[i] = take32(fr.at(i));
        if (cfg->forgeTierRate[i] < 0) {
            return fail(DataFile::Cards, "화로 등급 확률이 음수다",
                        "forge_tier_rate_permille");
        }
        frSum += cfg->forgeTierRate[i];
    }
    if (frSum != 1000) {
        return fail(DataFile::Cards, "화로 등급 확률 합이 1000이 아니다",
                    "forge_tier_rate_permille");
    }

    // ── 고유 각인 6종 (전설 풀 3~8번) ──
    //
    // **id로 찾는다. 배열 순서를 믿지 않는다.** 순서는 전설 풀 인덱스와 같아야
    // 하지만(리플레이), 그 불변식은 아래에서 따로 검사한다 — 순서에 의존해 읽으면
    // 데이터에서 두 줄이 바뀌었을 때 엉뚱한 각인에 수치가 들어가고 아무도 모른다.
    const json::Value ue = r["unique_engravings"];
    if (ue.size() == 0) {
        return fail(DataFile::Cards, "고유 각인이 비어 있다", "unique_engravings");
    }
    // 전설 풀 = 전설 유물 3종 + 고유 각인. 칸 수가 맞지 않으면 뽑히지 않는
    // 각인이나 효과 없는 칸이 생기는데, 둘 다 조용히 틀린다.
    if (ue.size() + 3 != cfg->legendPoolSize) {
        return fail(DataFile::Cards, "전설 풀 크기가 유물 3 + 고유 각인 수와 다르다",
                    "legend_pool_size");
    }
    {
        // 기대 순서 — `LegendId` 3~8과 같아야 한다.
        static const char* kOrder[6] = {"W_EXECUTE", "W_SHOCKWAVE", "W_VORTEX",
                                        "W_CENTRIFUGE", "W_AFTERSHOCK", "W_FISSURE"};
        for (uint32_t i = 0; i < ue.size() && i < 6; ++i) {
            if (!ue.at(i)["id"].strEquals(kOrder[i])) {
                return fail(DataFile::Cards, "고유 각인 순서가 LegendId와 다르다",
                            "unique_engravings");
            }
        }
        // **각 각인을 스킬에 묶는다.** 이름 → 인덱스는 데이터가 정한다.
        for (uint32_t i = 0; i < ue.size() && i < 6; ++i) {
            const json::Value want = ue.at(i)["skill"];
            uint32_t found = skillIdCount_;
            for (uint32_t k = 0; k < skillIdCount_; ++k) {
                if (want.strSame(skillIds_[k])) { found = k; break; }
            }
            // **모르는 스킬 id를 조용히 넘기지 않는다.** 오타 하나면 그 각인이
            // 어느 스킬에도 붙지 않는데, 증상은 "전설이 약하다"뿐이라 안 보인다.
            if (found == skillIdCount_) {
                return fail(DataFile::Cards, "고유 각인이 모르는 스킬을 가리킨다",
                            "skill");
            }
            cfg->uniqueSkill[i] = static_cast<int32_t>(found);
            feed(found);
        }

        const json::Value ex = ue.at(0);
        cfg->executeThresholdPermille = take32(ex["threshold_permille"]);
        cfg->executeAllAttacks        = ex["scope"].strEquals("all_attacks");
        feed(cfg->executeAllAttacks ? 1 : 0);

        cfg->shockwaveWidth = Fixed::fromPermille(take32(ue.at(1)["width_millitile"]));

        cfg->vortexDurationTicks = take32(ue.at(2)["duration_ticks"]);
        cfg->vortexDpsPermille   = take32(ue.at(2)["dps_permille"]);

        cfg->centrifugeStepPermille = take32(ue.at(3)["radius_step_permille"]);
        cfg->centrifugeMaxStacks    = take32(ue.at(3)["max_stacks"]);

        cfg->aftershockDamagePermille = take32(ue.at(4)["damage_permille"]);
        cfg->aftershockFuseTicks      = take32(ue.at(4)["fuse_ticks"]);

        cfg->fissureSlowPermille  = take32(ue.at(5)["slow_permille"]);
        cfg->fissureRadius        = Fixed::fromPermille(take32(ue.at(5)["radius_millitile"]));
        cfg->fissureDurationTicks = take32(ue.at(5)["duration_ticks"]);
        cfg->fissureDpsPermille   = take32(ue.at(5)["dps_permille"]);
    }

    // 일반 등급 스탯 카드 풀. **여기서 빌드 축이 갈린다.**
    const json::Value pool = r["stat_card_pool"];
    if (pool.size() > STAT_COUNT) {
        return fail(DataFile::Cards, "스탯 카드 풀이 상한을 넘는다", "stat_card_pool");
    }
    cfg->statCardPoolSize = pool.size();
    for (uint32_t i = 0; i < pool.size(); ++i) {
        const json::Value s = pool.at(i);
        uint32_t found = STAT_COUNT;
        for (uint32_t k = 0; k < STAT_COUNT; ++k) {
            if (s.strEquals(statJsonName(k))) { found = k; break; }
        }
        if (found == STAT_COUNT) {
            return fail(DataFile::Cards, "모르는 스탯 이름", "stat_card_pool");
        }
        cfg->statCardPool[i] = static_cast<uint8_t>(found);
        feed(found);
    }
    return sweep(DataFile::Cards, d);
}

inline bool ConfigLoader::loadSkills(const json::Doc& d, SimConfig* cfg) {
    const json::Value ss = d.root()["skills"];
    if (ss.size() > MAX_SKILLS) {
        return fail(DataFile::Skills, "스킬이 상한을 넘는다", "skills");
    }
    cfg->skillCount = ss.size();
    for (uint32_t i = 0; i < ss.size(); ++i) {
        const json::Value s = ss.at(i);
        cfg->skills[i].mult   = Fixed::fromPermille(take32(s["mult_permille"]));
        cfg->skills[i].weight = take32(s["weight_permille"]);
        cfg->skills[i].aoe    = s["aoe"].asBool();
        feed(cfg->skills[i].aoe ? 1 : 0);
        cfg->skills[i].ccGainPermille = take32(s["cc_gain_permille"]);
        // 광역 중심 — **어휘는 데이터가 정한다.** 모르는 값을 조용히 hero로
        // 떨어뜨리면 오타 하나가 "전방 범위가 영웅 중심이 된다"로 통과하는데,
        // 그게 바로 이번에 고친 버그의 모양이다.
        {
            const json::Value c = s["aoe_center"];
            if (c.strEquals("hero"))         cfg->skills[i].center = AoeCenter::Hero;
            else if (c.strEquals("forward")) cfg->skills[i].center = AoeCenter::Forward;
            else return fail(DataFile::Skills, "모르는 광역 중심 이름", "aoe_center");
            feed(static_cast<int64_t>(cfg->skills[i].center));
        }
        skillIds_[i] = s["id"];      // loadCards가 고유 각인을 여기에 묶는다
    }
    skillIdCount_ = ss.size();
    return sweep(DataFile::Skills, d);
}

inline bool ConfigLoader::loadItems(const json::Doc& d, SimConfig* cfg, RecipeTable* recipes,
                                   ItemMeta* meta) {
    const json::Value root = d.root();
    const json::Value commons = root["commons"];
    const json::Value rs      = root["recipes"];

    // **인덱스는 등장 순서다** — 흔함 9종이 0..8, 조합 결과가 9.. 이어진다.
    // 리플레이가 인덱스로 기록되므로 순서를 바꾸면 기존 로그가 깨진다 (§5).
    const uint32_t itemCount = commons.size() + rs.size();
    if (itemCount > DC_MAX_ITEM_TYPES_CFG || itemCount > config::MAX_ITEM_TYPES) {
        return fail(DataFile::Items, "아이템 종류가 상한을 넘는다");
    }
    cfg->itemTypeCount = itemCount;

    // id → 인덱스. 문자열을 복사하지 않고 원문 위치만 들고 선형 탐색한다 —
    // 51종이라 해시 테이블이 오히려 느리고, `std::hash`는 쓸 수도 없다.
    std::vector<json::Value> ids;
    ids.reserve(itemCount);
    for (uint32_t i = 0; i < commons.size(); ++i) ids.push_back(commons.at(i)["id"]);
    for (uint32_t i = 0; i < rs.size(); ++i)      ids.push_back(rs.at(i)["id"]);

    // 스탯 표 — 흔함과 조합 결과를 같은 방식으로 읽는다.
    auto readStats = [&](const json::Value& item, uint32_t idx) {
        const json::Value st = item["stats_permille"];
        for (uint32_t s = 0; s < STAT_COUNT; ++s) {
            const char* name = statJsonName(s);
            cfg->itemStats[idx][s] = st.has(name) ? take32(st[name]) : 0;
        }
        // CC 축은 스탯이 아니라 둔화다 — 모디파이어 스택을 타지 않는다 (§9)
        cfg->itemSlowAura[idx] = st.has("slow_aura") ? take32(st["slow_aura"]) : 0;
    };
    for (uint32_t i = 0; i < commons.size(); ++i) readStats(commons.at(i), i);
    for (uint32_t i = 0; i < rs.size(); ++i)      readStats(rs.at(i), commons.size() + i);

    // ── 등급 · 축 ──
    //
    // **등급은 시뮬도 읽는다** (RL_FORGE가 등급으로 추첨한다). 축은 하네스의
    // 카드 정책 전용이라 `meta`가 있을 때만 채운다.
    const json::Value axes   = root["axes"];
    const json::Value grades = root["grades"];
    if (grades.size() == 0 || grades.size() > MAX_ITEM_TIERS) {
        return fail(DataFile::Items, "등급 수가 범위를 벗어난다", "grades");
    }
    cfg->itemTierCount = grades.size();

    // **어휘를 C++가 알고 있으면 그것도 하드코딩이다** — 목록에서 인덱스를 찾는다.
    auto indexIn = [&](const json::Value& list, const json::Value& name) -> uint32_t {
        for (uint32_t i = 0; i < list.size(); ++i) {
            if (name.strSame(list.at(i))) return i;
        }
        return list.size();   // 못 찾음
    };
    for (uint32_t i = 0; i < itemCount; ++i) {
        const json::Value item = i < commons.size()
            ? commons.at(i) : rs.at(i - commons.size());
        const uint32_t g = indexIn(grades, item["grade"]);
        if (g >= grades.size()) return fail(DataFile::Items, "모르는 등급 이름", "grade");
        cfg->itemTier[i] = static_cast<uint8_t>(g);
        feed(g);   // 등급이 시뮬에 들어가므로 지문에도 들어간다

        if (meta != nullptr) {
            const uint32_t a = indexIn(axes, item["axis"]);
            if (a >= axes.size()) return fail(DataFile::Items, "모르는 축 이름", "axis");
            meta->axis[i] = static_cast<uint8_t>(a);
        }
    }
    if (meta != nullptr) {
        const json::Value power = root["tier_power_permille"];
        if (power.size() != grades.size()) {
            return fail(DataFile::Items, "등급 수와 위력 표 길이가 다르다",
                        "tier_power_permille");
        }
        for (uint32_t i = 0; i < power.size(); ++i) meta->tierPower[i] = power.at(i).asI32();
    }

    // 등급별 목록을 **계수 정렬**로 CSR에 눕힌다. 두 패스뿐이고(세기 → 배치)
    // 같은 등급 안에서 아이템 인덱스 순서가 보존된다 — RL_FORGE가 O(1)로
    // "등급 안에서 균등"을 뽑는 근거다 (`SimConfig::tierItems` 주석 참조).
    for (uint32_t i = 0; i < itemCount; ++i) ++cfg->tierStart[cfg->itemTier[i] + 1];
    for (uint32_t t = 0; t < cfg->itemTierCount; ++t) {
        cfg->tierStart[t + 1] += cfg->tierStart[t];
    }
    {
        uint32_t cursor[MAX_ITEM_TIERS + 1];
        for (uint32_t t = 0; t <= cfg->itemTierCount; ++t) cursor[t] = cfg->tierStart[t];
        for (uint32_t i = 0; i < itemCount; ++i) {
            cfg->tierItems[cursor[cfg->itemTier[i]]++] = static_cast<uint16_t>(i);
        }
    }

    // 화로 확률 표는 cards.json에서 왔다. **등급 수와 칸 수가 어긋나면 조용히
    // 도달 불가 등급이나 아무 일도 없는 발동이 생긴다** — 두 파일을 맞대 보는
    // 유일한 지점이 여기다 (합 검사는 `loadCards`가 이미 했다).
    for (uint32_t t = 0; t < cfg->itemTierCount; ++t) {
        if (cfg->forgeTierRate[t] != 0 && cfg->tierItemCount(t) == 0) {
            return fail(DataFile::Items, "화로가 아이템 없는 등급을 뽑게 되어 있다",
                        "grades");
        }
    }
    for (uint32_t t = cfg->itemTierCount; t < MAX_ITEM_TIERS; ++t) {
        if (cfg->forgeTierRate[t] != 0) {
            return fail(DataFile::Items, "화로 등급 확률이 등급 수보다 많다",
                        "forge_tier_rate_permille");
        }
    }

    // 뽑기 풀 — **흔함만 나온다** (§5). 조합으로만 올라간다는 규칙이 여기 있다.
    cfg->commonPoolSize = commons.size();
    for (uint32_t i = 0; i < commons.size(); ++i) cfg->commonPool[i] = static_cast<ItemId>(i);

    // 조합식. 재료는 id 문자열이라 위 `ids` 목록에서 인덱스로 바꾼다.
    std::vector<RecipeData> table;
    table.reserve(rs.size());
    for (uint32_t i = 0; i < rs.size(); ++i) {
        const json::Value ing = rs.at(i)["ingredients"];
        if (ing.size() == 0 || ing.size() > config::MAX_RECIPE_INGREDIENTS) {
            return fail(DataFile::Items, "재료 수가 범위를 벗어난다", "ingredients");
        }
        RecipeData rd;
        rd.result = static_cast<ItemId>(commons.size() + i);
        rd.count  = static_cast<uint8_t>(ing.size());
        for (uint32_t k = 0; k < ing.size(); ++k) {
            const json::Value want = ing.at(k);
            uint32_t found = itemCount;
            for (uint32_t j = 0; j < itemCount; ++j) {
                if (want.strSame(ids[j])) { found = j; break; }
            }
            // **없는 id를 조용히 넘기지 않는다.** 오타 하나가 "재료가 빠진
            // 조합식"으로 통과하면 그 아이템은 영영 만들어지지 않는데 아무도 모른다.
            if (found == itemCount) {
                return fail(DataFile::Items, "모르는 재료 id", "ingredients");
            }
            rd.ingredients[k] = static_cast<ItemId>(found);
            feed(found);
        }
        feed(rd.result);
        table.push_back(rd);
    }
    if (!sweep(DataFile::Items, d)) return false;

    const RecipeTableStatus st = recipes->build(table.data(),
                                                static_cast<uint32_t>(table.size()),
                                                itemCount);
    if (st != RecipeTableStatus::Ok) {
        return fail(DataFile::Items, "조합 테이블 빌드 실패", "recipes");
    }
    return true;
}

// 로드한 설정으로 World를 쓸 수 있는 상태로 만든다.
//
// **`World::init()`만으로는 부족하다.** 인벤토리는 `RecipeTable`이 있어야 초기화할
// 수 있어서 `init()`이 완전 초기화만 하고 남겨 두고, 스탯 기준값은 데이터에서
// 온다. 이 둘을 빠뜨리면 아이템 뽑기가 **조용히 거부되고**(`Inventory::matches`가
// 테이블 해시로 걸러낸다) 영웅이 맨몸으로 싸운다 — 증상이 크래시가 아니라
// "약하다"라서 찾기 어렵다. 한 함수로 묶어 빠뜨릴 자리를 없앤다.
// **설정을 통째로 받는다.** 지문만 옵션 인자로 두면 대부분의 호출부가 기본값 0을
// 넘겨 "데이터가 다른지 보는 검사"가 조용히 꺼진 채 초록불이 된다.
inline void initWorld(World& w, uint64_t seed, const SimConfig& cfg,
                      const RecipeTable& table, const HeroBaseline& hero) {
    w.init(seed);
    w.bindDataHash(cfg.dataHash);
    w.inventory.init(table);
    w.hero.stats.init(hero.bases, hero.bounds);
    w.refreshAllConditions();
}

}  // namespace dc

#endif  // DC_CONFIG_LOADER_H
