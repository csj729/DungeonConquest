// C ABI 원시 선언 — `core/abi/dc_abi.h`를 그대로 옮긴 것.
//
// **이 파일은 손으로 맞추지 않는다.** `tools/verify_abi_bindings.py`가 헤더와
// 이 파일을 대조하고 CI가 그걸 돌린다. 헤더만 고치고 여기를 잊으면 P/Invoke가
// 스택을 망가뜨리는데, 증상이 "가끔 이상한 값"이라 찾는 데 며칠이 걸린다.
//
// ## 규칙
//
//   1. **여기서 편의를 부리지 않는다.** 얇은 1:1 선언만 둔다 — 핸들 수명·문자열
//      변환·배열 뷰는 `SimWorld`가 맡는다. 두 가지를 한 파일에서 하면 대조가
//      불가능해진다
//   2. **`const char*`를 `string`으로 받지 않는다.** 기본 마샬러가 반환된
//      포인터를 CoTaskMemFree로 해제하려 드는데, 이 포인터들은 코어 내부의
//      정적 메모리라 즉시 힙이 깨진다. IntPtr로 받아 직접 변환한다
//   3. 포인터 인자는 전부 IntPtr다. 배열 뷰는 `SimWorld`가 Span으로 감싼다
using System;
using System.Runtime.InteropServices;

namespace DungeonConquest.Sim
{
    /// <summary>ABI 상태 코드. dc_abi.h의 DcStatus와 값이 같아야 한다.</summary>
    public enum DcStatus
    {
        Ok        =  0,
        ErrNull   = -1,
        ErrArg    = -2,
        ErrVersion = -3,
        ErrAlloc  = -4,
        ErrInput  = -5,
        ErrInternal = -6,
    }

    /// <summary>런 결과. dc_abi.h의 DcOutcome.</summary>
    public enum DcOutcome
    {
        Running = 0,
        Cleared = 1,
        Dead    = 2,
    }

    /// <summary>입력 종류. dc_abi.h의 DcInputKind.</summary>
    public enum DcInputKind
    {
        None         = 0,
        ManualTarget = 1,
        QteGrade     = 2,
        CardChoice   = 3,
        Craft        = 4,
    }

    /// <summary>
    /// 시스템별 부분 체크섬. **필드 순서가 dc_abi.h와 같아야 한다** —
    /// 구조체로 그대로 넘어오므로 순서가 어긋나면 값이 섞인다.
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct DcChecksums
    {
        public ulong Total;
        public ulong Entities;
        public ulong Hero;
        public ulong Run;
        public ulong Spawn;
        public ulong Rng;
    }

    /// <summary>
    /// `libdc_abi`의 원시 진입점. 직접 부르지 말고 <see cref="SimWorld"/>를 쓴다.
    /// </summary>
    public static class DcNative
    {
        /// <summary>
        /// 네이티브 라이브러리 이름. Unity는 Plugins 폴더에서, .NET은 실행 파일
        /// 옆에서 플랫폼별 확장자(.so/.dll/.dylib)를 붙여 찾는다.
        /// </summary>
        public const string Lib = "dc_abi";

        /// <summary>이 바인딩이 기대하는 ABI 버전. dc_abi.h의 DC_ABI_VERSION.</summary>
        public const int AbiVersion = 3;

        // ── 버전 ──
        [DllImport(Lib)] public static extern int dc_abi_version();
        [DllImport(Lib)] public static extern int dc_fixed_one();

        // ── 설정 ──
        [DllImport(Lib)] public static extern int dc_config_file_count();
        [DllImport(Lib)] public static extern IntPtr dc_config_file_name(int index);
        [DllImport(Lib)] public static extern IntPtr dc_config_create();
        [DllImport(Lib)] public static extern void dc_config_destroy(IntPtr c);
        [DllImport(Lib)] public static extern int dc_config_set(IntPtr c, int index, IntPtr text, int len);
        [DllImport(Lib)] public static extern int dc_config_load(IntPtr c);
        [DllImport(Lib)] public static extern IntPtr dc_config_error(IntPtr c);
        [DllImport(Lib)] public static extern IntPtr dc_config_error_key(IntPtr c);
        [DllImport(Lib)] public static extern int dc_config_error_file(IntPtr c);
        [DllImport(Lib)] public static extern ulong dc_config_data_hash(IntPtr c);

        // ── 수명 ──
        [DllImport(Lib)] public static extern IntPtr dc_world_create_with(IntPtr cfg, ulong seed);
        [DllImport(Lib)] public static extern void dc_world_destroy(IntPtr w);

        // ── 진행 ──
        [DllImport(Lib)] public static extern int dc_step(IntPtr w, int ticks);
        [DllImport(Lib)] public static extern int dc_input(IntPtr w, int kind, uint value);

        // ── 상태 읽기 ──
        [DllImport(Lib)] public static extern int dc_entity_count(IntPtr w);
        [DllImport(Lib)] public static extern IntPtr dc_entity_pos_x(IntPtr w);
        [DllImport(Lib)] public static extern IntPtr dc_entity_pos_y(IntPtr w);
        [DllImport(Lib)] public static extern IntPtr dc_entity_archetype(IntPtr w);
        [DllImport(Lib)] public static extern IntPtr dc_entity_prev_x(IntPtr w);
        [DllImport(Lib)] public static extern IntPtr dc_entity_prev_y(IntPtr w);
        [DllImport(Lib)] public static extern IntPtr dc_entity_id(IntPtr w);

        [DllImport(Lib)] public static extern int dc_hero_pos_x(IntPtr w);
        [DllImport(Lib)] public static extern int dc_hero_pos_y(IntPtr w);
        [DllImport(Lib)] public static extern int dc_hero_corruption(IntPtr w);
        [DllImport(Lib)] public static extern int dc_hero_corruption_max(IntPtr w);
        [DllImport(Lib)] public static extern int dc_hero_level(IntPtr w);

        [DllImport(Lib)] public static extern int dc_run_tick(IntPtr w);
        [DllImport(Lib)] public static extern int dc_run_segment(IntPtr w);
        [DllImport(Lib)] public static extern int dc_run_clear_points(IntPtr w);
        [DllImport(Lib)] public static extern int dc_run_outcome(IntPtr w);

        // ── 검증 ──
        [DllImport(Lib)] public static extern int dc_checksums(IntPtr w, out DcChecksums outChecksums);
        [DllImport(Lib)] public static extern int dc_headless_checksum(IntPtr cfg, ulong seed, int ticks,
                                                                       IntPtr log, int len,
                                                                       out DcChecksums outChecksums);

        /// <summary>코어가 돌려준 정적 문자열을 읽는다. 해제하지 않는다(규칙 2).</summary>
        public static string Utf8(IntPtr p) => p == IntPtr.Zero ? null : Marshal.PtrToStringUTF8(p);
    }
}
