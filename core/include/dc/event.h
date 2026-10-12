// 출력 이벤트 큐 — **프레젠테이션이 "순간"을 알아야 한다.**
//
// 상태 버퍼(SoA)는 "지금 어디에 무엇이 있는가"를 준다. 연출에 필요한 것은 그게
// 아니라 **불연속 사건**이다: 맞았다 · 죽었다 · 레벨이 올랐다 · 카드가 떴다.
// 그걸 상태 디프로 찾으려 하면 깨진다 —
//
//   · 죽은 엔티티는 **틱 끝에 압축되어 사라진다.** 다음 프레임의 상태 버퍼에는
//     그 행이 없으므로 "어디서 죽었는지"를 알 방법이 없다. 그래서 이벤트가
//     **위치를 자기 안에 들고 있다**
//   · 같은 틱에 두 번 맞으면 디프는 한 번으로 보인다
//   · 레벨업·카드 제시는 상태로 남지만 **그 틱에 일어났다**는 사실은 안 남는다
//
// ## 체크섬에 들어가지 않는다 — 출력이기 때문이다
//
// `hashInto()`가 없다. checksum.h의 규칙([파생]은 넣지 않는다)에 해당하고, 더
// 강하게는 **시뮬이 이 큐를 읽지 않는다.** 읽는 쪽은 C# 드라이버와 테스트뿐이다.
//
//   **읽지 말 것.** 분기·수식·난수에 쓰면 그 순간 결정론 경로가 되고, 그때는
//   체크섬에 들어가야 한다. 계측기(`Metrics`)와 같은 규칙이다.
//
// 그래서 이 큐를 더해도 골든 체크섬이 움직이지 않는다 — 그 불변이 곧 "출력이
// 시뮬에 새지 않았다"는 증거다.
//
// ## 틱마다 플립하지 않는다 — 드레인할 때 비운다
//
// design.md의 틱 루프는 `flipEventQueue()`를 틱 끝에 둔다. **구현은 그러지
// 않는다**: `TickDriver`가 한 프레임에 여러 틱을 소비하기 때문이다(죽음의 나선을
// 막는 상한이 있어 1~N틱). 틱마다 플립하면 마지막 틱 것만 남고 **앞의 틱 이벤트가
// 구조적으로 사라진다.**
//
// 그래서 드레인할 때까지 **쌓는다.** 넘치면 버리고 `dropped`를 센다 — 조용히
// 잃는 쪽이 이 프로젝트에서 반복된 고장 방식이라 세는 것을 기본으로 둔다.
#ifndef DC_EVENT_H
#define DC_EVENT_H

#include <cstdint>

#include "entity_id.h"
#include "fixed.h"

namespace dc {

// **값은 종류마다 뜻이 다르다.** 아래 표가 유일한 정의다.
enum class EventKind : int32_t {
    None        = 0,
    Hit         = 1,   // entity 대상 · pos 대상 위치 · value 전달 피해(raw)
    Kill        = 2,   // entity 대상 · pos **죽은 자리** · value Archetype
    OrbDrop     = 3,   // pos 드랍 자리 · value 정화량
    OrbPick     = 4,   // pos 영웅 위치 · value **실제로** 정화된 양(raw)
    LevelUp     = 5,   // pos 영웅 위치 · value 새 레벨
    CardOffer   = 6,   // value 선택지 수 — **드라이버가 0배속으로 전환한다**
    QteOpen     = 7,   // entity 원인 · pos 원인 위치 · value 남은 틱
    QteResult   = 8,   // value QteGrade
    CcTrigger   = 9,   // entity 대상 · pos 대상 위치 · value 기절 틱
    ZoneSpawn   = 10,  // pos 중심 · value ZoneKind
    BossSpawn   = 11,  // entity 보스 · pos 보스 위치
    BossPhase2  = 12,  // entity 보스 · pos 보스 위치 · value 정화량
    SegmentClear = 13, // value 새 구간 번호
    RunEnd      = 14,  // value RunOutcome
};

// POD다 — C#가 배열로 그대로 읽는다 (design.md "POD 배열 + 개수 반환").
//
// **AoS인 이유**: 상태 버퍼는 스프라이트 배칭에 넘기므로 SoA여야 하지만, 이쪽은
// 틱마다 한 번 순회하고 끝이다. 구조체 배열이 C# 쪽에서 읽기 쉽다.
struct SimEvent {
    int32_t  kind  = 0;      // EventKind
    int32_t  tick  = 0;      // 일어난 틱
    uint32_t entity = 0;     // EntityId raw (0 = 없음)
    int32_t  posX  = 0;      // Fixed raw — **연출 위치. 엔티티가 사라져도 남는다**
    int32_t  posY  = 0;
    int32_t  value = 0;      // 종류별 수치 (위 표)
};

struct EventQueue {
    // 한 번에 담는 상한. 한 프레임이 소비하는 틱(최대 4~5)에 장판이 틱당 수십
    // 대상을 때리는 최악을 담고도 남는 크기다.
    static constexpr uint32_t MAX_EVENTS = 512;

    SimEvent events[MAX_EVENTS]{};
    uint32_t count = 0;
    // **버린 개수.** 넘쳤다는 사실을 조용히 삼키지 않는다.
    uint32_t dropped = 0;

    void push(EventKind k, int32_t tick, EntityId e, Fixed x, Fixed y, int32_t value) {
        if (count >= MAX_EVENTS) { ++dropped; return; }
        SimEvent& ev = events[count++];
        ev.kind   = static_cast<int32_t>(k);
        ev.tick   = tick;
        ev.entity = e.bits;
        ev.posX   = x.raw;
        ev.posY   = y.raw;
        ev.value  = value;
    }

    // 대상이 없는 이벤트 (카드 제시 · 구간 돌파 · 런 종료 …)
    void push(EventKind k, int32_t tick, Fixed x, Fixed y, int32_t value) {
        push(k, tick, EntityId{}, x, y, value);
    }

    void clear() { count = 0; dropped = 0; }
};

}  // namespace dc

#endif  // DC_EVENT_H
