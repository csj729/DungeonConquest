"""`data/*.json` 로더 — 밸런스 수치의 단일 진실 원천.

**C++ 시뮬 코어와 파이썬 검증 도구가 같은 파일을 읽는다.** 수치가 두 곳에 살면
한쪽만 고치는 사고가 반드시 난다 — 이 세션만 해도 잡몹 HP가 10 → 30 → 20으로
두 번 뒤집혔다.

## 데이터 파일에는 정수만 담는다

JSON 숫자는 부동소수점이라 파서·플랫폼에 따라 값이 갈릴 수 있다. 결정론 코어가
읽을 파일에 소수를 두면 그 자체가 서버-클라 불일치 경로다. 그래서 **모든 소수는
정수 + 단위 접미사**로 담는다:

| 접미사 | 뜻 | 예 |
|---|---|---|
| `_permille` | 1/1000 | `crit_chance_permille: 100` = 10% |
| `_ticks` | 틱 (20Hz) | `attack_interval_ticks: 20` = 1.0초 |
| `_millitile` | 타일의 1/1000 | `radius_millitile: 19800` = 19.8타일 |
| `_q16` | 1/65536 | `proc_prd_c_q16: 2112` = 3.22% |

`_q16`은 **낮은 확률 전용**이다. permille로는 3.2%를 32로밖에 담지 못해 상대오차가
3%까지 벌어진다 (PRD 상수 C에서 실측). 같은 값이 q16에서는 오차 0.02%다.

파이썬 쪽은 여기서 실수로 되돌려 쓰고, C++ 쪽은 정수 그대로 고정소수점 연산에
넣는다. **되돌리는 지점이 이 파일 하나뿐이라는 게 핵심이다.**

## 여기 들어가지 않는 것

**검증 목표**(구간 통과 25~60초 같은 밴드)와 **모델 가정**(`AOE_TARGET_SHARE`,
`SURROUND_COUNT`, `APPROACH_SEC`)은 데이터가 아니다. 전자는 "이 수치가 맞는지
판정하는 기준"이고, 후자는 "닫힌 식으로 게임을 근사하려고 파이썬이 쓰는 값"이라
C++ 코어는 둘 다 읽지 않는다. 각 검증 도구에 남긴다.
"""
import hashlib
import json
import os

DATA_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "data")


def load(name):
    with open(os.path.join(DATA_DIR, f"{name}.json"), encoding="utf-8") as f:
        return json.load(f)


def fingerprint():
    """`data/*.json` 전체의 지문. **측정이 어느 데이터에서 나왔는지 못 박는 용도다.**

    `dc_legend`는 게임 전체를 돌리므로 **어느 파일의 어느 수치든** 측정을
    무효화한다. 전에는 손으로 고른 키 목록을 지문으로 썼는데 범위 구멍이 생겼다 —
    고유 각인 수치만 담아 둬서 E_PIERCE(공통 각인)를 고쳤을 때 조용히 통과했다.
    전체를 해싱하면 그 종류의 구멍이 사라진다.

    주석 키(`_`로 시작)는 뺀다 — 시뮬이 읽지 않으므로 설명을 고쳤다고 측정이
    낡은 것은 아니다.
    """
    def strip(o):
        if isinstance(o, dict):
            return {k: strip(v) for k, v in sorted(o.items()) if not k.startswith("_")}
        if isinstance(o, list):
            return [strip(v) for v in o]
        return o

    blob = json.dumps(
        {n: strip(load(n)) for n in sorted(
            f[:-5] for f in os.listdir(DATA_DIR) if f.endswith(".json"))},
        sort_keys=True, ensure_ascii=False, separators=(",", ":"))
    return hashlib.sha256(blob.encode("utf-8")).hexdigest()[:16]


CORE_DIR = os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "core", "include", "dc")


def _strip_cxx(src):
    """주석과 연속 공백을 없앤 C++ 소스.

    **설명을 고쳤다고 측정이 낡은 것은 아니다.** 이 코어 헤더에는 한글 주석이
    코드보다 많아서, 날것으로 해싱하면 주석 한 줄에도 가드가 울린다. 그러면
    사람이 "또 오탐이네" 하고 측정 없이 지문만 갈아 끼우게 된다 — 가드를 끄는
    가장 흔한 경로다. 그래서 의미가 바뀐 변경만 잡는다(서식 변경도 무시된다).

    문자열 리터럴 안의 `//`는 주석이 아니므로 상태 기계로 넘긴다. 코어에는 원시
    문자열(`R"(...)"`)이 없다 — 생기면 여기도 손봐야 한다.
    """
    out = []
    i, n = 0, len(src)
    while i < n:
        c = src[i]
        if c == "/" and i + 1 < n and src[i + 1] == "/":
            while i < n and src[i] != "\n":
                i += 1
            continue
        if c == "/" and i + 1 < n and src[i + 1] == "*":
            i += 2
            while i + 1 < n and not (src[i] == "*" and src[i + 1] == "/"):
                i += 1
            i += 2
            continue
        if c == '"' or c == "'":
            out.append(c)
            i += 1
            while i < n:
                if src[i] == "\\" and i + 1 < n:
                    out.append(src[i:i + 2])
                    i += 2
                    continue
                out.append(src[i])
                if src[i] == c:
                    i += 1
                    break
                i += 1
            continue
        out.append(c)
        i += 1
    return " ".join("".join(out).split())


def sim_fingerprint():
    """**시뮬레이션 결과를 결정하는 모든 것**의 지문 — 데이터 + 코어 헤더.

    `fingerprint()`(데이터만)로는 구멍이 남는다. 장판 흡혈 버그를 고쳤을 때
    `data/*.json`은 한 글자도 바뀌지 않았으므로 지문이 그대로였고,
    `verify_card_values`는 **고치기 전 소용돌이 행을 들고 PASS를 찍었다**
    (실제로는 Δ클리어가 +2.2%p → +5.5%p로 움직였다). 측정을 무효화하는 것은
    데이터만이 아니라 코어 코드다.

    코어는 헤더 온리라 `core/include/dc/*.h`가 시뮬 전부다. 측정 도구
    (`core/tools/dc_legend.cpp`)는 일부러 넣지 않는다 — 출력 서식을 고칠 때마다
    울리면 가드가 무의미해진다. 대신 시드 수는 출력에 찍히므로 눈에 보인다.
    """
    parts = ["data:" + fingerprint()]
    for name in sorted(f for f in os.listdir(CORE_DIR) if f.endswith(".h")):
        with open(os.path.join(CORE_DIR, name), encoding="utf-8") as f:
            body = _strip_cxx(f.read())
        parts.append(name + ":" + hashlib.sha256(body.encode("utf-8")).hexdigest())
    return hashlib.sha256("\n".join(parts).encode("utf-8")).hexdigest()[:16]


def _assert_integral(obj, path):
    """데이터 파일에 부동소수점이 섞이지 않았는지 확인한다 (결정론 요구사항)."""
    if isinstance(obj, float):
        raise ValueError(f"{path}: 부동소수점 {obj} — 정수 + 단위 접미사로 담을 것")
    if isinstance(obj, dict):
        for k, v in obj.items():
            _assert_integral(v, f"{path}.{k}")
    elif isinstance(obj, list):
        for i, v in enumerate(obj):
            _assert_integral(v, f"{path}[{i}]")


HERO = load("hero")
SKILLS_DATA = load("skills")
MONSTERS = load("monsters")
PROGRESSION = load("progression")
SPAWN = load("spawn")
SEGMENTS_DATA = load("segments")
CARDS = load("cards")
STATS = load("stats")

for _n, _d in (("hero", HERO), ("skills", SKILLS_DATA), ("monsters", MONSTERS),
               ("progression", PROGRESSION), ("spawn", SPAWN),
               ("segments", SEGMENTS_DATA), ("cards", CARDS), ("stats", STATS)):
    _assert_integral(_d, f"data/{_n}.json")


def pm(v):
    """permille → 실수. 파이썬 모델 전용 — C++는 정수 그대로 쓴다."""
    return v / 1000.0


def report():
    print("=== data/*.json ===")
    for name, d in (("hero", HERO), ("skills", SKILLS_DATA), ("monsters", MONSTERS),
                    ("progression", PROGRESSION), ("spawn", SPAWN),
                    ("segments", SEGMENTS_DATA), ("cards", CARDS), ("stats", STATS)):
        keys = len(d) - (1 if "_" in d else 0)
        print(f"  {name + '.json':<18} 키 {keys:>2}개   {d.get('_', '')[:46]}")
    print("\n  부동소수점 검사: 통과 (전 파일 정수만)")
    print("\n=== 핵심 수치 ===")
    print(f"  영웅       공격력 {HERO['attack_power']} / 간격 {HERO['attack_interval_ticks']}틱 / "
          f"잠식 {HERO['corruption_max']}")
    t = MONSTERS["trash"]
    print(f"  잡몹       HP {t['hp']} / 공격력 {t['damage']} / 사거리 {t['attack_range_millitile']/1000:g}타일")
    p = PROGRESSION
    print(f"  성장       레벨업당 +{pm(p['power_per_levelup_permille']):.0%} "
          f"(공속 몫 {pm(p['speed_growth_share_permille']):.0%}) / "
          f"잡몹 체력 구간당 ×{pm(p['trash_hp_scale_per_segment_permille']):.3f}")
    print(f"  경험치     need(n) = {p['level_need_base']} × "
          f"{pm(p['level_need_ratio_permille']):.3f}^(n-1)")
    print(f"  스폰       반경 {SPAWN['radius_millitile']/1000:g}타일 / "
          f"{SPAWN['interval_ticks']}틱마다 배치 {SPAWN['batch_start']}→{SPAWN['batch_end']} / "
          f"{SPAWN['directions']}방향")
    print(f"  구간       {len(SEGMENTS_DATA['segments'])}개, 게이지 목표 "
          f"{sum(s['melee'] for s in SEGMENTS_DATA['segments']) * SEGMENTS_DATA['trash_points'] + sum(len(s['elites']) for s in SEGMENTS_DATA['segments']) * SEGMENTS_DATA['elite_points']}점")
    print(f"  카드       등급 {len(CARDS['grades'])}단계 / 각인 {len(CARDS['engravings'])}종 / "
          f"유물 {len(CARDS['relics'])}종")
    print(f"  스탯       {len(STATS['stats'])}종 (하한 = 불변식, 밸런스 다이얼 아님)")
    return True


if __name__ == "__main__":
    report()
