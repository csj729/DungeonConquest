// 틱 소비 제어 — 배속 · 일시정지 · 렌더 보간 알파 (design.md §10).
//
// **코어는 틱을 스스로 돌리지 않는다.** 실시간 1초에 몇 번 `Step()`을 부르느냐가
// 드라이버의 책임이고, 그 결정은 시뮬 상태 밖에 있다. 그래서 배속·일시정지는
// 결정론에 아무 영향이 없다 — 체크섬은 틱의 순서와 내용만 보고, 같은 틱이
// 실시간으로 몇 초 만에 소비됐는지는 상태가 아니다.
//
// 서버 헤드리스 러너에는 이 개념 자체가 없다. 실시간 페이싱 없이 입력 로그를
// 최대 속도로 재생하므로, **배속·일시정지는 오직 클라이언트 표시 문제다.**
//
// 엔진에 의존하지 않는다 — `Advance(dt)`에 프레임 시간을 넘기면 된다.
using System;

namespace DungeonConquest.Sim
{
    /// <summary>
    /// 고정 20Hz 시뮬을 가변 프레임 렌더에 맞춰 소비한다.
    ///
    /// 쓰는 법: 프레임마다 <see cref="Advance"/>에 델타 시간을 넘기고,
    /// <see cref="Alpha"/>로 보간한다.
    /// </summary>
    public sealed class TickDriver
    {
        /// <summary>시뮬 고정 틱레이트. 코어와 같아야 한다 (progression.json:tick_hz).</summary>
        public const int TickHz = 20;
        private const float TickSeconds = 1.0f / TickHz;

        /// <summary>
        /// 한 프레임에 소비할 틱 수의 상한. **없으면 죽음의 나선이 생긴다** —
        /// 프레임이 밀리면 더 많은 틱을 돌리고, 그래서 더 밀린다. 상한에 걸리면
        /// 시뮬이 실시간보다 느려지지만 그건 멈추는 것보다 낫다.
        /// </summary>
        public int MaxTicksPerFrame { get; set; } = 8;

        private readonly SimWorld _world;
        private float _accum;
        private float _speed = 1.0f;
        private float _speedBeforeAutoPause = 1.0f;
        private bool  _autoPaused;

        public TickDriver(SimWorld world)
        {
            _world = world ?? throw new ArgumentNullException(nameof(world));
        }

        /// <summary>
        /// 배속. 1x/2x/4x가 표준이고 **0x가 곧 일시정지다** — 새 메커니즘이
        /// 아니라 배속의 극단값이다 (§10).
        ///
        /// QTE 슬로모도 같은 손잡이다: 판정 창에서만 값을 낮추면 4배속 중에도
        /// 판정이 가능하다 (§3).
        /// </summary>
        public float Speed
        {
            get => _speed;
            set => _speed = value < 0f ? 0f : value;
        }

        public bool IsPaused => _speed <= 0f;

        /// <summary>마지막으로 소비한 틱 이후 진행도 0..1. 렌더 보간에 쓴다.</summary>
        public float Alpha => _speed <= 0f ? 0f : Clamp01(_accum / TickSeconds);

        /// <summary>이번 프레임에 실제로 소비한 틱 수. 밀림 진단용이다.</summary>
        public int LastConsumed { get; private set; }

        /// <summary>상한에 걸려 버린 시간이 있었는가. 계속 참이면 시뮬이 뒤처진다.</summary>
        public bool LastFrameThrottled { get; private set; }

        /// <summary>
        /// **자동 일시정지** — 레벨업 카드 선택처럼 다음 틱 입력에 선택이 필요한
        /// 이벤트가 걸리면 드라이버가 0x로 내린다. 선택이 제출되면 이전 배속으로
        /// 돌아간다 (§10). 플레이어가 누른 수동 일시정지와 섞이지 않도록
        /// 직전 배속을 따로 기억한다.
        /// </summary>
        public void SetAutoPause(bool paused)
        {
            if (paused == _autoPaused) return;
            _autoPaused = paused;
            if (paused)
            {
                _speedBeforeAutoPause = _speed;
                _speed = 0f;
            }
            else
            {
                _speed = _speedBeforeAutoPause;
            }
        }

        public bool IsAutoPaused => _autoPaused;

        /// <summary>
        /// 프레임 델타(초)만큼 시간을 흘리고 필요한 만큼 틱을 소비한다.
        /// 소비한 틱 수를 돌려준다.
        /// </summary>
        public int Advance(float deltaSeconds)
        {
            LastConsumed = 0;
            LastFrameThrottled = false;
            if (deltaSeconds <= 0f || _speed <= 0f) return 0;

            // 런이 끝났으면 더 소비하지 않는다 — 결과를 읽기 전에 상태가 더
            // 움직이면 "어느 틱에 끝났는가"가 흐려진다.
            if (_world.IsOver) return 0;

            _accum += deltaSeconds * _speed;

            int consumed = 0;
            while (_accum >= TickSeconds)
            {
                if (consumed >= MaxTicksPerFrame)
                {
                    // 남은 시간을 버린다. **누적해 두면 다음 프레임이 더 밀린다.**
                    _accum = 0f;
                    LastFrameThrottled = true;
                    break;
                }
                _accum -= TickSeconds;
                _world.Step(1);
                ++consumed;
                if (_world.IsOver) { _accum = 0f; break; }
            }
            LastConsumed = consumed;
            return consumed;
        }

        private static float Clamp01(float v) => v < 0f ? 0f : (v > 1f ? 1f : v);
    }
}
