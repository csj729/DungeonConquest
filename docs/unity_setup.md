# Unity 클라이언트 셋업

이 문서는 **Unity를 열었을 때 붙이기만 하면 되는 상태**가 무엇인지, 그리고 무엇이
아직 남아 있는지를 적는다.

## 경계 — 무엇이 엔진 비의존인가

```
client/Runtime/        엔진 비의존. Unity 밖에서도 컴파일된다
  DcNative.cs          C ABI 원시 선언 (얇은 1:1)
  SimWorld.cs          핸들 수명 · 상태 버퍼 Span · 이벤트 드레인
  TickDriver.cs        틱 소비 · 배속 · 슬로모 인자 · 보간 알파
  RunLoop.cs           위 셋을 잇는 정책 (자동 일시정지 · 슬로모 · 선택 제출)

client/Harness/        dotnet 콘솔. **C# 레이어의 유일한 실행 검증**
Assets/ (당신이 만든다) MonoBehaviour · URP · 스프라이트 — Unity에 의존하는 전부
```

**`client/Runtime/`에 `UnityEngine`을 쓰지 말 것.** `verify_abi_bindings.py`가 검사하고
CI가 그걸 돌린다. 이유는 CLAUDE.md의 레이어 분리를 C#에 적용한 것이고, 실무적으로는
하네스가 깨지면 **C# 레이어의 검증이 사라지기 때문**이다.

## 먼저 이것이 통과해야 한다

```bash
cmake --build build/release --target dc_abi
dotnet run --project client/Harness -- data
```

통과하면 네이티브 로드 · 데이터 전달 · 틱 소비 · 이벤트 드레인 · 자동 일시정지가
전부 살아 있다는 뜻이다. **Unity를 열기 전에 여기서 확인한다** — 에디터 안에서
P/Invoke를 디버깅하는 것보다 훨씬 짧은 루프다.

> **왜 하네스가 따로 있나**: 이 저장소에 C# 컴파일러가 없다. `verify_abi_bindings.py`는
> P/Invoke **선언**을 헤더와 대조할 뿐이고, `RunLoop`의 정책이 실제로 도는지는 돌려봐야
> 안다. 하네스가 그 구멍을 메운다.

## Unity 프로젝트 쪽

### 1. 네이티브 플러그인

| 플랫폼 | CMake 출력 | 놓을 곳 |
|---|---|---|
| Linux | `build/release/core/libdc_abi.so` | `Assets/Plugins/x86_64/` |
| Windows | `build/release/core/dc_abi.dll` | `Assets/Plugins/x86_64/` |
| macOS | `build/release/core/libdc_abi.dylib` | `Assets/Plugins/` |

`DcNative.Lib`이 `"dc_abi"`이므로 파일명은 플랫폼 관례를 그대로 두면 된다.

> **에디터에서 네이티브 DLL은 언로드되지 않는다.** `dc_abi.h`가 적어 둔 그대로다 —
> C++를 다시 빌드하면 **에디터를 재시작해야** 새 바이너리가 물린다. 문서는 개발 중에
> 수동 `LoadLibrary`/`FreeLibrary` + 델리게이트 래퍼를 쓰라고 하는데, **지금은
> `[DllImport]` 한 경로만 둔다**: 바인딩 경로를 둘로 두면 둘이 어긋날 수 있고, 어긋남을
> 잡아 주는 가드(`verify_abi_bindings`)는 `[DllImport]` 형태를 읽는다. 코어가 안정된
> 지금은 재시작 비용이 더 싸다 — 코어를 자주 고치게 되면 그때 래퍼를 넣는다.

### 2. 데이터 파일

시뮬은 `data/*.json`을 **호스트가 읽어서 넘겨 준다.** 경로를 코어가 모르는 것이 의도다
(서버도 같은 코어를 쓴다).

```csharp
string[] names = SimConfig.FileNames();            // 이름과 순서를 코어가 정한다
var blobs = new byte[names.Length][];
for (int i = 0; i < names.Length; ++i)
    blobs[i] = LoadBytes($"{names[i]}.json");      // StreamingAssets 또는 TextAsset
using var config = new SimConfig(blobs);
```

**파일 목록을 클라이언트에 적어 두지 말 것** — 데이터가 늘 때 한쪽만 고치는 사고가 난다.
`StreamingAssets/data/`에 그대로 복사하거나 `TextAsset`으로 임포트하면 된다.

### 3. 한 프레임

```csharp
void Update()
{
    int consumed = _loop.Advance(Time.deltaTime);

    foreach (DcEvent e in _loop.LastEvents)
        Present(e);                                 // 타격·처치·구슬 연출

    _world.ReadPositions(_driver.Alpha, _xs, _ys);   // prev/curr 보간
    DrawSprites(_xs, _ys, _world.Archetypes);

    if (_loop.PendingChoiceCount > 0)
        ShowCardUI(_loop.PendingChoiceCount);        // 드라이버는 이미 0배속이다
}
```

- **이벤트는 프레임마다 드레인된다** — `RunLoop`이 틱 사이에 가져와 모아 준다
- **죽은 적의 연출 위치는 이벤트가 들고 있다.** 엔티티 행은 틱 끝에 압축되어 사라지므로
  상태 버퍼로는 "어디서 죽었는지"를 알 수 없다
- `Archetypes`/`PosX` 등은 코어가 소유한 메모리의 `Span`이다 — **복사하지 않는다.**
  다음 `Step`에서 내용이 바뀌므로 프레임을 넘겨 들고 있지 말 것

### 4. QTE — 판정은 프레젠테이션이 한다

`design.md §3`이 정했다: 시뮬은 50ms보다 정밀한 타이밍을 모르므로 **등급은 UI가 매기고**
시뮬은 `(틱 번호, 등급)`을 받는다.

```csharp
if (_loop.InQteWindow)
{
    int perfectFrom = _loop.QteCloseTick - _loop.View.QtePerfectWindowTicks;
    ShowQteGauge(_world.Tick, perfectFrom, _loop.QteCloseTick);
    if (playerPressed)
        _loop.SubmitQte(_world.Tick >= perfectFrom ? 2 : 1);   // Perfect : Success
}
```

**완벽 구간을 C#에 적어 두지 말 것.** `_loop.View`가 코어에서 읽어 온다 — 그 수치를
클라이언트 상수로 두면 `hero.json`과 두 곳에 산다(CLAUDE.md).

### 5. 배속 · 일시정지

| 하고 싶은 것 | 손잡이 |
|---|---|
| 플레이어 배속 1x/2x/4x | `driver.Speed = 2f` |
| 수동 일시정지 | `driver.Speed = 0f` |
| QTE 슬로모 · 히트스톱 | `driver.SlowMoFactor` (RunLoop이 QTE에서 자동으로 건다) |
| 카드 선택 대기 | `RunLoop`이 자동 — 직접 만지지 말 것 |

**`Speed`와 `SlowMoFactor`를 분리해 둔 이유**: 전에는 슬로모가 `Speed`를 덮었고 자동
일시정지가 "직전 배속"을 저장했다. QTE 창에서 카드가 뜨면 저장된 값이 슬로모 값이라
선택 뒤에 4배속이 0.35배속으로 돌아왔다. 곱셈 인자로 두면 저장·복원이 필요 없어 그
버그 종류가 사라진다.

### 6. asmdef · 빌드 설정

- `client/Runtime/`을 `Assets/Sim/`으로 심볼릭 링크하거나 복사한다. asmdef를 하나 두고
  **참조를 비워 둔다**(UnityEngine 참조 없이 컴파일돼야 한다 — 가드가 그걸 검사한다)
- `SimWorld`가 `unsafe`다 → asmdef의 **Allow unsafe code**를 켠다
- IL2CPP에서 `[DllImport]`는 그대로 동작한다. 다만 **스트립 레벨**이 높으면 P/Invoke가
  날아갈 수 있으므로 `link.xml`에 `DungeonConquest.Sim`을 보존으로 둔다

## 아직 없는 것 — 일부러 비워 둔 것 포함

| | 왜 |
|---|---|
| Unity 프로젝트 자체 (`Assets/`, 씬, URP 설정) | 이 컨테이너에 Unity가 없다. 당신 머신 몫이다 |
| `FrameView` 같은 뷰모델 구조체 | `SimWorld`의 `ReadPositions`·`Archetypes`·HUD 속성이 이미 덮는다. **컴파일 검증을 못 하는 코드를 편의만으로 늘리지 않았다** |
| 에디터 핫리로드 래퍼 (`LoadLibrary`) | 위 "네이티브 플러그인" 참조 — 바인딩 경로를 둘로 두지 않는 쪽을 골랐다 |
| 스프라이트 배칭 (`NativeArray` 래핑) | `design.md`가 `ConvertExistingDataToNativeArray`를 지정한다. Unity API라 여기서 쓸 수 없다 |
| `InputKind.CardReroll` | 코어에 없다. 리롤을 넣을 때 ABI와 함께 |

## 주의 — 이 C#는 여기서 컴파일된 적이 없다

`client/Runtime/`과 `client/Harness/`는 **C# 컴파일러 없이 작성됐다.** 처음
`dotnet run`에서 컴파일 에러가 나오면 그건 예상된 비용이다 — 고치고 가면 된다.
그래서 하네스를 먼저 돌리라고 하는 것이고, 하네스가 통과하면 Unity 쪽은 연결만 남는다.
