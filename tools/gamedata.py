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
import json
import os

DATA_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "data")


def load(name):
    with open(os.path.join(DATA_DIR, f"{name}.json"), encoding="utf-8") as f:
        return json.load(f)


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
    print(f"  잡몹       HP {t['hp']} / 근접 {t['damage']} / 원거리 {t['ranged_damage']}")
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
          f"{sum(s['melee'] + s['ranged'] for s in SEGMENTS_DATA['segments']) * SEGMENTS_DATA['trash_points'] + sum(len(s['elites']) for s in SEGMENTS_DATA['segments']) * SEGMENTS_DATA['elite_points']}점")
    print(f"  카드       등급 {len(CARDS['grades'])}단계 / 각인 {len(CARDS['engravings'])}종 / "
          f"유물 {len(CARDS['relics'])}종")
    print(f"  스탯       {len(STATS['stats'])}종 (하한 = 불변식, 밸런스 다이얼 아님)")
    return True


if __name__ == "__main__":
    report()
