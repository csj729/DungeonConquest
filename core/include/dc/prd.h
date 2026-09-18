// 의사난수 분포 (PRD) — §7 ①.
//
// 순수 확률 대신 **실패할수록 확률이 올라가는** 방식을 쓴다.
//   매 시행 확률 = C × n   (n = 마지막 성공 이후 시행 횟수, 1부터)
//   성공하면 n을 0으로 리셋
//
// **기대값은 그대로인데 긴 꼬리만 지수적으로 잘린다.** 플레이어가 "억까"라고
// 느끼는 건 K=15 이상 연속 실패 구간이므로 정확히 그 구간이 제거된다.
//
// ## C는 데이터에서 온다 — C++가 계산하지 않는다
//
// 목표 발동률 p에서 C를 구하려면 "첫 성공까지의 기대 시행 횟수"를 풀어야 하고,
// 그건 수치해석(이분 탐색)이다. 결정론 코어에 부동소수점 반복을 들일 이유가 없다.
//
// **파이썬이 풀고 `data/*.json`이 담고 C++는 정수를 읽는다.** 값이 어긋나면
// `tools/verify_prd.py`가 잡는다 (run_all.py에 포함).
//
// ## 단위가 permille이 아니라 q16인 이유 (실측)
//
// C는 0.96%~8.5% 구간이라 permille(1/1000)로는 해상도가 모자란다.
//
//     목표 8%  정확한 C 0.009552
//       permille   9  → 실효 7.76%  (오차 3.006%)
//       q16      626  → 실효 8.00%  (오차 0.002%)
//
// §10이 "5% 미만 확률을 다루게 되면 확률만 별도 고정 스케일(65536 = 100%)로
// 분리하면 된다"고 적어둔 그 지점이다.
#ifndef DC_PRD_H
#define DC_PRD_H

#include <cstdint>

#include "checksum.h"
#include "rng.h"

namespace dc {

// 이번 시행의 확률(q16). trials는 마지막 성공 이후 **실패한** 횟수다.
//
// 정수 연산만 쓴다. C ≤ 65536이고 trials가 아무리 커도 아래 곱은 uint64에서
// 넘치지 않으며, 1.0에서 포화시키므로 확률이 100%를 넘지 않는다.
constexpr uint32_t prdChanceQ16(uint32_t cQ16, int32_t trials) {
    if (trials < 0) return cQ16;
    const uint64_t p = static_cast<uint64_t>(cQ16)
                     * (static_cast<uint64_t>(static_cast<uint32_t>(trials)) + 1u);
    return p >= Q16_ONE ? Q16_ONE : static_cast<uint32_t>(p);
}

// PRD 채널 하나. **카운터가 곧 상태다** — 빠지면 확률 분포가 달라지므로
// 체크섬 입력이다 (§10이 "PRD 카운터도 체크섬 입력에 포함"이라고 명시).
//
// 채널을 나누는 이유는 RNG 스트림을 나누는 이유와 같다. 통합 proc과 전설 등장이
// 카운터 하나를 공유하면 한쪽의 시행 횟수가 다른 쪽 확률을 바꾼다.
struct PrdChannel {
    int32_t trials = 0;   // 마지막 성공 이후 실패 횟수

    void reset() { trials = 0; }

    // 이번 시행의 확률. UI에 노출할 때도 이 값을 쓴다 (§8 정보 노출 정책).
    constexpr uint32_t chanceQ16(uint32_t cQ16) const {
        return prdChanceQ16(cQ16, trials);
    }

    bool roll(Rng& rng, uint32_t cQ16) {
        if (rng.chanceQ16(chanceQ16(cQ16))) {
            trials = 0;
            return true;
        }
        ++trials;
        return false;
    }

    void hashInto(Hasher& h) const { h.feed(trials); }
};

// 최악 구간의 상한. C × n >= 1.0 이 되는 n이므로 **그 이상 연속 실패가
// 수학적으로 불가능하다.** 순수 확률에는 이런 상한이 아예 없다.
constexpr int32_t prdMaxStreak(uint32_t cQ16) {
    if (cQ16 == 0) return 0;
    // trials회 실패한 뒤의 확률이 C×(trials+1)이므로, C×(trials+1) >= 1.0 이 되는
    // 최초의 trials에서 **확정 성공**이다. 따라서 공백은 그 값을 넘을 수 없다.
    //   trials >= ceil(65536 / C) - 1
    return static_cast<int32_t>((Q16_ONE + cQ16 - 1) / cQ16) - 1;
}

}  // namespace dc

#endif  // DC_PRD_H
