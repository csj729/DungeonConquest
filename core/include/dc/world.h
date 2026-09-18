// World — 시뮬레이션 상태 전부.
//
// **여기 있는 것이 곧 체크섬 입력이고, 여기 없는 것은 재현에 필요 없는 것이다.**
// 그 경계를 흐리면 §14-2 체크섬이 거짓 양성(캐시 차이로 갈림)이나 거짓 음성
// (상태인데 해시에 안 들어감)을 낸다. 그래서 각 필드에 구분을 남긴다.
//
//   [상태]  재현에 필요하다. 체크섬 입력이고 스냅샷에 들어간다
//   [파생]  상태에서 언제든 다시 만들 수 있다. 체크섬에 넣지 않는다
//           (예: 공간 그리드, 스탯 캐시, 조합 가능 비트)
//
// 지금 World에는 [파생]이 하나도 없다. 그리드와 스탯 캐시가 붙을 때
// **별도 구조체로 분리해서** 담는다 — 같은 구조체에 섞이면 해시 대상을
// 필드 단위로 골라내야 하고, 그건 빠뜨리기 딱 좋은 작업이다.
#ifndef DC_WORLD_H
#define DC_WORLD_H

#include <cstdint>

#include "config.h"
#include "entity_store.h"
#include "fixed.h"
#include "rng.h"

namespace dc {

// 영웅. 단 하나뿐이라 배열이 아니다 (§3).
struct HeroState {
    Fixed    posX{};
    Fixed    posY{};

    // 잠식 게이지 — 체력의 자리를 대신한다 (§2·§9).
    // **누적값을 저장한다.** 체력처럼 `max - taken`으로 뒤집지 않으므로
    // 최대치가 이벤트로 오르내려도 손실이 없다.
    Fixed    corruption{};
    Fixed    corruptionMax{};

    int32_t  level        = 1;
    // 경험치는 Fixed가 아니다 (§4). 고정소수점 범위 ±524,288을 훨씬 넘고,
    // 소수점이 필요 없는 정수 누적값이다.
    int64_t  exp          = 0;

    int32_t  attackCooldown = 0;   // 남은 틱
    int32_t  qteCooldown    = 0;   // 남은 틱

    // PRD 카운터 (§7). 마지막 성공 이후 시행 횟수 — 이게 없으면 확률 분포가 달라진다.
    // **체크섬 입력에 반드시 포함**된다 (§10 검증 하네스).
    int32_t  prdTrials    = 0;

    EntityId target{};             // 현재 타겟. stale이면 EntityStore가 걸러준다

    // 게이지 잔량. 항상 0 이상으로 클램프한다.
    Fixed corruptionLeft() const {
        const Fixed d = corruptionMax - corruption;
        return d.raw > 0 ? d : Fixed{};
    }
    // 최대치가 내려가 누적값을 넘어도 게임오버다 — 설계된 결과다 (§9).
    bool dead() const { return corruptionMax.raw <= corruption.raw; }
};

// 런 진행 — 맵·구간·클리어 게이지 (§2).
struct RunState {
    int32_t mapIndex     = 0;   // 0 기반
    int32_t segmentIndex = 0;   // 맵 내 구간, 0 기반
    int32_t clearPoints  = 0;   // 처치 포인트 누적 (잡몹 1 · 엘리트 10)
    int32_t killedTrash  = 0;
    int32_t killedElite  = 0;
    bool    bossAlive    = false;

    // 전체 24구간 중 몇 번째인가. 동시 생존 상한 램프가 이 값을 쓴다 —
    // **맵 내 인덱스가 아니라 전역 인덱스다.** 맵별로 재시작하면 1맵과
    // 3맵의 압박이 같아지는 버그가 난다 (verify_spawn.py가 잡았던 건)
    int32_t globalSegment(int32_t segmentsPerMap) const {
        return mapIndex * segmentsPerMap + segmentIndex;
    }
};

// 스폰 (§2). 동시 생존 상한을 유지하는 방식이라 "다음 웨이브" 같은 상태가 없다.
struct SpawnState {
    int32_t  nextSpawnTick   = 0;
    uint32_t directionCursor = 0;   // 4방향 균등 배분. 나머지 배분 순서를 고정한다
    int32_t  spawnedTotal    = 0;
};

class World {
public:
    // 마스터 시드 하나가 런 전체를 결정한다. 여기서 모든 스트림이 파생된다.
    void init(uint64_t masterSeed) {
        masterSeed_ = masterSeed;
        tick_       = 0;

        entities.init();
        hero  = HeroState{};
        run   = RunState{};
        spawn = SpawnState{};

        rngSpawn  = Rng::derive(masterSeed, RngStream::Spawn);
        rngCombat = Rng::derive(masterSeed, RngStream::Combat);
        rngCards  = Rng::derive(masterSeed, RngStream::Cards);
        rngItems  = Rng::derive(masterSeed, RngStream::Items);
        rngEvents = Rng::derive(masterSeed, RngStream::Events);
    }

    // 고정 20Hz 한 틱. **코어가 스스로 부르지 않는다** — 호출 빈도는 드라이버
    // (클라 프레임 루프 / 서버 헤드리스 루프)의 책임이고 시뮬 상태 밖이다.
    // 배속·일시정지가 결정론에 영향이 없는 이유가 이것이다 (§10).
    //
    // 시스템 실행 순서는 고정이며 여기가 그 단일 정의점이다 (§10 틱 루프):
    //     drainConditionQueue → Spawn → Targeting → Combat → applyDeaths
    // 아직 시스템이 없으므로 지금은 틱 전진과 일괄 압축만 수행한다.
    void tick() {
        ++tick_;
        // TODO(§14-3~) Spawn::run / Targeting::run / Combat::run 이 여기 들어온다.
        applyDeaths();
    }

    // 틱 종료 일괄 압축. tick()이 부르지만, 시스템을 직접 돌리는 테스트·툴에서
    // 쓸 수 있도록 공개한다.
    uint32_t applyDeaths() { return entities.compact(); }

    int32_t  tickCount()  const { return tick_; }
    uint64_t masterSeed() const { return masterSeed_; }

    // 틱을 초로 환산. 실시간을 참조하지 않는다 — 틱 카운터만이 시간이다.
    int32_t elapsedSeconds() const { return tick_ / config::TICK_HZ; }

    // ---- [상태] 여기부터 전부 체크섬 입력 ----
    EntityStore entities{};
    HeroState   hero{};
    RunState    run{};
    SpawnState  spawn{};

    Rng rngSpawn{};
    Rng rngCombat{};
    Rng rngCards{};
    Rng rngItems{};
    Rng rngEvents{};

private:
    int32_t  tick_       = 0;
    uint64_t masterSeed_ = 0;
};

}  // namespace dc

#endif  // DC_WORLD_H
