// C# 레이어 하네스 — **Unity 없이 레이어가 도는지 확인한다.**
//
// 저장소에 C# 컴파일러가 없어서 `client/Runtime/`은 **여기서 컴파일 검증되지 않는다.**
// `verify_abi_bindings.py`는 선언만 대조한다. 이 하네스가 그 구멍을 메운다 — 실제
// 네이티브 라이브러리와 실제 `data/*.json`으로 런을 끝까지 돌리고 불변을 단정한다.
//
//     cmake --build build/release --target dc_abi
//     dotnet run --project client/Harness -- data
//
// Unity를 열기 전에 이것이 통과해야 한다. 통과하면 "네이티브 로드 · 데이터 전달 ·
// 틱 소비 · 이벤트 드레인 · 자동 일시정지"가 전부 살아 있다는 뜻이다.
using System;
using System.IO;
using DungeonConquest.Sim;

internal static class Program
{
    private static int _checks;
    private static int _fails;

    private static void Check(bool ok, string what)
    {
        ++_checks;
        if (ok) { Console.WriteLine($"  OK   {what}"); return; }
        ++_fails;
        Console.WriteLine($"  FAIL {what}");
    }

    private static int Main(string[] args)
    {
        string dataDir = args.Length > 0 ? args[0] : "data";
        Console.WriteLine($"=== C# 레이어 하네스 (데이터 {dataDir}) ===");

        Check(DcNative.dc_abi_version() == DcNative.AbiVersion,
              $"ABI 버전 — 라이브러리 {DcNative.dc_abi_version()} · 바인딩 {DcNative.AbiVersion}");

        // ── 데이터 전달 ──
        // **이름과 순서를 코어가 정한다** — 클라이언트가 파일 목록을 적어 두면
        // 데이터가 늘 때 한쪽만 고치는 사고가 난다.
        string[] names = SimConfig.FileNames();
        Console.WriteLine($"  코어가 요구하는 파일 {names.Length}개: {string.Join(", ", names)}");
        var blobs = new byte[names.Length][];
        for (int i = 0; i < names.Length; ++i)
        {
            string path = Path.Combine(dataDir, names[i] + ".json");
            if (!File.Exists(path))
            {
                Console.WriteLine($"  FAIL 데이터 파일이 없다: {path}");
                return 1;
            }
            blobs[i] = File.ReadAllBytes(path);
        }

        using var config = new SimConfig(blobs);
        Check(config.DataHash != 0, $"설정 지문 0x{config.DataHash:x16}");

        using var world = new SimWorld(config, 20250918UL);
        DcViewConfig view = world.ViewConfig;
        Check(view.TickHz == TickDriver.TickHz,
              $"틱레이트 — 코어 {view.TickHz} · 드라이버 {TickDriver.TickHz}");
        Check(view.QtePerfectWindowTicks > 0,
              $"완벽 구간 {view.QtePerfectWindowTicks}틱 (프레젠테이션이 판정에 쓴다)");

        var driver = new TickDriver(world);
        var loop = new RunLoop(world, driver);

        // ── 런을 끝까지 돌린다 ──
        //
        // 프레임을 흉내 낸다: 60fps에 16.67ms. 카드가 뜨면 **드라이버가 0배속으로
        // 내려가 더 소비하지 않아야 한다** — 그걸 직접 확인한다.
        int frames = 0, ticks = 0, offers = 0, pausedFrames = 0;
        int hits = 0, kills = 0, levelUps = 0, qteOpens = 0;
        bool sawPauseHold = false;
        const float dt = 1.0f / 60.0f;

        while (!world.IsOver && frames < 60 * 60 * 15)   // 최대 15분
        {
            ++frames;
            int consumed = loop.Advance(dt);
            ticks += consumed;

            foreach (DcEvent e in loop.LastEvents)
            {
                switch ((DcEventKind)e.Kind)
                {
                    case DcEventKind.Hit:       ++hits; break;
                    case DcEventKind.Kill:      ++kills; break;
                    case DcEventKind.LevelUp:   ++levelUps; break;
                    case DcEventKind.QteOpen:   ++qteOpens; break;
                }
            }

            if (loop.PendingChoiceCount > 0)
            {
                // **한 프레임은 선택하지 않고 넘겨 본다** — 그 프레임에 틱이
                // 0이어야 자동 일시정지가 실제로 거는 것이다.
                if (!sawPauseHold)
                {
                    sawPauseHold = true;
                    ++frames;
                    int held = loop.Advance(dt);
                    Check(held == 0, "카드 제시 중에는 틱이 소비되지 않는다");
                    Check(driver.IsPaused, "자동 일시정지가 걸린다 (EffectiveSpeed 0)");
                    ++pausedFrames;
                }
                ++offers;
                bool took = loop.SubmitCardChoice(0);
                if (!took)
                {
                    // **여기서 빠져나온다** — 0배속이라 더 돌려도 틱이 안 나가고
                    // 프레임 상한까지 헛돈다. 실패를 그 자리에서 보이게 한다.
                    Check(false, "카드 선택이 받아들여진다");
                    break;
                }
            }

            // QTE는 **프레젠테이션이 등급을 판정한다** (§3). 여기서는 항상 완벽으로
            // 민다 — 완벽 구간이 창의 끝 N틱이라는 규약을 그대로 쓴다.
            if (loop.InQteWindow && world.Tick >= loop.QteCloseTick - view.QtePerfectWindowTicks)
            {
                loop.SubmitQte(2);   // QteGrade::Perfect — core/include/dc/qte.h
            }
        }

        Console.WriteLine($"  런 종료: {world.Outcome} · {world.Tick}틱 · 프레임 {frames}");
        Console.WriteLine($"  이벤트: 타격 {hits} · 처치 {kills} · 레벨업 {levelUps} · QTE창 {qteOpens}");
        Console.WriteLine($"  카드 선택 {offers}회 · 일시정지 확인 {pausedFrames}회 · 버린 이벤트 {loop.DroppedTotal}");

        Check(world.IsOver, "런이 끝났다 (타임아웃이 아니다)");
        Check(ticks > 0 && ticks == world.Tick, $"소비한 틱과 월드 틱이 같다 ({ticks} · {world.Tick})");
        Check(hits > 0, "타격 이벤트가 나온다");
        Check(kills > 0, "처치 이벤트가 나온다");
        Check(levelUps > 0, "레벨업 이벤트가 나온다");
        Check(offers > 0, "카드 제시가 나온다");
        Check(sawPauseHold, "자동 일시정지를 실제로 밟았다");
        Check(loop.DroppedTotal == 0, "이벤트 큐가 넘치지 않았다");

        // ── 보간 읽기가 도는가 ──
        var xs = new float[world.EntityCount + 1];
        var ys = new float[xs.Length];
        world.ReadPositions(driver.Alpha, xs, ys);
        // **단정하지 않는다** — 던지지 않았다는 것 자체가 확인이고, 참인 것을
        // Check로 적으면 통과 수만 부풀어 지표가 흐려진다.
        Console.WriteLine($"  보간 읽기 {world.EntityCount}기 · alpha {driver.Alpha:F2}");

        Console.WriteLine($"\n{_checks - _fails}/{_checks} 통과");
        return _fails == 0 ? 0 : 1;
    }
}
