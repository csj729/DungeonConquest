# 클라이언트 브리지 (C#)

시뮬 코어를 C#에서 쓰는 계층. **Unity와 서버가 같은 파일을 쓴다** — 엔진에
의존하는 코드가 여기 없기 때문이다(`UnityEngine` 참조 0개).

```
DcNative.cs    C ABI 원시 P/Invoke 선언 — dc_abi.h와 1:1
SimWorld.cs    핸들 수명 · 설정 로드 · SoA 뷰 · 보간
TickDriver.cs  틱 소비 제어 — 배속 · 일시정지 · 보간 알파
```

## 쓰는 법

```csharp
// 호스트가 파일을 읽는다. 코어는 파일을 모른다.
var names = SimConfig.FileNames();              // "hero.json", "progression.json", ...
var files = new byte[names.Length][];
for (int i = 0; i < names.Length; ++i)
    files[i] = File.ReadAllBytes(Path.Combine(dataDir, names[i]));
    // Unity: Resources.Load<TextAsset>(...).bytes

using var config = new SimConfig(files);        // 실패하면 어느 파일 어느 키인지 말한다
using var world  = new SimWorld(config, seed);  // 설정은 복사된다 — 먼저 Dispose해도 된다
var driver = new TickDriver(world);

// 프레임마다
driver.Advance(Time.deltaTime);
world.ReadPositions(driver.Alpha, xs, ys);      // 보간된 좌표
```

## 검사

`tools/verify_abi_bindings.py`가 `dc_abi.h`와 `DcNative.cs`를 대조한다 —
함수 집합 · 시그니처 · enum 값 · 구조체 필드 순서 · ABI 버전 · 드라이버 틱레이트.
CI가 돌리므로 헤더만 고치고 C#을 잊으면 걸린다.

**이 저장소에는 C# 컴파일러가 없다.** 그래서 위 대조가 닿지 않는 부분
(`TickDriver`의 누적·상한 로직, `SimWorld`의 예외 경로)은 아직 실행으로
확인되지 않았다. Unity 프로젝트가 붙으면 그쪽에서 먼저 돌려 볼 것.

## 요구 사항

- C# 8 이상, `unsafe` 허용 (`ReadOnlySpan<T>`로 코어 배열을 복사 없이 읽는다)
- .NET Standard 2.1 이상 (`Marshal.PtrToStringUTF8`)
- 네이티브 라이브러리 `dc_abi` — `cmake --build <dir> --target dc_abi`가 만든다.
  Unity는 `Assets/Plugins/<플랫폼>/`에 둔다

## 주의

- **SoA 뷰는 다음 `Step()`까지만 유효하다.** 코어 배열을 그대로 가리키므로
  프레임 안에서 읽고 버린다. 들고 있으면 안 된다
- **보간의 `prev`는 코어가 현재 행에 맞춰 준다** (ABI 3). 틱 끝 압축이 행을
  옮겨도 정렬이 유지되므로 클라이언트가 짝지을 필요가 없다
- 배속·일시정지는 **틱 소비 속도만** 바꾼다. 시뮬 틱 내부 계산은 항상 고정
  속도이므로 결정론에 영향이 없다
