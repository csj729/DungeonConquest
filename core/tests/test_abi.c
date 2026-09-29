/* C ABI 계약 테스트.
 *
 * **이 파일은 순수 C다.** C++ 컴파일러가 아니라 C 컴파일러로 빌드된다 —
 * 그래야 "헤더가 정말 C에서 읽히는가"가 주장이 아니라 빌드 결과가 된다.
 * C++로 테스트하면 헤더에 C++ 타입이 섞여 들어가도 통과해 버린다.
 *
 * 무엇을 보는가:
 *   1. 불투명 핸들 왕복 — 만들고 쓰고 해제
 *   2. NULL·범위 밖 인자가 크래시가 아니라 **오류 코드**로 돌아오는가
 *   3. **ABI를 거친 체크섬이 결정론적인가** — 같은 시드 두 번이 같은 값
 *   4. **서버 진입점이 클라이언트 경로와 같은 결과를 내는가**
 *      ← 이게 이 파일의 핵심이다. 갈라지는 순간 "시뮬 구현은 하나뿐"이 거짓이 되고
 *        리플레이 검증의 전제가 무너진다
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dc_abi.h"

/* 설정 버퍼. **C가 파일을 읽어 넘긴다** — 코어는 파일을 모른다.
 * Unity·서버가 할 일을 여기서 C로 한 번 해 보는 것이기도 하다. */
#define MAX_FILES 16
static char*  g_buf[MAX_FILES];
static long   g_len[MAX_FILES];

static int slurp(const char* path, char** out, long* outLen) {
    FILE* f = fopen(path, "rb");
    long n;
    if (f == NULL) return 0;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return 0; }
    *out = (char*)malloc((size_t)n);
    if (*out == NULL) { fclose(f); return 0; }
    if (fread(*out, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(*out); return 0; }
    fclose(f);
    *outLen = n;
    return 1;
}

/* 설정을 만들어 로드까지 한다. 실패하면 NULL. */
static DcConfig* makeConfig(void) {
    DcConfig* c = dc_config_create();
    int i;
    const int n = dc_config_file_count();
    if (c == NULL || n > MAX_FILES) return NULL;
    for (i = 0; i < n; ++i) {
        char path[512];
        snprintf(path, sizeof path, "%s/%s", DC_DATA_DIR, dc_config_file_name(i));
        if (!slurp(path, &g_buf[i], &g_len[i])) {
            printf("  파일을 못 읽었다: %s\n", path);
            dc_config_destroy(c);
            return NULL;
        }
        if (dc_config_set(c, i, g_buf[i], (int32_t)g_len[i]) != DC_OK) {
            dc_config_destroy(c);
            return NULL;
        }
    }
    if (dc_config_load(c) != DC_OK) {
        printf("  설정 로드 실패 [%s] %s\n",
               dc_config_file_name(dc_config_error_file(c)), dc_config_error(c));
        dc_config_destroy(c);
        return NULL;
    }
    return c;
}

static int g_pass = 0;
static int g_fail = 0;

static void check(int cond, const char* what) {
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        printf("  FAIL %s\n", what);
    }
}

static void section(const char* name) { printf("-- %s\n", name); }

int main(void) {
    section("버전 · 상수");
    check(dc_abi_version() == DC_ABI_VERSION, "dc_abi_version");
    /* Fixed 20.12 — 프레젠테이션이 raw를 나눌 값이다 */
    check(dc_fixed_one() == 4096, "dc_fixed_one == 4096");

    section("설정 — 호스트가 읽어 넘긴다");
    DcConfig* cfg = makeConfig();
    check(cfg != NULL, "설정 로드");
    if (cfg == NULL) { printf("설정을 못 만들어 중단\n"); return 1; }
    check(dc_config_data_hash(cfg) != 0, "dataHash가 계산됐다");
    check(dc_config_file_count() > 0, "파일 수 > 0");
    check(dc_config_file_name(-1) == NULL, "범위 밖 인덱스는 NULL");
    check(dc_config_file_name(dc_config_file_count()) == NULL, "상한도 NULL");
    /* 설정 오류 경로 — 버퍼를 안 물린 핸들은 로드가 실패해야 한다 */
    {
        DcConfig* empty = dc_config_create();
        check(empty != NULL, "빈 설정 핸들");
        check(dc_config_load(empty) != DC_OK, "버퍼 없이 로드하면 실패");
        check(dc_config_error(empty) != NULL, "이유를 말한다");
        check(dc_world_create_with(empty, 1ull) == NULL, "로드 실패한 설정으로는 월드를 못 만든다");
        dc_config_destroy(empty);
        dc_config_destroy(NULL);   /* NULL 해제 안전 */
    }

    section("NULL 인자가 크래시가 아니라 오류 코드로 온다");
    check(dc_world_create_with(NULL, 1ull) == NULL, "dc_world_create_with(NULL)");
    check(dc_config_set(NULL, 0, "x", 1) == DC_ERR_NULL, "dc_config_set(NULL)");
    check(dc_config_load(NULL) == DC_ERR_NULL, "dc_config_load(NULL)");
    check(dc_step(NULL, 1) == DC_ERR_NULL, "dc_step(NULL)");
    check(dc_input(NULL, DC_INPUT_CRAFT, 0) == DC_ERR_NULL, "dc_input(NULL)");
    check(dc_entity_count(NULL) == DC_ERR_NULL, "dc_entity_count(NULL)");
    check(dc_entity_pos_x(NULL) == NULL, "dc_entity_pos_x(NULL)");
    check(dc_checksums(NULL, NULL) == DC_ERR_NULL, "dc_checksums(NULL)");
    /* 해제는 NULL에도 안전해야 한다 — C# 쪽 Dispose가 두 번 불릴 수 있다 */
    dc_world_destroy(NULL);
    check(1, "dc_world_destroy(NULL) 안전");

    section("핸들 왕복");
    DcWorld* w = dc_world_create_with(cfg, 20250918ull);
    check(w != NULL, "dc_world_create_with");
    if (w == NULL) { printf("핸들을 못 만들어 중단\n"); return 1; }

    check(dc_run_tick(w) == 0, "시작 틱 0");
    check(dc_run_outcome(w) == DC_RUN_RUNNING, "시작은 Running");
    check(dc_hero_corruption_max(w) > 0, "잠식 최대치 > 0");

    section("범위 밖 인자");
    check(dc_step(w, -1) == DC_ERR_ARG, "음수 틱");
    check(dc_input(w, 0, 0) == DC_ERR_ARG, "kind=None 거부");
    check(dc_input(w, 99, 0) == DC_ERR_ARG, "kind 범위 밖 거부");

    section("진행 · 상태 포인터");
    check(dc_step(w, 200) == DC_OK, "200틱 전진");
    check(dc_run_tick(w) == 200, "틱 카운터 200");

    const int32_t n = dc_entity_count(w);
    check(n > 0, "엔티티가 스폰됐다");
    const int32_t* px = dc_entity_pos_x(w);
    const int32_t* py = dc_entity_pos_y(w);
    const uint8_t* ar = dc_entity_archetype(w);
    check(px != NULL && py != NULL && ar != NULL, "상태 포인터가 살아 있다");

    /* 포인터가 실제로 배열이어야 한다 — 읽어서 하나라도 원점이 아니면 성공.
       (스폰은 영웅 주위 반경에서 일어나므로 전부 0일 수 없다) */
    int nonzero = 0;
    for (int32_t i = 0; i < n; ++i) {
        if (px[i] != 0 || py[i] != 0) { nonzero = 1; break; }
    }
    check(nonzero, "좌표 배열이 실제 값을 담고 있다");

    section("체크섬 결정론 — 같은 시드 두 번");
    DcChecksums a, b;
    memset(&a, 0, sizeof a);
    memset(&b, 0, sizeof b);
    check(dc_checksums(w, &a) == DC_OK, "dc_checksums");
    check(a.total != 0, "체크섬이 0이 아니다");

    DcWorld* w2 = dc_world_create_with(cfg, 20250918ull);
    check(w2 != NULL, "두 번째 핸들");
    check(dc_step(w2, 200) == DC_OK, "두 번째 200틱");
    check(dc_checksums(w2, &b) == DC_OK, "두 번째 체크섬");
    check(a.total == b.total, "같은 시드 → 같은 총 체크섬");
    check(a.entities == b.entities && a.hero == b.hero && a.run == b.run
              && a.spawn == b.spawn && a.rng == b.rng,
          "부분 체크섬도 전부 일치");

    section("다른 시드는 다른 결과");
    DcWorld* w3 = dc_world_create_with(cfg, 1ull);
    DcChecksums c;
    memset(&c, 0, sizeof c);
    check(w3 != NULL, "세 번째 핸들");
    check(dc_step(w3, 200) == DC_OK, "세 번째 200틱");
    check(dc_checksums(w3, &c) == DC_OK, "세 번째 체크섬");
    check(c.total != a.total, "시드가 다르면 체크섬이 다르다");

    section("서버 진입점이 클라이언트 경로와 같은 결과를 낸다");
    /* **이 프로젝트의 핵심 계약이다.** 서버가 리플레이를 재실행해 얻는 체크섬이
       클라이언트가 같은 입력으로 얻는 것과 달라지면, 검증이 정직한 플레이어를
       탈락시킨다. 두 표면이 같은 코어를 쓴다는 것을 여기서 못 박는다. */
    DcChecksums h;
    memset(&h, 0, sizeof h);
    check(dc_headless_checksum(cfg, 20250918ull, 200, NULL, 0, &h) == DC_OK,
          "dc_headless_checksum");
    check(h.total == a.total, "헤드리스 == 클라이언트 경로 (총합)");
    check(h.entities == a.entities && h.hero == a.hero && h.run == a.run
              && h.spawn == a.spawn && h.rng == a.rng,
          "헤드리스 == 클라이언트 경로 (부분 전부)");

    section("헤드리스 인자 검증");
    check(dc_headless_checksum(cfg, 1ull, 10, NULL, 0, NULL) == DC_ERR_NULL, "out=NULL");
    check(dc_headless_checksum(cfg, 1ull, -1, NULL, 0, &h) == DC_ERR_ARG, "음수 틱");
    check(dc_headless_checksum(cfg, 1ull, 10, NULL, 3, &h) == DC_ERR_NULL,
          "log=NULL인데 len>0");
    {
        /* 로그는 (tick, kind, value) 3튜플이어야 한다 */
        const int32_t bad[2] = {0, DC_INPUT_CRAFT};
        check(dc_headless_checksum(cfg, 1ull, 10, bad, 2, &h) == DC_ERR_ARG,
              "len이 3의 배수가 아니다");
    }

    check(dc_headless_checksum(NULL, 1ull, 10, NULL, 0, &h) == DC_ERR_NULL, "cfg=NULL");

    dc_world_destroy(w);
    dc_world_destroy(w2);
    dc_world_destroy(w3);
    dc_config_destroy(cfg);
    {
        int i;
        for (i = 0; i < dc_config_file_count(); ++i) free(g_buf[i]);
    }

    printf("test_abi: %d 통과, %d 실패\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
