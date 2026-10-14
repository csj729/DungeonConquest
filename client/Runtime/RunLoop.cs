// 런 루프 — **SimWorld · TickDriver · EventQueue를 잇는 자리.**
//
// 세 조각은 이미 있었는데 **아무도 잇지 않았다.** `TickDriver`에 `SetAutoPause`가
// 있고 `EventQueue`가 `CardOffer`를 내보내는데, 그 둘을 연결하는 코드가 없어서
// design.md §10이 약속한 자동 일시정지가 성립하지 않았다. 이 파일이 그 정책이다.
//
// ## 왜 한 틱씩 소비하는가 — 이벤트 큐의 설계와 얽힌 자리
//
// `dc_events_drain`은 드레인할 때까지 쌓는다(틱마다 플립하면 앞의 틱 이벤트가
// 사라지므로). 그래서 "프레임당 한 번 드레인"이 가능하다 — **그런데 그러면 안 된다.**
//
// 카드 제시는 "다음 틱 입력에 플레이어 선택이 필요한" 이벤트다(§10). 한 프레임에 8틱을
// 소비하고 나서 드레인하면, 3번째 틱에 뜬 제시를 보기 전에 4~8틱이 이미 지나간다.
// 시뮬은 선택을 기다려 주지 않는다 — `dealCards`가 제시를 열어 두기만 하고 틱은 계속
// 돈다. **멈추는 것은 드라이버의 책임**이고, 그 책임을 지려면 틱마다 봐야 한다.
//
// 비용은 틱당 P/Invoke 한 번이다. 프레임당 상한이 8틱이므로 무시할 수 있다.
//
// ## 결정론에 영향이 없다
//
// 여기서 하는 일은 **몇 틱을 소비할지**와 **언제 입력을 넣을지**뿐이다. 입력은
// `(틱 번호, 값)`으로 기록되므로 서버가 그대로 재현한다. 배속·슬로모·일시정지는
// 상태가 아니다 (§10).
using System;

namespace DungeonConquest.Sim
{
    /// <summary>
    /// 한 프레임에 할 일을 묶은 것. 프레젠테이션은 <see cref="Advance"/>를 부르고
    /// <see cref="LastEvents"/>를 읽어 연출하면 된다.
    /// </summary>
    public sealed class RunLoop
    {
        /// <summary>
        /// QTE 창에서 낮출 배속. **프레젠테이션 손잡이다** — 시뮬 틱 자체는 항상
        /// 고정 속도이므로 결정론에 영향이 없다 (design.md §3).
        ///
        /// <para>문서가 값을 정하지 않았다. 4배속에서도 판정이 가능해야 한다는 요구만
        /// 있으므로, 실제 값은 플레이해 보고 정할 것.</para>
        /// </summary>
        public float QteSlowMoSpeed { get; set; } = 0.35f;

        private readonly SimWorld _world;
        private readonly TickDriver _driver;
        private readonly DcViewConfig _view;

        // 한 프레임에 모인 이벤트. **틱마다 드레인한 것을 여기 누적한다** —
        // 프레젠테이션은 프레임 단위로 보는 쪽이 자연스럽다.
        private DcEvent[] _frame = new DcEvent[64];
        private int _frameCount;
        private int _droppedTotal;

        // **델리게이트를 미리 만들어 둔다** — 프레임마다 람다를 새로 만들면 그만큼
        // GC 압력이 되고, Unity에서 그건 곧 프레임 끊김이다.
        private readonly Func<bool> _afterTick;

        public RunLoop(SimWorld world, TickDriver driver)
        {
            _world  = world  ?? throw new ArgumentNullException(nameof(world));
            _driver = driver ?? throw new ArgumentNullException(nameof(driver));
            _afterTick = AfterTick;
            _view = world.ViewConfig;
            if (_view.TickHz != TickDriver.TickHz)
            {
                // **드라이버가 다른 틱레이트로 소비하면 시뮬이 실시간과 어긋난다.**
                // 상수를 양쪽에 적어 둔 상태라 여기서 대조한다.
                throw new SimLoadException(
                    $"틱레이트 불일치: 코어 {_view.TickHz} · 드라이버 {TickDriver.TickHz}");
            }
        }

        /// <summary>프레젠테이션이 알아야 하는 설정 상수 (완벽 구간 · 그로기 길이 …).</summary>
        public DcViewConfig View => _view;

        /// <summary>이번 프레임에 모인 이벤트. 다음 <see cref="Advance"/>에서 덮인다.</summary>
        public ReadOnlySpan<DcEvent> LastEvents => new ReadOnlySpan<DcEvent>(_frame, 0, _frameCount);

        /// <summary>큐가 넘쳐 버린 누적 개수. 0이 아니면 로그에 남길 것.</summary>
        public int DroppedTotal => _droppedTotal;

        /// <summary>선택을 기다리는 카드 수. 0이면 기다리는 것이 없다.</summary>
        public int PendingChoiceCount { get; private set; }

        /// <summary>
        /// 열려 있는 QTE 창. **구간을 계산하지 말 것** — 시뮬이 완벽 주장을
        /// 강등하므로 한 틱만 어긋나도 "완벽을 눌렀는데 Success"가 된다.
        /// <c>Qte.IsPerfectAt(world.Tick)</c>을 쓴다 (design.md §3).
        /// </summary>
        public DcQteState Qte { get; private set; }

        /// <summary>QTE 창이 열려 있는가. 열려 있으면 배속이 낮아져 있다.</summary>
        public bool InQteWindow => Qte.Open;

        /// <summary>
        /// 프레임을 전진시킨다. 소비한 틱 수를 돌려준다.
        ///
        /// <para><b>틱마다 이벤트를 본다</b> — 카드 제시가 뜬 틱에서 멈추기 위해서다.
        /// 멈춘 뒤에는 선택이 들어올 때까지 더 소비하지 않는다.</para>
        /// </summary>
        public int Advance(float deltaSeconds)
        {
            _frameCount = 0;
            if (_world.IsOver) return 0;

            // **누산기는 드라이버 하나뿐이다.** 여기서 또 세면 두 곳이 어긋난다 —
            // 틱 사이 콜백으로 끼어들기만 한다.
            return _driver.Advance(deltaSeconds, _afterTick);
        }

        /// <summary>
        /// 카드 선택을 제출한다. 받아들여지면 자동 일시정지가 풀린다.
        /// 거부되면 false — 인덱스가 범위를 벗어났거나 제시가 닫혀 있다.
        /// </summary>
        public bool SubmitCardChoice(int index)
        {
            if (index < 0 || PendingChoiceCount <= 0) return false;
            if (!_world.Input(DcInputKind.CardChoice, (uint)index)) return false;
            PendingChoiceCount = 0;
            _driver.SetAutoPause(false);
            return true;
        }

        /// <summary>
        /// QTE 등급을 제출한다. **등급은 프레젠테이션이 판정한다** — 시뮬은
        /// `(틱 번호, 등급)`을 받아 기록할 뿐이다 (§3).
        /// </summary>
        public bool SubmitQte(int grade) => _world.Input(DcInputKind.QteGrade, (uint)grade);

        // 틱마다 불린다. **false면 이번 프레임의 소비를 여기서 멈춘다.**
        private bool AfterTick()
        {
            Collect();

            // **슬로모는 창 상태에서 끌어온다 — 이벤트를 세지 않는다.**
            // 전에는 QteOpen에서 켜고 QteResult에서 껐는데, 그러면 이벤트 하나를
            // 놓치거나 순서가 뒤집히면 배속이 눌린 채로 남는다. 상태를 그냥 읽으면
            // 그 실패 양식이 사라진다 — 매 틱 P/Invoke 한 번이 그 값이다.
            Qte = _world.QteState;
            _driver.SlowMoFactor = Qte.Open ? QteSlowMoSpeed : 1.0f;

            return PendingChoiceCount == 0;   // 선택을 기다리는 중이면 멈춘다
        }

        // 이번 틱의 이벤트를 프레임 버퍼에 모으고 정책을 적용한다.
        private void Collect()
        {
            ReadOnlySpan<DcEvent> evs = _world.DrainEvents(out int dropped);
            _droppedTotal += dropped;
            for (int i = 0; i < evs.Length; ++i)
            {
                Append(evs[i]);
                Apply(evs[i]);
            }
        }

        private void Append(DcEvent e)
        {
            if (_frameCount == _frame.Length)
            {
                // **프레임마다 새로 할당하지 않는다** — Unity에서 GC 압력이 곧 프레임
                // 끊김이다. 한 번 커지면 그 크기를 유지한다.
                var bigger = new DcEvent[_frame.Length * 2];
                Array.Copy(_frame, bigger, _frame.Length);
                _frame = bigger;
            }
            _frame[_frameCount++] = e;
        }

        private void Apply(DcEvent e)
        {
            switch ((DcEventKind)e.Kind)
            {
                case DcEventKind.CardOffer:
                    // **다음 틱 입력에 선택이 필요하다** → 0배속 (§10)
                    PendingChoiceCount = e.Value;
                    _driver.SetAutoPause(true);
                    break;

                // QteOpen · QteResult는 **연출 신호로만** 쓴다. 배속은 창 상태에서
                // 끌어오므로(AfterTick) 여기서 만지지 않는다.
            }
        }
    }
}
