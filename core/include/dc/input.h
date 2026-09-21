// 플레이어 입력 로그 (§10).
//
// **입력은 상태가 아니다.** 같은 로그를 같은 순서로 먹이면 같은 결과가 나오므로,
// World는 입력을 *받는 API*만 갖고 로그 자체는 드라이버가 들고 있다.
// 서버 리플레이 검증은 클라이언트가 보낸 로그를 그대로 재생해 체크섬을 비교한다.
//
// 모든 입력이 **`(틱 번호, 값)`** 한 형식이다 — QTE 판정도, 수동 타게팅도,
// 카드 선택도. 형식을 하나로 두면 기록·전송·재생 경로를 세 번 만들지 않아도 된다.
#ifndef DC_INPUT_H
#define DC_INPUT_H

#include <cstdint>

#include "checksum.h"

namespace dc {

// **뒤에만 덧붙인다** — 값이 바뀌면 기존 리플레이가 전부 깨진다.
enum class InputKind : uint8_t {
    None         = 0,
    ManualTarget = 1,   // value = EntityId.bits (0이면 해제)
    QteGrade     = 2,   // value = QteGrade
    CardChoice   = 3,   // value = 선택지 인덱스
    Count        = 4,
};

struct InputEvent {
    int32_t   tick  = 0;
    InputKind kind  = InputKind::None;
    uint32_t  value = 0;
};

// 고정 용량. 한 판 24000틱에서 QTE 약 100회 + 카드 60회 + 수동 지시로
// 1024면 넉넉하다. **동적 할당을 하지 않는다** (CLAUDE.md).
class InputLog {
public:
    static constexpr uint32_t CAPACITY = 1024;

    void clear() { count_ = 0; cursor_ = 0; }

    // 기록은 **틱 오름차순**이어야 한다. 어긋나면 거부한다 —
    // 재생이 커서 하나로 도는 전제가 깨지기 때문이다.
    bool record(const InputEvent& e) {
        if (count_ >= CAPACITY) return false;
        if (count_ > 0 && e.tick < events_[count_ - 1].tick) return false;
        events_[count_++] = e;
        return true;
    }

    uint32_t count() const { return count_; }
    const InputEvent& at(uint32_t i) const { return events_[i]; }

    // 재생 커서를 되감는다. 스냅샷 로드 후 같은 틱부터 다시 먹일 때 쓴다.
    void rewind(int32_t toTick) {
        cursor_ = 0;
        while (cursor_ < count_ && events_[cursor_].tick < toTick) ++cursor_;
    }

    // 이 틱의 이벤트를 하나씩 꺼낸다. 없으면 false.
    bool next(int32_t tick, InputEvent* out) {
        if (cursor_ >= count_) return false;
        if (events_[cursor_].tick != tick) return false;
        *out = events_[cursor_++];
        return true;
    }

    // 서버가 클라이언트 로그의 동일성을 대조할 때 쓴다.
    uint64_t hash() const {
        Hasher h;
        h.feed(count_);
        for (uint32_t i = 0; i < count_; ++i) {
            h.feed(events_[i].tick);
            h.feed(static_cast<uint8_t>(events_[i].kind));
            h.feed(events_[i].value);
        }
        return h.value();
    }

private:
    InputEvent events_[CAPACITY]{};
    uint32_t   count_  = 0;
    uint32_t   cursor_ = 0;
};

}  // namespace dc

#endif  // DC_INPUT_H
