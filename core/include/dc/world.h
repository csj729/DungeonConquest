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

#include "checksum.h"
#include "config.h"
#include "entity_store.h"
#include "fixed.h"
#include "inventory.h"
#include "prd.h"
#include "rng.h"
#include "stat_block.h"

namespace dc {

// 영웅. 단 하나뿐이라 배열이 아니다 (§3).
struct HeroState {
    Fixed    posX{};
    Fixed    posY{};

    // 잠식 게이지 — 체력의 자리를 대신한다 (§2·§9).
    // **누적값을 저장한다.** 체력처럼 `max - taken`으로 뒤집지 않으므로
    // 최대치가 이벤트로 오르내려도 손실이 없다.
    //
    // **최대치는 여기 없다.** `Stat::CorruptionMax`가 유일한 원천이다 —
    // 돌발 이벤트(§6 A)가 최대치를 모디파이어로 건드리므로, 별도 필드를 두면
    // 두 값이 갈라진다. 읽을 때는 corruptionMax()를 쓴다.
    Fixed    corruption{};

    int32_t  level        = 1;
    // 경험치는 Fixed가 아니다 (§4). 고정소수점 범위 ±524,288을 훨씬 넘고,
    // 소수점이 필요 없는 정수 누적값이다.
    int64_t  exp          = 0;

    int32_t  attackCooldown = 0;   // 남은 틱
    int32_t  qteCooldown    = 0;   // 남은 틱

    // PRD 채널 (§7). 통합 proc은 기본 공격당 판정을 한 번만 굴리므로(§3)
    // 채널도 하나다. 어떤 스킬인지는 발동이 확정된 뒤 가중 추첨으로 고른다.
    // **체크섬 입력에 반드시 포함**된다 (§10 검증 하네스).
    PrdChannel proc{};

    EntityId target{};             // 현재 타겟. stale이면 EntityStore가 걸러준다

    // 모디파이어 컨테이너 (§9). 아이템·각인·유물·이벤트 효과가 전부 여기로 들어온다.
    //
    // **엔티티에는 아직 달지 않는다.** 보스 기믹이 실제로 스탯을 만질 때 달면 된다.
    // 1024칸 전부에 미리 달면 1MB에 체크섬 비용도 그만큼 늘어난다 — §9가
    // "지금 미리 만들 필요는 없다"고 한 것과 같은 판단이다.
    StatBlock stats{};

    // 최대치는 모디파이어를 거친 스탯 값이다. 하한(stats.json)이 0보다 크므로
    // 아래 나눗셈의 분모가 0이 되지 않는다.
    Fixed corruptionMax() const { return stats.value(Stat::CorruptionMax); }

    // 게이지 잔량. 항상 0 이상으로 클램프한다.
    Fixed corruptionLeft() const {
        const Fixed d = corruptionMax() - corruption;
        return d.raw > 0 ? d : Fixed{};
    }
    // 최대치가 내려가 누적값을 넘어도 게임오버다 — 설계된 결과다 (§9).
    bool dead() const { return corruptionMax().raw <= corruption.raw; }

    // 잠식 비율(permille). CorruptionThreshold 조건이 읽는 값이다.
    // **분모가 0이면 이미 죽은 것으로 본다** — 0 나눗셈은 UB다.
    int32_t corruptionPermille() const {
        const int32_t maxRaw = corruptionMax().raw;
        if (maxRaw <= 0) return 1000;
        return static_cast<int32_t>(
            (static_cast<int64_t>(corruption.raw) * 1000) / maxRaw);
    }

    void hashInto(Hasher& h) const {
        h.feed(posX);
        h.feed(posY);
        h.feed(corruption);      // 저장값을 쓴다. corruptionLeft()는 [파생]이다
                                 // 최대치는 stats(CorruptionMax)가 들고 있다
        h.feed(level);
        h.feed(exp);
        h.feed(attackCooldown);
        h.feed(qteCooldown);
        proc.hashInto(h);        // §10이 명시적으로 요구하는 입력
        h.feed(target);
        stats.hashInto(h);
    }
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

    void hashInto(Hasher& h) const {
        h.feed(mapIndex);
        h.feed(segmentIndex);
        h.feed(clearPoints);
        h.feed(killedTrash);
        h.feed(killedElite);
        h.feed(bossAlive);
        // globalSegment()는 [파생] — mapIndex·segmentIndex의 함수다
    }
};

// 스폰 (§2). 동시 생존 상한을 유지하는 방식이라 "다음 웨이브" 같은 상태가 없다.
struct SpawnState {
    int32_t  nextSpawnTick   = 0;
    uint32_t directionCursor = 0;   // 4방향 균등 배분. 나머지 배분 순서를 고정한다
    int32_t  spawnedTotal    = 0;

    void hashInto(Hasher& h) const {
        h.feed(nextSpawnTick);
        h.feed(directionCursor);
        h.feed(spawnedTotal);
    }
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
        hero.stats.init(nullptr, nullptr);   // 데이터 로더가 붙으면 여기로 값이 온다
        // 인벤토리는 RecipeTable이 있어야 init할 수 있으므로 여기서는 완전 초기화만
        // 한다. 호출자가 데이터를 로드한 뒤 inventory.init(table)을 부른다.
        // **이걸 빠뜨리면 이전 런의 아이템이 다음 런에 샌다** (test_world가 잡는다).
        inventory = Inventory{};
        nextSourceId_ = 1;                   // 0은 "없음" 예약

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
        tickTimeConditions();   // §10 틱 루프의 drainConditionQueue 자리
        // TODO(§14-6~) Spawn::run / Targeting::run / Combat::run 이 여기 들어온다.
        applyDeaths();
    }

    // 틱 종료 일괄 압축. tick()이 부르지만, 시스템을 직접 돌리는 테스트·툴에서
    // 쓸 수 있도록 공개한다.
    uint32_t applyDeaths() { return entities.compact(); }

    // 전 상태 해시 + 시스템별 부분 체크섬 (§10 검증 하네스).
    //
    // **호출 빈도는 용도가 정한다.** 재현성 하네스는 매 틱 부른다(최초로 갈라진
    // 틱을 찾아야 하므로). 서버 리플레이 검증과 클라이언트는 런 종료 시점이나
    // 일정 간격으로 부르면 된다 — 매 틱 부를 이유가 없다.
    Checksums checksums() const {
        Checksums c;
        {
            Hasher h;
            entities.hashInto(h);
            c.entities = h.value();
        }
        {
            Hasher h;
            hero.hashInto(h);
            h.feed(nextSourceId_);
            inventory.hashInto(h);   // §10이 "인벤토리도 체크섬 입력"이라고 명시
            c.hero = h.value();
        }
        {
            Hasher h;
            run.hashInto(h);
            c.run = h.value();
        }
        {
            Hasher h;
            spawn.hashInto(h);
            c.spawn = h.value();
        }
        {
            // 전역 스트림. 소비 횟수가 다르면 여기서 갈린다 —
            // 결과가 아직 상태에 반영되기 전이라도 잡힌다는 뜻이다.
            Hasher h;
            h.feed(rngSpawn);
            h.feed(rngCombat);
            h.feed(rngCards);
            h.feed(rngItems);
            h.feed(rngEvents);
            c.rng = h.value();
        }
        c.seal(static_cast<uint64_t>(static_cast<uint32_t>(tick_)), masterSeed_);
        return c;
    }

    uint64_t checksum() const { return checksums().total; }

    // ── 조건부 모디파이어 (§9) ────────────────────────────────────
    //
    // **매 틱 전수 평가를 하지 않는다.** 값이 바뀐 쪽만 알려주고, StatBlock이
    // 역인덱스로 "그 조건을 가진 스탯"만 다시 본다. 조건부가 없으면 O(1)이다.

    ConditionContext conditionContext() const {
        ConditionContext ctx;
        ctx.tick               = tick_;
        ctx.corruptionPermille = hero.corruptionPermille();
        const int32_t d = entities.denseOf(hero.target);
        ctx.targetArchetype = (d >= 0)
            ? static_cast<uint8_t>(entities.archetype[static_cast<uint32_t>(d)])
            : uint8_t{0xFF};
        return ctx;
    }

    // 잠식 게이지가 움직였을 때. 피격·물량 충전·흡혈 전부 여기로 온다.
    uint16_t notifyCorruptionChanged() {
        return hero.stats.refreshConditions(
            conditionContext(), conditionKindBit(ConditionKind::CorruptionThreshold));
    }

    // 타겟이 바뀌었을 때 (§9 "대상 의존 — 타겟 변경 시").
    uint16_t notifyTargetChanged() {
        return hero.stats.refreshConditions(
            conditionContext(), conditionKindBit(ConditionKind::TargetArchetype));
    }

    // 시간 조건. **만료 틱이 되기 전에는 아무것도 하지 않는다** —
    // 매 틱 하는 일은 int 비교 하나뿐이다.
    uint16_t tickTimeConditions() {
        if (tick_ < hero.stats.nextExpiryTick()) return 0;
        return hero.stats.refreshConditions(
            conditionContext(), conditionKindBit(ConditionKind::TimeWindow));
    }

    // 스냅샷 로드나 데이터 재로드 직후처럼 무엇이 바뀌었는지 모를 때.
    uint16_t refreshAllConditions() {
        uint16_t mask = 0;
        for (uint32_t k = 0; k < CONDITION_KIND_COUNT; ++k) {
            mask |= conditionKindBit(static_cast<ConditionKind>(k));
        }
        return hero.stats.refreshConditions(conditionContext(), mask);
    }

    // 모디파이어 sourceId 발급 (§9 "전역 단조 증가 카운터").
    //
    // PercentMult·Override의 적용 순서가 이 값으로 정해지므로 **발급 순서가
    // 곧 결정론이다.** 포인터 값이나 주소를 키로 쓰지 않는 이유가 이것이다.
    uint32_t allocSourceId() { return nextSourceId_++; }
    uint32_t peekSourceId() const { return nextSourceId_; }

    int32_t  tickCount()  const { return tick_; }
    uint64_t masterSeed() const { return masterSeed_; }

    // 틱을 초로 환산. 실시간을 참조하지 않는다 — 틱 카운터만이 시간이다.
    int32_t elapsedSeconds() const { return tick_ / config::TICK_HZ; }

    // ---- [상태] 여기부터 전부 체크섬 입력 ----
    EntityStore entities{};
    HeroState   hero{};
    // 인벤토리 (§5). RecipeTable은 정적 데이터라 World 밖에 살고, 조회가 필요한
    // 호출마다 인자로 받는다 — 포인터 멤버를 두면 memcpy 스냅샷이 깨진다.
    Inventory   inventory{};
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
    // [상태] — 다음 모디파이어가 받을 적용 순서를 정한다.
    // 지금은 영웅 스탯만 쓰므로 hero 영역에서 해시한다. 몬스터 모디파이어가
    // 생기면 별도 영역으로 옮긴다.
    uint32_t nextSourceId_ = 1;
};

}  // namespace dc

#endif  // DC_WORLD_H
