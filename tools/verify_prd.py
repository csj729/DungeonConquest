"""PRD 상수 C가 목표 발동률과 정합한지 검사 (§7 ①).

**C는 `proc_rate_permille`에서 파생되는 값이다.** 데이터에 두 값이 같이 사는 건
"수치를 두 곳에 두지 않는다" 규칙의 예외이므로, 예외인 만큼 자동 대조를 건다 —
발동률만 바꾸고 C를 잊으면 여기서 FAIL이 나고 올바른 값을 알려준다.

C를 C++에서 계산하지 않는 이유: 목표 발동률에서 C를 구하는 건 수치해석(이분 탐색)
이라 결정론 코어에 부동소수점 반복을 들여야 한다. 파이썬이 풀고 데이터가 담는다.

여기서 같이 확인하는 것:
  - §7이 경고한 **손익분기 K**가 "억까 체감선"(K=15)보다 앞에 있는가
  - PRD의 **최대 연속 실패 상한**이 닫힌 식과 맞는가
"""
import gamedata as gd
from prd_variance import solve_c

Q16 = 65536

# 검증 목표 — 데이터가 아니라 판정 기준이다 (gamedata.py 참조).
C_TOLERANCE_Q16 = 1        # 저장값과 재계산값의 허용 차이
RATE_TOLERANCE = 0.005     # 실효 발동률의 목표 대비 상대오차 허용치
PAIN_THRESHOLD_K = 15      # §7 "억까 체감선"


def proc_rate(c):
    """상수 C에서 실효 발동률을 닫힌 식으로 구한다 (첫 성공까지 기대 시행의 역수)."""
    mean, survive, n = 0.0, 1.0, 1
    while survive > 1e-15 and n < 100000:
        p = min(1.0, c * n)
        mean += n * survive * p
        survive *= (1.0 - p)
        n += 1
    return 1.0 / mean


def streak_ge_prd(c, k):
    """PRD에서 연속 실패가 K회 이상일 확률 (성공 1회당)."""
    survive = 1.0
    for n in range(1, k + 1):
        survive *= (1.0 - min(1.0, c * n))
    return survive


def streak_ge_pure(p, k):
    return (1.0 - p) ** k


def report():
    ok = True
    hero = gd.HERO
    target = gd.pm(hero["proc_rate_permille"])
    stored = hero["proc_prd_c_q16"]

    exact = solve_c(target)
    want = int(exact * Q16 + 0.5)
    diff = abs(stored - want)

    good = diff <= C_TOLERANCE_Q16
    ok &= good
    print(f"  {'OK ' if good else 'X  '} C 정합       proc_rate {target:.1%} → C={exact:.6f} "
          f"→ q16 {want}  (저장값 {stored}, 차 {diff})")
    if not good:
        print(f"      data/hero.json의 proc_prd_c_q16을 {want}로 고칠 것")

    c = stored / Q16
    rate = proc_rate(c)
    err = abs(rate - target) / target
    good = err <= RATE_TOLERANCE
    ok &= good
    print(f"  {'OK ' if good else 'X  '} 실효 발동률   {rate:.4f} (목표 {target:.4f}, "
          f"상대오차 {err:.3%} <= {RATE_TOLERANCE:.1%})")

    # 최대 연속 실패 상한 — C×n >= 1 이 되는 n의 직전까지.
    # **순수 확률에는 이런 상한이 아예 없다.** PRD가 파는 게 정확히 이것이다.
    cap = -(-Q16 // stored) - 1
    good = streak_ge_prd(c, cap + 1) == 0.0 and streak_ge_prd(c, cap) > 0.0
    ok &= good
    print(f"  {'OK ' if good else 'X  '} 연속실패 상한  {cap}회 (그 이상은 수학적으로 불가능)")

    # §7의 경고: 손익분기 K가 체감선보다 앞에 있어야 PRD가 이득이다.
    breakeven = None
    for k in range(1, 200):
        if streak_ge_prd(c, k) < streak_ge_pure(target, k):
            breakeven = k
            break
    good = breakeven is not None and breakeven < PAIN_THRESHOLD_K
    ok &= good
    print(f"  {'OK ' if good else 'X  '} 손익분기 K     {breakeven} < 체감선 {PAIN_THRESHOLD_K} "
          f"(K 이상에서 PRD가 순수 확률보다 유리)")

    print(f"\n  연속 실패 K회 이상 확률 (성공 1회당, 발동률 {target:.0%})")
    print(f"  {'K':>4} {'순수':>10} {'PRD':>10} {'감소배수':>10}")
    for k in (5, 7, 10, 15, 20, 30):
        a = streak_ge_pure(target, k)
        b = streak_ge_prd(c, k)
        ratio = f"{a / b:>9.1f}x" if b > 1e-12 else "        ∞"
        print(f"  {k:>4} {a:>10.5f} {b:>10.5f} {ratio:>10}")

    return bool(ok)


if __name__ == "__main__":
    report()
