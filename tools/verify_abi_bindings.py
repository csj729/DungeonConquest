#!/usr/bin/env python3
"""C ABI 헤더와 C# P/Invoke 선언이 어긋나지 않는지 대조 (§10 · §14-9).

**이 저장소에는 C# 컴파일러가 없다.** 그래서 `client/Runtime/DcNative.cs`는
"맞을 것이다"라는 주장으로 남는데, P/Invoke가 어긋나면 컴파일은 통과하고
**실행 중에 스택이 망가진다** — 증상이 "가끔 이상한 값"이라 찾는 데 며칠이 걸린다.

`verify_core_constants`가 `dev_data.h`에 하던 것과 같은 장치다: 두 곳에 사는
선언을 기계가 대조하고, 한쪽만 고치면 CI가 걸린다.

## 무엇을 보는가

  1. **함수 집합** — 헤더에 있는데 C#에 없거나, 그 반대
  2. **반환형과 인자 타입** — C ↔ C# 대응표로 환산해서 비교
  3. **enum 이름과 값** — 값이 어긋나면 조용히 다른 입력이 된다
  4. **구조체 필드 순서** — 순서가 바뀌면 값이 섞인다
  5. **ABI 버전 상수** — 헤더의 DC_ABI_VERSION과 C#의 AbiVersion

## 무엇을 못 보는가

호출 규약과 문자열 마샬링은 문법으로 드러나지 않는다. 그래서 `const char*`를
C#에서 `string`으로 받는 것(기본 마샬러가 해제하려 들어 힙이 깨진다)만은
**규칙으로 금지하고 여기서 검사한다** — 나머지는 사람이 봐야 한다.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HEADER = ROOT / "core" / "abi" / "dc_abi.h"
BINDING = ROOT / "client" / "Runtime" / "DcNative.cs"
DRIVER = ROOT / "client" / "Runtime" / "TickDriver.cs"

# C ↔ C# 대응. **포인터는 전부 IntPtr다** — 배열 뷰는 상위 계층이 감싼다.
TYPE_MAP = {
    "void": "void",
    "int32_t": "int",
    "uint32_t": "uint",
    "uint64_t": "ulong",
    "const char*": "IntPtr",
    "DcWorld*": "IntPtr",
    "const DcWorld*": "IntPtr",
    "DcConfig*": "IntPtr",
    "const DcConfig*": "IntPtr",
    "const int32_t*": "IntPtr",
    "const uint8_t*": "IntPtr",
    "const uint32_t*": "IntPtr",
    "DcChecksums*": "out DcChecksums",
}


def norm_c_type(t):
    """`const char *name` → (`const char*`, `name`)."""
    t = t.strip()
    t = re.sub(r"/\*.*?\*/", " ", t, flags=re.S)
    t = re.sub(r"\s+", " ", t).strip()
    if t in ("void", ""):
        return "void", None
    m = re.match(r"^(.*?)\s*\*?\s*([A-Za-z_]\w*)$", t)
    name = None
    if m and m.group(1):
        base, name = m.group(1).strip(), m.group(2)
        if "*" in t[len(m.group(1)):]:
            base += "*"
        t = base
    return re.sub(r"\s*\*", "*", t).strip(), name


def parse_header():
    src = HEADER.read_text(encoding="utf-8")
    src = re.sub(r"/\*.*?\*/", " ", src, flags=re.S)   # 주석 제거

    funcs = {}
    # **줄 머리의 DC_API만 잡는다.** `#define DC_API ...` 줄에도 같은 토큰이
    # 있어서 그냥 찾으면 전처리기 블록 전체를 반환형으로 삼킨다(실제로 그랬다).
    for m in re.finditer(r"^DC_API\s+([\w\s*]+?)\s+(dc_\w+)\s*\((.*?)\)\s*DC_NOEXCEPT\s*;",
                         src, re.S | re.M):
        ret, name, args = m.group(1), m.group(2), m.group(3)
        ret = re.sub(r"\s*\*", "*", re.sub(r"\s+", " ", ret)).strip()
        params = []
        for a in args.split(","):
            ctype, _ = norm_c_type(a)
            if ctype != "void":
                params.append(ctype)
        funcs[name] = (ret, params)

    enums = {}
    for m in re.finditer(r"typedef enum (\w+)\s*\{(.*?)\}\s*\1\s*;", src, re.S):
        body = {}
        for e in re.finditer(r"(DC_\w+)\s*=\s*(-?\d+)", m.group(2)):
            body[e.group(1)] = int(e.group(2))
        enums[m.group(1)] = body

    structs = {}
    for m in re.finditer(r"typedef struct (\w+)\s*\{(.*?)\}\s*\1\s*;", src, re.S):
        fields = [(t, n) for t, n in re.findall(r"(\w+)\s+(\w+)\s*;", m.group(2))]
        structs[m.group(1)] = fields

    version = int(re.search(r"#define DC_ABI_VERSION\s+(\d+)", src).group(1))
    return funcs, enums, structs, version


def parse_binding():
    src = BINDING.read_text(encoding="utf-8")
    src = re.sub(r"//.*", "", src)          # 줄 주석 제거 (문자열 안에 // 는 없다)

    funcs = {}
    for m in re.finditer(
            r"\[DllImport\([^\]]*\)\]\s*public\s+static\s+extern\s+(\S+)\s+(dc_\w+)\s*\((.*?)\)\s*;",
            src, re.S):
        ret, name, args = m.group(1), m.group(2), m.group(3)
        params = []
        for a in args.split(","):
            a = re.sub(r"\s+", " ", a).strip()
            if not a:
                continue
            # "out DcChecksums outChecksums" · "IntPtr w" · "int ticks"
            parts = a.rsplit(" ", 1)
            params.append(parts[0].strip() if len(parts) == 2 else a)
        funcs[name] = (ret, params)

    enums = {}
    for m in re.finditer(r"public enum (\w+)\s*\{(.*?)\}", src, re.S):
        body = {}
        for e in re.finditer(r"(\w+)\s*=\s*(-?\d+)", m.group(2)):
            body[e.group(1)] = int(e.group(2))
        enums[m.group(1)] = body

    structs = {}
    for m in re.finditer(r"public struct (\w+)\s*\{(.*?)\}", src, re.S):
        structs[m.group(1)] = [(t, n) for t, n in
                               re.findall(r"public\s+(\w+)\s+(\w+)\s*;", m.group(2))]

    version = int(re.search(r"AbiVersion\s*=\s*(\d+)", src).group(1))
    return funcs, enums, structs, version, src


# C의 DC_XXX_YYY 이름과 C#의 PascalCase를 잇는다. **값으로만 비교하면
# 이름이 뒤바뀐 실수를 못 잡으므로** 대응을 명시한다.
ENUM_NAME_MAP = {
    "DcStatus": {
        "DC_OK": "Ok", "DC_ERR_NULL": "ErrNull", "DC_ERR_ARG": "ErrArg",
        "DC_ERR_VERSION": "ErrVersion", "DC_ERR_ALLOC": "ErrAlloc",
        "DC_ERR_INPUT": "ErrInput", "DC_ERR_INTERNAL": "ErrInternal",
    },
    "DcOutcome": {
        "DC_RUN_RUNNING": "Running", "DC_RUN_CLEARED": "Cleared", "DC_RUN_DEAD": "Dead",
    },
    "DcInputKind": {
        "DC_INPUT_NONE": "None", "DC_INPUT_MANUAL_TARGET": "ManualTarget",
        "DC_INPUT_QTE_GRADE": "QteGrade", "DC_INPUT_CARD_CHOICE": "CardChoice",
        "DC_INPUT_CRAFT": "Craft",
    },
}


def report():
    hf, he, hs, hv = parse_header()
    cf, ce, cs, cv, csrc = parse_binding()
    ok = True

    print("=== C ABI ↔ C# P/Invoke 대조 ===")
    print("  **C# 컴파일러가 없는 저장소다.** 어긋난 P/Invoke는 컴파일을 통과하고")
    print("  실행 중에 스택을 망가뜨리므로, 선언이 맞다는 것을 여기서 검사로 만든다.\n")

    # 1. ABI 버전
    good = hv == cv
    ok &= good
    print(f"  {'OK ' if good else 'X  '} {'ABI 버전':<20} 헤더 {hv} · C# {cv}")

    # 2. 함수 집합
    missing = sorted(set(hf) - set(cf))
    extra = sorted(set(cf) - set(hf))
    for n in missing:
        print(f"  X   C#에 없는 함수: {n}")
    for n in extra:
        print(f"  X   헤더에 없는 함수: {n}")
    ok &= not missing and not extra
    print(f"  {'OK ' if not missing and not extra else 'X  '} {'함수 집합':<20} "
          f"헤더 {len(hf)}개 · C# {len(cf)}개")

    # 3. 시그니처
    bad = []
    for n in sorted(set(hf) & set(cf)):
        hret, hargs = hf[n]
        cret, cargs = cf[n]
        want_ret = TYPE_MAP.get(hret)
        if want_ret is None:
            bad.append(f"{n}: 대응표에 없는 반환형 {hret!r}")
            continue
        if want_ret != cret:
            bad.append(f"{n}: 반환형 {hret} → {want_ret} 이어야 하는데 {cret}")
        want_args = []
        unknown = False
        for a in hargs:
            w = TYPE_MAP.get(a)
            if w is None:
                bad.append(f"{n}: 대응표에 없는 인자 타입 {a!r}")
                unknown = True
                break
            want_args.append(w)
        if unknown:
            continue
        if want_args != cargs:
            bad.append(f"{n}: 인자 {want_args} 이어야 하는데 {cargs}")
    for b in bad:
        print(f"  X   {b}")
    ok &= not bad
    print(f"  {'OK ' if not bad else 'X  '} {'시그니처':<20} "
          f"{len(set(hf) & set(cf)) - len(bad)}/{len(set(hf) & set(cf))} 일치")

    # 4. enum
    ebad = []
    for ename, hbody in he.items():
        cbody = ce.get(ename)
        if cbody is None:
            ebad.append(f"{ename}: C#에 없다")
            continue
        names = ENUM_NAME_MAP.get(ename, {})
        for cname, val in hbody.items():
            csname = names.get(cname)
            if csname is None:
                ebad.append(f"{ename}.{cname}: 이름 대응이 없다 (ENUM_NAME_MAP에 추가할 것)")
            elif cbody.get(csname) != val:
                ebad.append(f"{ename}.{cname}={val} 인데 C# {csname}={cbody.get(csname)}")
    for b in ebad:
        print(f"  X   {b}")
    ok &= not ebad
    print(f"  {'OK ' if not ebad else 'X  '} {'enum 값':<20} "
          f"{sum(len(v) for v in he.values())}개 상수")

    # 5. 구조체 필드 순서
    sbad = []
    for sname, hfields in hs.items():
        if not hfields:          # 불투명 핸들 (typedef struct DcWorld DcWorld)
            continue
        cfields = cs.get(sname)
        if cfields is None:
            sbad.append(f"{sname}: C#에 없다")
            continue
        if len(hfields) != len(cfields):
            sbad.append(f"{sname}: 필드 수 {len(hfields)} != {len(cfields)}")
            continue
        for i, ((ht, hn), (ct, cn)) in enumerate(zip(hfields, cfields)):
            want = TYPE_MAP.get(ht, ht)
            if want != ct:
                sbad.append(f"{sname} 필드 {i}: {ht} → {want} 인데 {ct}")
            if hn.lower() != cn.lower():
                sbad.append(f"{sname} 필드 {i}: 이름 {hn} vs {cn} (순서가 어긋났을 수 있다)")
    for b in sbad:
        print(f"  X   {b}")
    ok &= not sbad
    print(f"  {'OK ' if not sbad else 'X  '} {'구조체 필드':<20} "
          f"{sum(1 for f in hs.values() if f)}개 구조체")

    # 6. 문자열 마샬링 금지 규약
    #
    # `const char*`를 C#에서 string으로 받으면 기본 마샬러가 CoTaskMemFree로
    # 해제하려 드는데, 코어가 돌려주는 것은 정적 메모리라 즉시 힙이 깨진다.
    # 문법만으로는 드러나지 않으므로 여기서 못 박는다.
    strret = re.findall(r"extern\s+string\s+(dc_\w+)", csrc)
    ok &= not strret
    for n in strret:
        print(f"  X   {n}: const char*를 string으로 받으면 안 된다 (마샬러가 해제한다)")
    print(f"  {'OK ' if not strret else 'X  '} {'문자열 마샬링':<20} "
          f"const char* 반환은 전부 IntPtr")

    # 7. 드라이버의 틱레이트가 데이터와 같은가
    #
    # **C#에만 사는 수치다.** 코어가 20Hz인데 드라이버가 30Hz로 소비하면
    # 게임이 1.5배로 흐르는데, 결정론은 멀쩡해서 체크섬으로는 안 잡힌다 —
    # 틱의 순서와 내용만 보기 때문이다. 표시 속도가 틀리는 것은 여기서만 걸린다.
    import gamedata as gd
    drv = int(re.search(r"TickHz\s*=\s*(\d+)", DRIVER.read_text(encoding="utf-8")).group(1))
    want = gd.PROGRESSION["tick_hz"]
    good = drv == want
    ok &= good
    print(f"  {'OK ' if good else 'X  '} {'드라이버 틱레이트':<20} "
          f"TickDriver {drv}Hz · progression.json {want}Hz")

    print("\n전체: " + ("PASS" if ok else "FAIL"))
    return bool(ok)


if __name__ == "__main__":
    sys.exit(0 if report() else 1)
