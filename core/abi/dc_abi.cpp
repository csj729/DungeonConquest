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
#include "../tools/dev_data.h"

namespace {

using namespace dc;

// 핸들이 들고 다니는 것. **World만으로는 부족하다** — stepWorld가 SimScratch를
// 받고, applyInput이 SimConfig와 RecipeTable을 받는다. 호출자에게 그 수명을
// 관리시키면 표면이 그만큼 넓어지므로 전부 핸들 안에 넣는다.
//
// SimScratch를 여기 두는 것이 결정론에도 맞다 — 전역 정적으로 두면 월드 여러 개를
// 동시에 돌릴 때(몬테카를로 병렬화) 서로의 스크래치를 밟는다.
struct Handle {
    World      world{};
    SimConfig  cfg{};
    SimScratch scratch{};
};

inline Handle*       self(DcWorld* h)       { return reinterpret_cast<Handle*>(h); }
inline const Handle* self(const DcWorld* h) { return reinterpret_cast<const Handle*>(h); }

// **설정은 아직 임시 로더에서 온다** (core/tools/dev_data.h). 진짜 데이터 로더가
// 붙으면 여기만 바뀐다 — 표면은 그대로다.
void setup(Handle& h, uint64_t seed) {
    h.world.init(seed);
    h.cfg = dev::devConfig();
    dev::applyHeroBaseline(h.world);
}

}  // namespace

// ── 버전 ──────────────────────────────────────────────────────────────────

extern "C" int32_t dc_abi_version(void) noexcept { return DC_ABI_VERSION; }

extern "C" int32_t dc_fixed_one(void) noexcept { return Fixed::ONE_RAW; }

// ── 수명 ──────────────────────────────────────────────────────────────────

extern "C" DcWorld* dc_world_create(uint64_t seed) noexcept {
    // World가 123 KB라 스택에 두지 않는다. nothrow로 받아 예외 경로를 아예 없앤다.
    Handle* h = new (std::nothrow) Handle();
    if (h == nullptr) return nullptr;
    try {
        setup(*h, seed);
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
        return applyInput(h.world, h.cfg, dev::devRecipeTable(), e) ? DC_OK : DC_ERR_INPUT;
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

extern "C" int32_t dc_headless_checksum(uint64_t seed, int32_t ticks,
                                        const int32_t* log, int32_t len,
                                        DcChecksums* out) noexcept {
    if (out == nullptr) return DC_ERR_NULL;
    if (ticks < 0 || len < 0) return DC_ERR_ARG;
    if (log == nullptr && len != 0) return DC_ERR_NULL;
    if (len % 3 != 0) return DC_ERR_ARG;   // (tick, kind, value) 3튜플이어야 한다

    // **클라이언트 표면과 같은 코어를 쓴다.** 여기서 World를 따로 만들지 않고
    // dc_world_create를 부르는 것이 그 사실을 코드로 남기는 방법이다 —
    // 두 경로가 갈라지는 순간 리플레이 검증의 전제가 무너진다.
    DcWorld* w = dc_world_create(seed);
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
