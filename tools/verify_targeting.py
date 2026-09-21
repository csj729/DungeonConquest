"""타겟 우선순위가 QTE 예산을 지키는지 검산 (§3).

**우선순위는 밸런스 다이얼이므로 데이터에 있고, 여기서 그 숫자로 결과를 잰다.**
`monsters.json`의 `target_priority`를 바꾸면 이 도구가 바로 반응한다.

핵심 검증: 보스와 엘리트가 동시에 존재할 때 QTE가 예산 안인가.
"""
import gamedata as gd

HZ = gd.PROGRESSION["tick_hz"]
BOSS_SEC = gd.MONSTERS["map_boss_target_ticks"] / HZ
AIM_SEC  = gd.MONSTERS["archer_windup_ticks"] / HZ      # 궁병대장 조준 = QTE 주기

# 검증 목표 · 모델 가정 — 데이터가 아니다 (gamedata.py 참조)
QTE_BUDGET_MAX = 5
# monsters_vertical_slice.md: QTE 패턴을 가진 엘리트는 궁병대장·미친 고블린뿐
QTE_SOURCES = {"GE_ARCHER", "GE_MAD"}


def elites():
    return {e["id"]: e for e in gd.MONSTERS["elites"]}


def kill_sec(e):
    return (e["target_sec_min"] + e["target_sec_max"]) / 2


def report():
    ok = True
    E = elites()
    boss_prio = gd.MONSTERS["boss"]["target_priority"]
    trash_prio = gd.MONSTERS["trash"]["target_priority"]

    print(f"  우선순위  잡몹 {trash_prio} · 보스 {boss_prio} · "
          + " · ".join(f"{e['name']} {e['target_priority']}" for e in E.values()))

    # 목표 1: 엘리트가 보스보다 먼저여야 한다.
    # 보스를 먼저 치면 잔존 엘리트가 보스전(75초) 내내 살아 QTE와 피격을 계속 만든다.
    below = [e["name"] for e in E.values() if e["target_priority"] <= boss_prio]
    good = not below
    ok &= good
    print(f"  {'OK ' if good else 'X  '} 목표 1  엘리트 전원이 보스보다 우선"
          + (f"  ← 위반: {', '.join(below)}" if below else ""))

    # 목표 2: 잔존 QTE 소스 1마리가 만드는 QTE가 예산 안인가.
    print(f"  목표 2  잔존 QTE 소스의 QTE 횟수 (예산 {QTE_BUDGET_MAX}회)")
    for eid in sorted(QTE_SOURCES):
        e = E[eid]
        by_elite = kill_sec(e) / AIM_SEC
        by_boss  = BOSS_SEC / AIM_SEC
        good = by_elite <= QTE_BUDGET_MAX
        ok &= good
        print(f"    {'OK ' if good else 'X  '} {e['name']:<14} "
              f"엘리트 우선 {by_elite:>5.1f}회 / 보스 우선 {by_boss:>5.1f}회 "
              f"({by_boss/by_elite:>4.1f}배)")

    # 목표 3: 보스 우선이었다면 잠식이 얼마나 날아가는가 (뒤집지 말아야 할 근거).
    archer_dmg = 40   # monsters_vertical_slice.md 기준선 (조준 강공격)
    cmax = gd.HERO["corruption_max"]
    lost = (BOSS_SEC / AIM_SEC) * archer_dmg
    print(f"  목표 3  보스 우선 시 잔존 궁병대장 1마리 피해 {lost:.0f} "
          f"= 잠식 최대치의 {lost/cmax:.0%}  ← 뒤집으면 안 되는 이유")

    # 목표 4: 엘리트끼리의 순서 — QTE가 없어도 방치 비용이 큰 쪽이 먼저여야 한다.
    # 주술사는 QTE 패턴이 없지만 소환으로 물량을 불린다 (archetype 고정 순서로는
    # 표현할 수 없는 판단이고, 그래서 우선순위를 타입별 정수로 둔다).
    shaman = E["GE_SHAMAN"]["target_priority"]
    others = [e["target_priority"] for i, e in E.items() if i != "GE_SHAMAN"]
    good = shaman > max(others)
    ok &= good
    print(f"  {'OK ' if good else 'X  '} 목표 4  주술사({shaman})가 나머지 엘리트"
          f"(최대 {max(others)})보다 우선 — 방치 비용이 가장 크다")

    return bool(ok)


if __name__ == "__main__":
    report()
