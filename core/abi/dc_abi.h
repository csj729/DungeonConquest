/* DungeonConquest 시뮬 코어 — C ABI 표면.
 *
 * **이 헤더는 순수 C다.** C++ 타입이 하나도 나오지 않고 C 컴파일러로 그대로 읽힌다.
 * 그게 이 파일의 유일한 존재 이유다 — C++에는 표준 ABI가 없으므로(이름 맹글링·예외
 * 전파·STL 내부 구조가 전부 구현 정의), C++ 타입을 경계 밖에 내보내면 그걸 빌드한
 * 바로 그 컴파일러의 그 버전에서만 동작한다. Unity는 IL2CPP로 플랫폼마다 다른
 * 툴체인을 쓰므로 성립하지 않는다.
 *
 * ## 같은 ABI의 두 진입점 (CLAUDE.md)
 *
 *   클라이언트 (Unity)   dc_world_create → dc_step → 상태 포인터 읽기
 *   서버 (ASP.NET Core)  dc_headless_checksum — 입력 로그를 재생해 체크섬만 반환
 *
 * 서버 표면이 클라이언트 표면과 같은 라이브러리를 쓰므로 **시뮬 구현은 하나뿐**이다.
 * 두 언어로 각각 구현하면 결과 일치를 보장할 수 없고 리플레이 검증이 성립하지 않는다.
 *
 * ## 경계의 규칙 — 세 가지뿐이다
 *
 * 함수 **본문 안에서는 C++를 전부 쓴다**(템플릿·STL·가상 함수 자유). 제약은 표면에만
 * 걸린다:
 *
 *   1. 시그니처에 STL·C++ 타입 금지 — **컴파일러가 막아주지 않으므로** 규칙으로 지킨다
 *   2. 예외가 함수 밖으로 탈출 금지 — 탈출하면 std::terminate다. 반환 코드로 바꾼다
 *   3. extern "C" 함수끼리 오버로딩 금지 — 심볼이 하나뿐이다 (이건 컴파일 에러로 막힌다)
 *
 * ## 구조체를 노출하지 않는 이유
 *
 * World·EntityStore는 **자명 복사(trivially copyable)는 되지만 표준 레이아웃이 아니다** —
 * 공개 SoA 배열과 비공개 count_가 섞여 있어서다. 즉 memcpy는 안전한데(snapshot.h가
 * 그걸 쓴다) **멤버 오프셋은 구현 정의**다. C# 쪽 [StructLayout]이 어긋나도 컴파일되고
 * 크래시도 안 나며, 좌표가 미묘하게 틀려 **체크섬만 갈라진다** — 이 프로젝트에서 가장
 * 찾기 힘든 종류의 버그다. 그래서 불투명 핸들 + 평면 포인터로만 내보낸다.
 */
#ifndef DC_ABI_H
#define DC_ABI_H

#include <stdint.h>

#if defined(_WIN32)
#  if defined(DC_ABI_BUILD)
#    define DC_API __declspec(dllexport)
#  else
#    define DC_API __declspec(dllimport)
#  endif
#else
#  define DC_API __attribute__((visibility("default")))
#endif

/* **경계 함수는 전부 noexcept다** — 예외가 C 호출자에게 전파되면 std::terminate이고,
 * Unity에서는 에디터까지 같이 죽는다. 그런데 noexcept는 C++17에서 **함수 타입의
 * 일부**라 선언과 정의가 일치해야 하고, 이 헤더는 순수 C로도 읽혀야 한다.
 * 그래서 언어별로 갈라 둔다. */
#ifdef __cplusplus
#  define DC_NOEXCEPT noexcept
#else
#  define DC_NOEXCEPT
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ── 버전 ────────────────────────────────────────────────────────────────
 * C#와 네이티브가 따로 배포되므로 **서로 다른 버전이 만날 수 있다.** 핸들을
 * 만들기 전에 dc_abi_version()을 확인해 맞지 않으면 즉시 실패하는 편이,
 * 엉뚱한 오프셋을 읽고 체크섬만 갈라지는 것보다 낫다.
 * 표면이 바뀌면(함수 추가·시그니처 변경·의미 변경) 올린다. */
/* 2: 설정이 data 디렉터리의 JSON에서 온다. dc_world_create(seed)가 사라지고
 *    dc_config_* + dc_world_create_with가 그 자리를 대신한다.
 * 3: 렌더 보간용 직전 위치와 엔티티 id가 추가됐다. */
#define DC_ABI_VERSION 3

/* ── 오류 코드 ───────────────────────────────────────────────────────────
 * 예외를 경계 밖으로 내보낼 수 없으므로 모든 실패는 여기로 온다.
 * 음수 = 실패. 0 이상 = 성공(또는 개수 같은 유의미한 값). */
typedef enum DcStatus {
    DC_OK             =  0,
    DC_ERR_NULL       = -1,   /* 핸들이나 출력 포인터가 NULL */
    DC_ERR_ARG        = -2,   /* 인자 범위 밖 (음수 틱 수 등) */
    DC_ERR_VERSION    = -3,   /* ABI 버전 불일치 */
    DC_ERR_ALLOC      = -4,   /* 할당 실패 */
    DC_ERR_INPUT      = -5,   /* 입력 이벤트가 거부됨 (틱 역행·용량 초과 등) */
    DC_ERR_INTERNAL   = -6    /* 코어에서 예외가 올라옴 — 경계에서 잡아 코드로 바꾼 것 */
} DcStatus;

/* 런 결과. World의 RunOutcome과 같은 값을 쓴다. */
typedef enum DcOutcome {
    DC_RUN_RUNNING = 0,
    DC_RUN_CLEARED = 1,
    DC_RUN_DEAD    = 2
} DcOutcome;

/* 입력 종류. InputKind와 같은 값을 쓴다 (input.h). */
typedef enum DcInputKind {
    DC_INPUT_NONE          = 0,
    DC_INPUT_MANUAL_TARGET = 1,   /* value = EntityId.bits (0이면 해제) */
    DC_INPUT_QTE_GRADE     = 2,   /* value = QteGrade */
    DC_INPUT_CARD_CHOICE   = 3,   /* value = 선택지 인덱스 */
    DC_INPUT_CRAFT         = 4    /* value = 조합식 인덱스 */
} DcInputKind;

/* 부분 체크섬. **어느 시스템에서 갈렸는지 즉시 특정하기 위해** 총합만이 아니라
 * 시스템별로 내보낸다 (CLAUDE.md: 시스템별 부분 체크섬도 함께 기록한다).
 * 전부 고정폭 정수라 레이아웃이 안전하다 — 이 구조체는 노출해도 된다. */
typedef struct DcChecksums {
    uint64_t total;
    uint64_t entities;
    uint64_t hero;
    uint64_t run;
    uint64_t spawn;
    uint64_t rng;
} DcChecksums;

/* 불투명 핸들. C#는 World의 내부를 전혀 모르고 IntPtr 하나만 들고 다닌다. */
typedef struct DcWorld DcWorld;

/* ── 수명 ───────────────────────────────────────────────────────────────── */

/* 이 라이브러리가 노출하는 ABI 버전. 핸들을 만들기 전에 확인한다. */
DC_API int32_t dc_abi_version(void) DC_NOEXCEPT;

/* ── 설정 ───────────────────────────────────────────────────────────────
 *
 * **코어는 파일을 읽지 않는다.** 호스트가 데이터 JSON을 읽어 버퍼로 넘긴다 —
 * Unity는 TextAsset, 서버는 File.ReadAllBytes다. 그래야 코어가 플랫폼별 파일
 * 시스템을 모르는 채로 양쪽에서 같은 바이너리로 돈다.
 *
 * 쓰는 순서: create → set(전부) → load → (월드 만들기) → destroy.
 * **버퍼는 dc_config_load가 돌아올 때까지 살아 있어야 한다** (파서가 문자열을
 * 복사하지 않는다). 그 뒤에는 설정이 정수만 들고 있으므로 놓아줘도 된다. */
typedef struct DcConfig DcConfig;

/* 호스트가 읽어 와야 하는 파일 수. dc_config_file_name으로 이름을 얻는다. */
DC_API int32_t     dc_config_file_count(void) DC_NOEXCEPT;
DC_API const char* dc_config_file_name(int32_t index) DC_NOEXCEPT;

DC_API DcConfig* dc_config_create(void) DC_NOEXCEPT;
DC_API void      dc_config_destroy(DcConfig* c) DC_NOEXCEPT;
DC_API int32_t   dc_config_set(DcConfig* c, int32_t index,
                               const char* text, int32_t len) DC_NOEXCEPT;
DC_API int32_t   dc_config_load(DcConfig* c) DC_NOEXCEPT;

/* 실패 원인. **조용히 기본값으로 때우지 않는다** — 어느 파일 어느 키인지 말한다.
 * 반환 문자열은 설정 핸들이 살아 있는 동안 유효하다. */
DC_API const char* dc_config_error(const DcConfig* c) DC_NOEXCEPT;
DC_API const char* dc_config_error_key(const DcConfig* c) DC_NOEXCEPT;
DC_API int32_t     dc_config_error_file(const DcConfig* c) DC_NOEXCEPT;
/* 로드한 데이터의 지문. **서버와 클라가 이 값이 다르면 같은 판이 아니다.** */
DC_API uint64_t    dc_config_data_hash(const DcConfig* c) DC_NOEXCEPT;

/* 실패하면 NULL. 해제는 dc_world_destroy로만 한다 (할당자가 다를 수 있으므로
 * C# 쪽에서 free하면 안 된다).
 *
 * 설정은 **복사된다** — 월드를 만든 뒤 설정 핸들을 해제해도 된다. */
DC_API DcWorld* dc_world_create_with(const DcConfig* cfg, uint64_t seed) DC_NOEXCEPT;
DC_API void     dc_world_destroy(DcWorld* w) DC_NOEXCEPT;

/* ── 진행 ───────────────────────────────────────────────────────────────── */

/* ticks만큼 시뮬을 전진시킨다. 런이 끝나면 그 자리에서 멈춘다.
 * **배속·일시정지는 여기서 구현하지 않는다** — 프레젠테이션이 이 함수를 얼마나
 * 자주 부르느냐로 정한다. 시뮬 틱 내부 계산은 항상 고정 속도다 (CLAUDE.md). */
DC_API int32_t dc_step(DcWorld* w, int32_t ticks) DC_NOEXCEPT;

/* 입력을 **현재 틱에** 적용한다. 조합처럼 전투 중에도 가능한 입력이 있으므로
 * (design.md §5) 정확히 어느 틱에 눌렀는지가 결과를 바꾼다. 거부되면 DC_ERR_INPUT. */
DC_API int32_t dc_input(DcWorld* w, int32_t kind, uint32_t value) DC_NOEXCEPT;

/* ── 상태 읽기 ───────────────────────────────────────────────────────────
 * **프레젠테이션 → 시뮬레이션 역방향 참조 금지** (CLAUDE.md). 전부 읽기 전용이고,
 * 포인터는 배열 시작 주소를 그대로 준다 — 복사가 0이다. C# 쪽에서
 * NativeArrayUnsafeUtility.ConvertExistingDataToNativeArray로 감싸면 된다.
 *
 * **포인터 수명**: 다음 dc_step 또는 dc_world_destroy까지만 유효하다.
 * 엔티티는 틱 종료 시 일괄 압축되므로 인덱스도 그때 바뀐다.
 *
 * Fixed는 int32_t raw로 나간다. C#가 Fixed를 흉내 내는 대신 표시할 때만
 * 나누면 된다 — 결정론 규칙은 C++ 시뮬 코어에 한하고 프레젠테이션은 float를 써도
 * 결과에 영향이 없다 (CLAUDE.md). 나누는 값은 dc_fixed_one()이다. */
DC_API int32_t dc_fixed_one(void) DC_NOEXCEPT;          /* Fixed 1.0의 raw 값 (20.12 → 4096) */

DC_API int32_t dc_entity_count(const DcWorld* w) DC_NOEXCEPT;   /* 죽음 표시된 행 포함 */
DC_API const int32_t* dc_entity_pos_x(const DcWorld* w) DC_NOEXCEPT;
DC_API const int32_t* dc_entity_pos_y(const DcWorld* w) DC_NOEXCEPT;
DC_API const uint8_t* dc_entity_archetype(const DcWorld* w) DC_NOEXCEPT;

/* 직전 틱의 위치. **현재 행에 맞춰져 있다** — 틱 끝 압축이 행을 당겨 와도
 * 코어가 함께 옮기므로, 프레젠테이션은 짝짓기 없이 그대로 보간하면 된다:
 *
 *     x = lerp(prev_x[i], pos_x[i], alpha)      alpha = 틱 사이 진행도 0..1
 *
 * 이 배열이 없으면 보간이 성립하지 않는다. 행 인덱스가 틱 간에 안 맞으므로
 * 클라이언트가 지난 프레임 좌표를 들고 있어도 **어느 행이 누구였는지 모르고**,
 * 하나가 죽으면 그 뒤 엔티티가 전부 순간이동한다.
 *
 * 새로 스폰한 엔티티는 prev == pos라 제자리에서 나타난다. */
DC_API const int32_t* dc_entity_prev_x(const DcWorld* w) DC_NOEXCEPT;
DC_API const int32_t* dc_entity_prev_y(const DcWorld* w) DC_NOEXCEPT;

/* 엔티티 식별자. **행 인덱스는 정체성이 아니다** — 압축이 옮기므로 프레임마다
 * 다른 것을 가리킨다. 스프라이트·오브젝트 풀을 엔티티에 묶어 두려면 이 값으로
 * 짝지어야 한다. 0은 유효한 id가 아니다. */
DC_API const uint32_t* dc_entity_id(const DcWorld* w) DC_NOEXCEPT;
/* **살아있음 플래그는 내보내지 않는다.** 죽음은 틱 중간에 표시만 되고 stepWorld가
 * 끝에서 일괄 압축하므로, dc_step이 반환한 시점에 [0, count) 행은 전부 살아 있다.
 * 호출자가 걸러야 할 죽은 행이 애초에 없다. */

DC_API int32_t dc_hero_pos_x(const DcWorld* w) DC_NOEXCEPT;
DC_API int32_t dc_hero_pos_y(const DcWorld* w) DC_NOEXCEPT;
DC_API int32_t dc_hero_corruption(const DcWorld* w) DC_NOEXCEPT;
DC_API int32_t dc_hero_corruption_max(const DcWorld* w) DC_NOEXCEPT;
DC_API int32_t dc_hero_level(const DcWorld* w) DC_NOEXCEPT;

DC_API int32_t dc_run_tick(const DcWorld* w) DC_NOEXCEPT;
DC_API int32_t dc_run_segment(const DcWorld* w) DC_NOEXCEPT;      /* 맵 내 구간, 0 기반 */
DC_API int32_t dc_run_clear_points(const DcWorld* w) DC_NOEXCEPT;
DC_API int32_t dc_run_outcome(const DcWorld* w) DC_NOEXCEPT;      /* DcOutcome */

/* ── 검증 ───────────────────────────────────────────────────────────────── */

/* 현재 상태의 체크섬. out이 NULL이면 DC_ERR_NULL. */
DC_API int32_t dc_checksums(const DcWorld* w, DcChecksums* out) DC_NOEXCEPT;

/* **서버용 진입점.** 입력 로그를 재생해 체크섬만 돌려준다 — 상태 버퍼도 이벤트도
 * 만들지 않는다. 클라이언트 표면과 같은 라이브러리의 다른 함수일 뿐이라
 * 시뮬 구현이 하나로 유지된다.
 *
 * 입력 로그는 (tick, kind, value) 3튜플의 평면 배열이다. C#가 구조체 배열을
 * 마샬링하지 않아도 되도록 int32_t 3개씩 늘어놓는다:
 *
 *     [t0, k0, v0, t1, k1, v1, ...]   len = 이벤트 수 × 3
 *
 * log가 NULL이면 입력 없이 ticks만큼 돌린다. */
DC_API int32_t dc_headless_checksum(const DcConfig* cfg, uint64_t seed, int32_t ticks,
                                    const int32_t* log, int32_t len,
                                    DcChecksums* out) DC_NOEXCEPT;

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif  /* DC_ABI_H */
