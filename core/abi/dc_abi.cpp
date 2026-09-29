// C ABI 표면 구현.
//
// **코어는 한 줄도 건드리지 않는다.** 이 파일은 얇은 껍데기이고, 안에서 기존 C++
// API를 그냥 호출한다. 그게 가능한 이유는 결정론 규칙이 코어를 이미 POD·SoA·고정폭으로
// 만들어 놨기 때문이다 — 보통 C ABI 작업의 대부분인 "내부를 POD로 뜯어고치기"가
// 이미 끝나 있다.
//
// ## 이 파일이 지키는 것
//
//   1. 모든 경계 함수는 noexcept다. 예외가 C 호출자에게 전파되면 std::terminate이고,
//      Unity에서는 에디터까지 같이 죽는다. 안을 try/catch로 감싸 반환 코드로 바꾼다
//   2. 시그니처에 C++ 타입이 없다. **컴파일러가 막아주지 않으므로** 규칙으로 지킨다
//   3. 구조체를 노출하지 않는다 — World는 표준 레이아웃이 아니라 오프셋이 구현 정의다
//
// 함수 **본문 안에서는** C++를 자유롭게 쓴다. 아래에도 참조·템플릿·RAII가 나온다.
#include "dc_abi.h"

#include <new>

#include "../include/dc/sim.h"
#include "../include/dc/world.h"
#include "../include/dc/config_loader.h"

namespace {

using namespace dc;

// 설정 핸들. 로더와 결과를 함께 들고 있다 — C#는 안을 모르고 IntPtr만 넘긴다.
//
// **월드가 이걸 참조하지 않는다.** 만들 때 값으로 복사하므로 설정을 먼저
// 해제해도 월드는 멀쩡하다 (수명 규칙이 하나 줄어든다).
struct ConfigHandle {
    ConfigLoader loader;
    SimConfig    cfg{};
    RecipeTable  table{};
    HeroBaseline hero{};
    bool         loaded = false;
};

// 핸들이 들고 다니는 것. **World만으로는 부족하다** — stepWorld가 SimScratch를
// 받고, applyInput이 SimConfig와 RecipeTable을 받는다. 호출자에게 그 수명을
// 관리시키면 표면이 그만큼 넓어지므로 전부 핸들 안에 넣는다.
//
// SimScratch를 여기 두는 것이 결정론에도 맞다 — 전역 정적으로 두면 월드 여러 개를
// 동시에 돌릴 때(몬테카를로 병렬화) 서로의 스크래치를 밟는다.
struct Handle {
    World       world{};
    SimConfig   cfg{};
    RecipeTable table{};
    SimScratch  scratch{};
};

inline Handle*       self(DcWorld* h)       { return reinterpret_cast<Handle*>(h); }
inline const Handle* self(const DcWorld* h) { return reinterpret_cast<const Handle*>(h); }
inline ConfigHandle* cself(DcConfig* h)             { return reinterpret_cast<ConfigHandle*>(h); }
inline const ConfigHandle* cself(const DcConfig* h) { return reinterpret_cast<const ConfigHandle*>(h); }

// 설정은 호스트가 넘긴 `data/*.json`에서 온다. **복사해 들고 간다** —
// 설정 핸들의 수명과 월드의 수명을 묶지 않기 위해서다.
void setup(Handle& h, const ConfigHandle& c, uint64_t seed) {
    h.cfg   = c.cfg;
    h.table = c.table;
    initWorld(h.world, seed, h.cfg, h.table, c.hero);
}

}  // namespace

// ── 버전 ──────────────────────────────────────────────────────────────────

extern "C" int32_t dc_abi_version(void) noexcept { return DC_ABI_VERSION; }

extern "C" int32_t dc_fixed_one(void) noexcept { return Fixed::ONE_RAW; }

// ── 수명 ──────────────────────────────────────────────────────────────────

// ── 설정 ──────────────────────────────────────────────────────────────────

extern "C" int32_t dc_config_file_count(void) noexcept {
    return static_cast<int32_t>(DATA_FILE_COUNT);
}

extern "C" const char* dc_config_file_name(int32_t index) noexcept {
    if (index < 0 || index >= static_cast<int32_t>(DATA_FILE_COUNT)) return nullptr;
    return dataFileName(static_cast<DataFile>(index));
}

extern "C" DcConfig* dc_config_create(void) noexcept {
    ConfigHandle* c = new (std::nothrow) ConfigHandle();
    return reinterpret_cast<DcConfig*>(c);
}

extern "C" void dc_config_destroy(DcConfig* c) noexcept {
    delete cself(c);
}

extern "C" int32_t dc_config_set(DcConfig* c, int32_t index,
                                 const char* text, int32_t len) noexcept {
    if (c == nullptr || text == nullptr) return DC_ERR_NULL;
    if (index < 0 || index >= static_cast<int32_t>(DATA_FILE_COUNT) || len < 0) {
        return DC_ERR_ARG;
    }
    cself(c)->loader.set(static_cast<DataFile>(index), text, static_cast<size_t>(len));
    return DC_OK;
}

extern "C" int32_t dc_config_load(DcConfig* c) noexcept {
    if (c == nullptr) return DC_ERR_NULL;
    try {
        ConfigHandle& h = *cself(c);
        h.loaded = h.loader.load(&h.cfg, &h.table, &h.hero);
        return h.loaded ? DC_OK : DC_ERR_INPUT;
    } catch (...) {
        return DC_ERR_INTERNAL;
    }
}

extern "C" const char* dc_config_error(const DcConfig* c) noexcept {
    return c != nullptr ? cself(c)->loader.error() : nullptr;
}
extern "C" const char* dc_config_error_key(const DcConfig* c) noexcept {
    return c != nullptr ? cself(c)->loader.errorKey() : nullptr;
}
extern "C" int32_t dc_config_error_file(const DcConfig* c) noexcept {
    return c != nullptr ? static_cast<int32_t>(cself(c)->loader.errorFile()) : DC_ERR_NULL;
}
extern "C" uint64_t dc_config_data_hash(const DcConfig* c) noexcept {
    return c != nullptr ? cself(c)->cfg.dataHash : 0u;
}

extern "C" DcWorld* dc_world_create_with(const DcConfig* cfg, uint64_t seed) noexcept {
    if (cfg == nullptr || !cself(cfg)->loaded) return nullptr;
    // World가 131 KB라 스택에 두지 않는다 (보간 버퍼 8 KB 포함).
    // nothrow로 받아 예외 경로를 아예 없앤다.
    Handle* h = new (std::nothrow) Handle();
    if (h == nullptr) return nullptr;
    try {
        setup(*h, *cself(cfg), seed);
    } catch (...) {
        delete h;
        return nullptr;
    }
    return reinterpret_cast<DcWorld*>(h);
}

extern "C" void dc_world_destroy(DcWorld* w) noexcept {
    delete self(w);   // delete nullptr는 정의된 동작이다
}

// ── 진행 ──────────────────────────────────────────────────────────────────

extern "C" int32_t dc_step(DcWorld* w, int32_t ticks) noexcept {
    if (w == nullptr) return DC_ERR_NULL;
    if (ticks < 0)    return DC_ERR_ARG;
    try {
        Handle& h = *self(w);
        for (int32_t i = 0; i < ticks; ++i) {
            // 런이 끝났으면 더 전진하지 않는다. 호출자가 결과를 읽기 전에
            // 상태가 더 움직이면 "어느 틱에 끝났는가"가 흐려진다.
            if (h.world.run.over()) break;
            stepWorld(h.world, h.cfg, h.scratch);
        }
        return DC_OK;
    } catch (...) {
        return DC_ERR_INTERNAL;
    }
}

extern "C" int32_t dc_input(DcWorld* w, int32_t kind, uint32_t value) noexcept {
    if (w == nullptr) return DC_ERR_NULL;
    if (kind <= static_cast<int32_t>(InputKind::None)
        || kind >= static_cast<int32_t>(InputKind::Count)) {
        return DC_ERR_ARG;
    }
    try {
        Handle& h = *self(w);
        InputEvent e;
        e.tick  = h.world.tickCount();
        e.kind  = static_cast<InputKind>(kind);
        e.value = value;
        return applyInput(h.world, h.cfg, h.table, e) ? DC_OK : DC_ERR_INPUT;
    } catch (...) {
        return DC_ERR_INTERNAL;
    }
}

// ── 상태 읽기 ─────────────────────────────────────────────────────────────
//
// 전부 단순 접근이라 예외가 날 자리가 없지만 noexcept는 붙인다 — 표면의 규칙을
// 함수마다 다르게 두면 나중에 누가 어느 쪽이었는지 기억해야 한다.

extern "C" int32_t dc_entity_count(const DcWorld* w) noexcept {
    return w ? static_cast<int32_t>(self(w)->world.entities.count()) : DC_ERR_NULL;
}

// **Fixed 배열을 int32_t 배열로 그대로 내보낸다.** Fixed가 표준 레이아웃이고
// raw 하나만 담은 4바이트라 성립한다 — 그래서 복사가 0이다.
// (World가 표준 레이아웃이 아닌 것과 대비된다. 그건 구조체째 못 넘긴다)
extern "C" const int32_t* dc_entity_pos_x(const DcWorld* w) noexcept {
    return w ? &self(w)->world.entities.posX[0].raw : nullptr;
}
extern "C" const int32_t* dc_entity_pos_y(const DcWorld* w) noexcept {
    return w ? &self(w)->world.entities.posY[0].raw : nullptr;
}
extern "C" const uint8_t* dc_entity_archetype(const DcWorld* w) noexcept {
    // Archetype은 uint8_t 기반 enum class다. 평면 바이트 배열로 그대로 나간다.
    return w ? reinterpret_cast<const uint8_t*>(&self(w)->world.entities.archetype[0])
             : nullptr;
}

extern "C" const int32_t* dc_entity_prev_x(const DcWorld* w) noexcept {
    return w ? &self(w)->world.entities.renderPrevX[0].raw : nullptr;
}
extern "C" const int32_t* dc_entity_prev_y(const DcWorld* w) noexcept {
    return w ? &self(w)->world.entities.renderPrevY[0].raw : nullptr;
}

// EntityId는 uint32_t 하나를 담은 표준 레이아웃이라 평면 배열로 그대로 나간다.
extern "C" const uint32_t* dc_entity_id(const DcWorld* w) noexcept {
    return w ? self(w)->world.entities.idData() : nullptr;
}

extern "C" int32_t dc_hero_pos_x(const DcWorld* w) noexcept {
    return w ? self(w)->world.hero.posX.raw : DC_ERR_NULL;
}
extern "C" int32_t dc_hero_pos_y(const DcWorld* w) noexcept {
    return w ? self(w)->world.hero.posY.raw : DC_ERR_NULL;
}
extern "C" int32_t dc_hero_corruption(const DcWorld* w) noexcept {
    return w ? self(w)->world.hero.corruption.raw : DC_ERR_NULL;
}
extern "C" int32_t dc_hero_corruption_max(const DcWorld* w) noexcept {
    return w ? self(w)->world.hero.corruptionMax().raw : DC_ERR_NULL;
}
extern "C" int32_t dc_hero_level(const DcWorld* w) noexcept {
    return w ? self(w)->world.hero.level : DC_ERR_NULL;
}

extern "C" int32_t dc_run_tick(const DcWorld* w) noexcept {
    return w ? self(w)->world.tickCount() : DC_ERR_NULL;
}
extern "C" int32_t dc_run_segment(const DcWorld* w) noexcept {
    return w ? self(w)->world.run.segmentIndex : DC_ERR_NULL;
}
extern "C" int32_t dc_run_clear_points(const DcWorld* w) noexcept {
    return w ? self(w)->world.run.clearPoints : DC_ERR_NULL;
}
extern "C" int32_t dc_run_outcome(const DcWorld* w) noexcept {
    return w ? static_cast<int32_t>(self(w)->world.run.outcome) : DC_ERR_NULL;
}

// ── 검증 ──────────────────────────────────────────────────────────────────

extern "C" int32_t dc_checksums(const DcWorld* w, DcChecksums* out) noexcept {
    if (w == nullptr || out == nullptr) return DC_ERR_NULL;
    try {
        const Checksums c = self(w)->world.checksums();
        out->total    = c.total;
        out->entities = c.entities;
        out->hero     = c.hero;
        out->run      = c.run;
        out->spawn    = c.spawn;
        out->rng      = c.rng;
        return DC_OK;
    } catch (...) {
        return DC_ERR_INTERNAL;
    }
}

extern "C" int32_t dc_headless_checksum(const DcConfig* cfg, uint64_t seed, int32_t ticks,
                                        const int32_t* log, int32_t len,
                                        DcChecksums* out) noexcept {
    if (out == nullptr || cfg == nullptr) return DC_ERR_NULL;
    if (ticks < 0 || len < 0) return DC_ERR_ARG;
    if (log == nullptr && len != 0) return DC_ERR_NULL;
    if (len % 3 != 0) return DC_ERR_ARG;   // (tick, kind, value) 3튜플이어야 한다

    // **클라이언트 표면과 같은 코어를 쓴다.** 여기서 World를 따로 만들지 않고
    // dc_world_create_with를 부르는 것이 그 사실을 코드로 남기는 방법이다 —
    // 두 경로가 갈라지는 순간 리플레이 검증의 전제가 무너진다.
    DcWorld* w = dc_world_create_with(cfg, seed);
    if (w == nullptr) return DC_ERR_ALLOC;

    int32_t rc = DC_OK;
    const int32_t events = len / 3;
    int32_t next = 0;   // 다음에 적용할 이벤트

    for (int32_t t = 0; t < ticks && rc == DC_OK; ++t) {
        // 이 틱에 기록된 입력을 전부 적용한 뒤 전진한다. 로그는 틱 오름차순이다
        // (InputLog::record가 역행을 거부한다).
        while (next < events && log[next * 3] == t) {
            rc = dc_input(w, log[next * 3 + 1], static_cast<uint32_t>(log[next * 3 + 2]));
            if (rc != DC_OK) break;
            ++next;
        }
        if (rc != DC_OK) break;
        if (dc_run_outcome(w) != DC_RUN_RUNNING) break;
        rc = dc_step(w, 1);
    }

    if (rc == DC_OK) rc = dc_checksums(w, out);
    dc_world_destroy(w);
    return rc;
}
