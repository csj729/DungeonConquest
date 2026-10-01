// 시뮬 코어를 C#에서 쓰는 표면. `DcNative`의 원시 선언 위에 수명·뷰만 얹는다.
//
// **읽기만 한다** (CLAUDE.md 레이어 분리). 여기서 시뮬 상태를 바꾸는 길은
// `Input()` 하나뿐이고, 그건 코어가 틱 번호와 함께 받아 기록한다.
using System;
using System.Runtime.InteropServices;

namespace DungeonConquest.Sim
{
    /// <summary>
    /// `data/*.json`을 읽어 만든 설정. **코어는 파일을 모른다** — 호스트가 읽어
    /// 버퍼로 넘긴다(Unity는 TextAsset, 서버는 File.ReadAllBytes).
    ///
    /// 월드를 만들 때 설정이 복사되므로 <see cref="Dispose"/>를 먼저 해도 된다.
    /// </summary>
    public sealed class SimConfig : IDisposable
    {
        private IntPtr _handle;

        /// <summary>호스트가 읽어 와야 하는 파일 이름들. 순서가 곧 인덱스다.</summary>
        public static string[] FileNames()
        {
            int n = DcNative.dc_config_file_count();
            var names = new string[n];
            for (int i = 0; i < n; ++i) names[i] = DcNative.Utf8(DcNative.dc_config_file_name(i));
            return names;
        }

        /// <summary>
        /// 파일 내용(UTF-8 바이트)을 <see cref="FileNames"/> 순서대로 넘긴다.
        /// 실패하면 <see cref="SimLoadException"/> — **조용히 기본값으로 때우지 않는다.**
        /// </summary>
        public SimConfig(byte[][] files)
        {
            if (files == null) throw new ArgumentNullException(nameof(files));
            int want = DcNative.dc_config_file_count();
            if (files.Length != want)
            {
                throw new SimLoadException($"설정 파일이 {want}개여야 하는데 {files.Length}개다");
            }

            _handle = DcNative.dc_config_create();
            if (_handle == IntPtr.Zero) throw new SimLoadException("설정 핸들을 만들지 못했다");

            // **핀 고정이 필요하다.** 파서가 문자열을 복사하지 않으므로 load가
            // 끝날 때까지 GC가 버퍼를 옮기면 안 된다.
            var pins = new GCHandle[files.Length];
            try
            {
                for (int i = 0; i < files.Length; ++i)
                {
                    pins[i] = GCHandle.Alloc(files[i], GCHandleType.Pinned);
                    int rc = DcNative.dc_config_set(_handle, i, pins[i].AddrOfPinnedObject(),
                                                    files[i].Length);
                    if (rc != (int)DcStatus.Ok) throw new SimLoadException($"파일 {i} 전달 실패 ({rc})");
                }
                if (DcNative.dc_config_load(_handle) != (int)DcStatus.Ok)
                {
                    int fi = DcNative.dc_config_error_file(_handle);
                    string file = fi >= 0 ? DcNative.Utf8(DcNative.dc_config_file_name(fi)) : "?";
                    string key = DcNative.Utf8(DcNative.dc_config_error_key(_handle));
                    string why = DcNative.Utf8(DcNative.dc_config_error(_handle));
                    throw new SimLoadException(
                        key != null ? $"[{file}] {why} · 키 {key}" : $"[{file}] {why}");
                }
            }
            catch
            {
                Dispose();
                throw;
            }
            finally
            {
                for (int i = 0; i < pins.Length; ++i) if (pins[i].IsAllocated) pins[i].Free();
            }
        }

        internal IntPtr Handle => _handle;

        /// <summary>로드한 데이터의 지문. **서버와 이 값이 다르면 같은 판이 아니다.**</summary>
        public ulong DataHash => DcNative.dc_config_data_hash(_handle);

        public void Dispose()
        {
            if (_handle == IntPtr.Zero) return;
            DcNative.dc_config_destroy(_handle);
            _handle = IntPtr.Zero;
        }
    }

    /// <summary>설정 로드 실패. 어느 파일 어느 키인지 메시지에 담긴다.</summary>
    public sealed class SimLoadException : Exception
    {
        public SimLoadException(string message) : base(message) { }
    }

    /// <summary>
    /// 시뮬 월드 하나. **틱을 스스로 돌리지 않는다** — 호출 빈도는 드라이버의
    /// 책임이고, 그 결정은 시뮬 상태 밖에 있다 (design.md §10).
    /// </summary>
    public sealed unsafe class SimWorld : IDisposable
    {
        private IntPtr _handle;

        /// <summary>Fixed 20.12의 1.0 raw 값. 좌표를 타일 단위로 되돌릴 때 나눈다.</summary>
        public static readonly float FixedOne = DcNative.dc_fixed_one();

        public SimWorld(SimConfig config, ulong seed)
        {
            if (config == null) throw new ArgumentNullException(nameof(config));
            if (DcNative.dc_abi_version() != DcNative.AbiVersion)
            {
                // **버전이 다르면 바로 멈춘다.** 시그니처가 어긋난 채로 부르면
                // 스택이 망가지는데 증상이 "가끔 이상한 값"이라 찾기 어렵다.
                throw new SimLoadException(
                    $"ABI 버전 불일치: 라이브러리 {DcNative.dc_abi_version()} · 바인딩 {DcNative.AbiVersion}");
            }
            _handle = DcNative.dc_world_create_with(config.Handle, seed);
            if (_handle == IntPtr.Zero) throw new SimLoadException("월드를 만들지 못했다");
        }

        /// <summary>틱을 전진시킨다. 런이 끝났으면 그 자리에서 멈춘다.</summary>
        public void Step(int ticks)
        {
            int rc = DcNative.dc_step(_handle, ticks);
            if (rc != (int)DcStatus.Ok) throw new InvalidOperationException($"dc_step 실패 ({rc})");
        }

        /// <summary>입력을 **현재 틱에** 적용한다. 거부되면 false.</summary>
        public bool Input(DcInputKind kind, uint value)
            => DcNative.dc_input(_handle, (int)kind, value) == (int)DcStatus.Ok;

        public int Tick        => DcNative.dc_run_tick(_handle);
        public int Segment     => DcNative.dc_run_segment(_handle);
        public int ClearPoints => DcNative.dc_run_clear_points(_handle);
        public DcOutcome Outcome => (DcOutcome)DcNative.dc_run_outcome(_handle);
        public bool IsOver     => Outcome != DcOutcome.Running;

        public int HeroCorruption    => DcNative.dc_hero_corruption(_handle);
        public int HeroCorruptionMax => DcNative.dc_hero_corruption_max(_handle);
        public int HeroLevel         => DcNative.dc_hero_level(_handle);
        public float HeroX => DcNative.dc_hero_pos_x(_handle) / FixedOne;
        public float HeroY => DcNative.dc_hero_pos_y(_handle) / FixedOne;

        public int EntityCount => DcNative.dc_entity_count(_handle);

        // ── SoA 뷰 ──
        //
        // **복사하지 않는다.** 코어의 배열을 그대로 가리키므로, 다음 Step()까지만
        // 유효하다. 프레임 안에서 읽고 버리는 용도다 — 들고 있으면 안 된다.
        public ReadOnlySpan<int>  PosX      => Span(DcNative.dc_entity_pos_x(_handle));
        public ReadOnlySpan<int>  PosY      => Span(DcNative.dc_entity_pos_y(_handle));
        public ReadOnlySpan<int>  PrevX     => Span(DcNative.dc_entity_prev_x(_handle));
        public ReadOnlySpan<int>  PrevY     => Span(DcNative.dc_entity_prev_y(_handle));
        public ReadOnlySpan<uint> EntityIds => SpanU(DcNative.dc_entity_id(_handle));
        public ReadOnlySpan<byte> Archetypes
            => new ReadOnlySpan<byte>((void*)DcNative.dc_entity_archetype(_handle), EntityCount);

        private ReadOnlySpan<int> Span(IntPtr p)
            => p == IntPtr.Zero ? default : new ReadOnlySpan<int>((void*)p, EntityCount);
        private ReadOnlySpan<uint> SpanU(IntPtr p)
            => p == IntPtr.Zero ? default : new ReadOnlySpan<uint>((void*)p, EntityCount);

        /// <summary>
        /// 보간한 엔티티 위치. **prev는 코어가 현재 행에 맞춰 준다** — 틱 끝 압축이
        /// 행을 옮겨도 정렬이 유지되므로 짝짓기가 필요 없다 (ABI 3).
        /// </summary>
        public void ReadPositions(float alpha, float[] outX, float[] outY)
        {
            var px = PosX; var py = PosY;
            var qx = PrevX; var qy = PrevY;
            int n = Math.Min(px.Length, Math.Min(outX.Length, outY.Length));
            for (int i = 0; i < n; ++i)
            {
                outX[i] = (qx[i] + (px[i] - qx[i]) * alpha) / FixedOne;
                outY[i] = (qy[i] + (py[i] - qy[i]) * alpha) / FixedOne;
            }
        }

        public DcChecksums Checksums()
        {
            int rc = DcNative.dc_checksums(_handle, out DcChecksums c);
            if (rc != (int)DcStatus.Ok) throw new InvalidOperationException($"dc_checksums 실패 ({rc})");
            return c;
        }

        public void Dispose()
        {
            if (_handle == IntPtr.Zero) return;
            DcNative.dc_world_destroy(_handle);
            _handle = IntPtr.Zero;
        }
    }
}
