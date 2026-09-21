// QTE (§3) — **플레이어가 "내가 했다"고 느끼는 유일한 순간.**
//
// ## 판정은 프레젠테이션이, 검증은 시뮬이 한다
//
// 시뮬은 20Hz라 **판정 해상도의 하한이 50ms**다. 그보다 정밀한 타이밍은 알 수 없으므로
// 등급 판정 자체는 프레젠테이션 계층이 하고, 시뮬은 `(틱 번호, 등급)`을 받는다.
//
// 다만 **받은 등급을 틱으로 검증한다.** 창 밖에서 온 입력은 버리고, 완벽 구간 밖에서
// 온 `Perfect`는 `Success`로 강등한다. 시뮬이 알 수 있는 건 틱뿐이므로 틱으로 막을 수
// 있는 만큼은 막는다 — 리플레이 검증은 결정론을 보증하지 그 자체로 부정을 막지는 않는다.
//
// ## 두 종류는 실패의 의미가 정반대다
//
// | | 실패 시 |
// |---|---|
// | 스킬 증폭 | **페널티 없음** — 보너스만 없다 |
// | 위기 회피 | **확정 피격** — 명중 굴림이 따로 없고 QTE가 회피 판정 그 자체다 |
//
// 그래서 쿨다운을 다루는 방식도 다르다 (아래 openWindow 주석).
#ifndef DC_QTE_H
#define DC_QTE_H

#include <cstdint>

#include "checksum.h"
#include "entity_id.h"

namespace dc {

enum class QteKind : uint8_t {
    None         = 0,
    SkillAmplify = 1,   // 고유 스킬 자동 발동 시 — 상시 리듬
    CrisisEvade  = 2,   // 엘리트·보스의 텔레그래프 패턴 — 긴장의 피크
};

// **밀리초 판정을 포기하고 3단계로 고정한다** (§3 결정론 제약).
enum class QteGrade : uint8_t {
    Miss    = 0,
    Success = 1,
    Perfect = 2,
};

struct QteWindow {
    QteKind  kind = QteKind::None;
    EntityId source{};          // 위기 회피: 패턴을 낸 엘리트/보스
    uint16_t skillIndex = 0;    // 스킬 증폭: 가중 추첨으로 뽑힌 스킬
    int32_t  openTick   = 0;
    // **이 틱에 해결된다.** 따라서 입력을 받을 수 있는 마지막 틱은 closeTick - 1이다
    // — 드라이버가 틱 N을 그려 플레이어 입력을 받고 그 다음에 stepWorld를 부르므로,
    // closeTick에 도달한 시점엔 이미 판정이 끝나 있다.
    int32_t  closeTick  = 0;
    int32_t  perfectFrom = 0;   // 완벽 구간 [perfectFrom, perfectTo]
    int32_t  perfectTo  = 0;
    QteGrade input      = QteGrade::Miss;
    uint8_t  hasInput   = 0;

    bool open() const { return kind != QteKind::None; }

    // 시뮬이 할 수 있는 검증: **틱이 창 안인가, 완벽 구간 안인가.**
    QteGrade judge(int32_t tick, QteGrade claimed) const {
        if (!open() || tick < openTick || tick >= closeTick) return QteGrade::Miss;
        if (claimed == QteGrade::Perfect
            && (tick < perfectFrom || tick > perfectTo)) {
            return QteGrade::Success;   // 완벽 구간 밖이면 강등
        }
        return claimed;
    }

    void reset() { *this = QteWindow{}; }

    void hashInto(Hasher& h) const {
        h.feed(static_cast<uint8_t>(kind));
        h.feed(source);
        h.feed(skillIndex);
        h.feed(openTick);
        h.feed(closeTick);
        h.feed(perfectFrom);
        h.feed(perfectTo);
        h.feed(static_cast<uint8_t>(input));
        h.feed(hasInput);
    }
};

}  // namespace dc

#endif  // DC_QTE_H
