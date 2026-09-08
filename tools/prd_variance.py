"""의사난수 분포(PRD)의 분산 감소 효과 측정.

포트폴리오용 근거 수치를 뽑기 위한 스크립트.
동일 기대 발동률에서 '연속 실패 구간'이 얼마나 줄어드는지 비교한다.
"""
import random


def solve_c(target_p: float) -> float:
    """목표 발동률을 만족하는 PRD 상수 C를 이분 탐색으로 구한다."""
    def proc_rate(c: float) -> float:
        # 첫 성공까지의 기대 시행 횟수 → 그 역수가 실효 발동률
        mean, survive = 0.0, 1.0
        n = 1
        while survive > 1e-12 and n < 10000:
            p_n = min(1.0, c * n)
            mean += n * survive * p_n
            survive *= (1.0 - p_n)
            n += 1
        return 1.0 / mean

    lo, hi = 1e-6, 1.0
    for _ in range(200):
        mid = (lo + hi) / 2
        if proc_rate(mid) < target_p:
            lo = mid
        else:
            hi = mid
    return (lo + hi) / 2


def run(target_p: float, trials: int, seed: int = 12345):
    rng = random.Random(seed)
    c = solve_c(target_p)

    results = {}
    for mode in ("pure", "prd"):
        procs, streak, cur = 0, [], 0
        n_since = 0
        for _ in range(trials):
            p = target_p if mode == "pure" else min(1.0, c * (n_since + 1))
            if rng.random() < p:
                procs += 1
                if cur:
                    streak.append(cur)
                cur, n_since = 0, 0
            else:
                cur += 1
                n_since += 1
        results[mode] = {
            "rate": procs / trials,
            "streaks": streak,
            "max": max(streak) if streak else 0,
        }
    return c, results


def streak_ge(streaks, k):
    return sum(1 for s in streaks if s >= k) / max(1, len(streaks))


if __name__ == "__main__":
    TRIALS = 2_000_000
    print(f"{'발동률':>6} {'C':>8} {'실측(순수)':>10} {'실측(PRD)':>10}"
          f" {'최대연속(순수)':>14} {'최대연속(PRD)':>13}")
    print("-" * 72)
    rows = []
    for p in (0.08, 0.12, 0.15, 0.25):
        c, r = run(p, TRIALS)
        rows.append((p, c, r))
        print(f"{p:>6.0%} {c:>8.5f} {r['pure']['rate']:>10.4f}"
              f" {r['prd']['rate']:>10.4f} {r['pure']['max']:>14d} {r['prd']['max']:>13d}")

    print()
    print("연속 실패 K회 이상이 발생할 확률 (성공 1회당)")
    print(f"{'발동률':>6} {'K':>4} {'순수':>10} {'PRD':>10} {'감소배수':>10}")
    print("-" * 46)
    for p, c, r in rows:
        for k in (10, 15, 20, 30):
            a = streak_ge(r["pure"]["streaks"], k)
            b = streak_ge(r["prd"]["streaks"], k)
            ratio = f"{a/b:>9.1f}x" if b > 0 else "     ∞"
            print(f"{p:>6.0%} {k:>4d} {a:>10.5f} {b:>10.5f} {ratio:>10}")
