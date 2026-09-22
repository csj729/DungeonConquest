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
#include "card.h"
#include "input.h"
#include "inventory.h"
#include "prd.h"
#include "qte.h"
#include "rng.h"
#include "stat_block.h"

namespace dc {

// 영웅. 단 하나뿐이라 배열이 아니다 (§3).
struct HeroState {
    Fixed    posX{};
    Fixed    posY{};

    // 직전 이동 방향(단위 벡터). **[상태]다** — 위협 벡터가 상쇄되는 대칭 포위에서
    // 이 값이 이동을 이어받으므로, 빠지면 그 순간 서버와 클라가 갈린다.
    Fixed    facingX{};
    Fixed    facingY{};

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
    // QTE 쿨다운 — **두 종류가 공유한다.** 위기 회피는 쿨다운을 무시하고 열리되
    // 소모는 한다. 놓치면 확정 피격인 QTE를 쿨다운으로 막으면 플레이어가
    // 통제할 수 없는 이유로 맞게 되기 때문이다 (§3).
    int32_t  qteCooldown    = 0;   // 남은 틱
    QteWindow qte{};

    // PRD 채널 (§7). 통합 proc은 기본 공격당 판정을 한 번만 굴리므로(§3)
    // 채널도 하나다. 어떤 스킬인지는 발동이 확정된 뒤 가중 추첨으로 고른다.
    // **체크섬 입력에 반드시 포함**된다 (§10 검증 하네스).
    PrdChannel proc{};

    // ── 유물 상태 (§4) ──
    // R_RAGE(분노의 토템) — 피격 시 쌓이고 일정 시간 뒤 통째로 풀린다.
    // **중첩 상한은 등급과 무관하게 고정**이다 (문서 §2) — 상한을 등급으로 올리면
    // 중첩당 수치와 곱해져 증가폭이 1 : 4 : 16이 된다.
    int32_t  rageStacks     = 0;
    int32_t  rageExpireTick = 0;
    // R_BOLT(뇌전의 성물) — 다음 방전 틱. **틱 카운터 기반이라 결정론에 안전하다.**
    int32_t  boltNextTick   = 0;

    // CC 축 아이템의 둔화 합 (permille). **[파생]이다** — 인벤토리에서 계산되므로
    // 체크섬에 넣지 않는다(인벤토리가 이미 들어간다). 아이템이 바뀔 때만 갱신한다.
    int32_t  slowAura       = 0;

    EntityId target{};             // 현재 타겟. stale이면 EntityStore가 걸러준다

    // 플레이어가 직접 지정한 타겟 (§3). 자동 우선순위를 덮어쓴다.
    // **[상태]다** — 서버가 리플레이할 때 같은 지시를 재현해야 한다.
    // 대상이 죽으면 stale이 되어 자동으로 해제된다.
    EntityId manualTarget{};

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
        h.feed(facingX);
        h.feed(facingY);
        h.feed(corruption);      // 저장값을 쓴다. corruptionLeft()는 [파생]이다
                                 // 최대치는 stats(CorruptionMax)가 들고 있다
        h.feed(level);
        h.feed(exp);
        h.feed(attackCooldown);
        h.feed(qteCooldown);
        proc.hashInto(h);        // §10이 명시적으로 요구하는 입력
        h.feed(rageStacks);
        h.feed(rageExpireTick);
        h.feed(boltNextTick);
        h.feed(target);
        h.feed(manualTarget);
        qte.hashInto(h);
        stats.hashInto(h);
    }
};

// 런의 끝. 몬테카를로 하네스가 이 값을 집계한다 (§14).
enum class RunOutcome : uint8_t {
    Running = 0,
    Cleared = 1,   // 보스 처치
    Dead    = 2,   // 잠식 게이지 만충
};

// 런 진행 — 맵·구간·클리어 게이지 (§2).
struct RunState {
    int32_t mapIndex     = 0;   // 0 기반
    int32_t segmentIndex = 0;   // 맵 내 구간, 0 기반
    // R_TIDE(밀물의 인장)가 읽는다 — 구간 경과 시간에 비례해 공격력이 오르고
    // 구간이 넘어가면 리셋된다. [상태]다.
    int32_t segmentStartTick = 0;
    int32_t clearPoints  = 0;   // 처치 포인트 누적 (잡몹 1 · 엘리트 10)
    int32_t killedTrash  = 0;
    int32_t killedElite  = 0;
    bool       bossAlive    = false;
    bool       bossSpawned  = false;
    RunOutcome outcome      = RunOutcome::Running;
    int32_t    endTick      = 0;

    bool over() const { return outcome != RunOutcome::Running; }

    // 전체 24구간 중 몇 번째인가. 동시 생존 상한 램프가 이 값을 쓴다 —
    // **맵 내 인덱스가 아니라 전역 인덱스다.** 맵별로 재시작하면 1맵과
    // 3맵의 압박이 같아지는 버그가 난다 (verify_spawn.py가 잡았던 건)
    int32_t globalSegment(int32_t segmentsPerMap) const {
        return mapIndex * segmentsPerMap + segmentIndex;
    }

    void hashInto(Hasher& h) const {
        h.feed(mapIndex);
        h.feed(segmentIndex);
        h.feed(segmentStartTick);
        h.feed(clearPoints);
        h.feed(killedTrash);
        h.feed(killedElite);
        h.feed(bossAlive);
        h.feed(bossSpawned);
        h.feed(static_cast<uint8_t>(outcome));
        h.feed(endTick);
        // globalSegment()는 [파생] — mapIndex·segmentIndex의 함수다
    }
};

// 스폰 (§2). 동시 생존 상한을 유지하는 방식이라 "다음 웨이브" 같은 상태가 없다.
// 잠식 회복 구슬 (§2) — **처치가 회복으로 바뀌는 통로다.**
//
// 즉시 회복이 아니라 드랍·습득 2단계로 두는 이유는 **위치를 1급 요소로 유지**하기
// 위해서다. 영웅은 추격 후 공격만 하므로(§3) 구슬을 주우러 돌아가지 않는다 —
// 멀리 있는 엘리트를 쫓으면 뒤에 흘린 구슬이 그대로 소멸한다. 즉 "무엇을 언제
// 때릴지"가 회복량까지 정하게 되고, 타겟 선택의 손잡이가 하나 더 늘지 않는다.
//
// EntityStore에 넣지 않는다 — 구슬은 전투 대상이 아니라서 타게팅·분리·충돌
// 전부가 낭비다. 배열 하나면 된다.
struct OrbState {
    static constexpr uint32_t MAX_ORBS = 256;

    // [상태] — 전부 체크섬 입력이다
    Fixed    posX[MAX_ORBS]{};
    Fixed    posY[MAX_ORBS]{};
    int32_t  amount[MAX_ORBS]{};       // 정화량
    int32_t  expireTick[MAX_ORBS]{};   // 이 틱에 사라진다
    uint32_t count = 0;

    // **안정 압축**이다 (EntityStore와 같은 규칙). swap-remove를 쓰면 배열 순서가
    // 습득 순서에 의존해 디버깅이 어려워진다.
    void removeAt(uint32_t i) {
        if (i >= count) return;
        for (uint32_t k = i + 1; k < count; ++k) {
            posX[k - 1] = posX[k];
            posY[k - 1] = posY[k];
            amount[k - 1] = amount[k];
            expireTick[k - 1] = expireTick[k];
        }
        --count;
    }

    // 가득 차면 **가장 먼저 사라질 구슬을 밀어낸다.** 드랍을 조용히 버리면
    // 물량이 많은 후반에 회복이 말라붙는데, 그건 밀도가 높을수록 회복이 쉬워야
    // 한다는 설계 의도와 정반대다.
    bool push(Fixed x, Fixed y, int32_t amt, int32_t expire) {
        if (amt <= 0) return false;
        if (count >= MAX_ORBS) {
            uint32_t oldest = 0;
            for (uint32_t i = 1; i < count; ++i) {
                if (expireTick[i] < expireTick[oldest]) oldest = i;
            }
            removeAt(oldest);
        }
        posX[count] = x; posY[count] = y;
        amount[count] = amt; expireTick[count] = expire;
        ++count;
        return true;
    }

    void hashInto(Hasher& h) const {
        h.feed(count);
        for (uint32_t i = 0; i < count; ++i) {
            h.feed(posX[i]); h.feed(posY[i]);
            h.feed(amount[i]); h.feed(expireTick[i]);
        }
    }
};

struct SpawnState {
    int32_t  nextSpawnTick   = 0;
    uint32_t directionCursor = 0;   // 4방향 균등 배분. 나머지 배분 순서를 고정한다
    int32_t  spawnedTotal    = 0;
    uint32_t nextEliteIndex  = 0;   // 다음에 등장할 엘리트 스폰 지점 (클리어 게이지 임계)

    void hashInto(Hasher& h) const {
        h.feed(nextSpawnTick);
        h.feed(directionCursor);
        h.feed(spawnedTotal);
        h.feed(nextEliteIndex);
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
        orbs  = OrbState{};
        hero.stats.init(nullptr, nullptr);   // 데이터 로더가 붙으면 여기로 값이 온다
        // 인벤토리는 RecipeTable이 있어야 init할 수 있으므로 여기서는 완전 초기화만
        // 한다. 호출자가 데이터를 로드한 뒤 inventory.init(table)을 부른다.
        // **이걸 빠뜨리면 이전 런의 아이템이 다음 런에 샌다** (test_world가 잡는다).
        inventory = Inventory{};
        for (uint32_t i = 0; i < STAT_COUNT; ++i) itemStatApplied[i] = 0;
        hero.slowAura = 0;
        cards     = CardState{};
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
    // **틱 루프 자체는 `sim.h`의 stepWorld()가 소유한다.** 시스템 실행 순서를
    // 한 곳에서만 정의하기 위해서다 — World가 시스템을 부르면 world.h ↔ 시스템
    // 헤더가 서로를 include해야 하고, 순서가 두 군데로 갈린다.
    void beginTick() {
        ++tick_;
        tickTimeConditions();   // §10 틱 루프의 drainConditionQueue 자리
    }
    void endTick() { applyDeaths(); }

    // 시스템 없이 틱만 넘긴다. 테스트·툴 전용이며 실제 시뮬 진행은 stepWorld()다.
    void tick() { beginTick(); endTick(); }

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
            for (uint32_t i = 0; i < STAT_COUNT; ++i) h.feed(itemStatApplied[i]);
            cards.hashInto(h);
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
            orbs.hashInto(h);    // 구슬은 스폰이 만들어낸 상태다
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

    // 잠식 정화 — **회복의 단일 통로다** (§2).
    //
    // 잠식은 반전된 체력과 동형이라 회복은 게이지를 깎는 것이다. 경로가 셋
    // (처치 · 구간 진입 · 흡혈)이지만 하한 클램프와 조건 재평가를 한 곳에 모은다 —
    // 세 군데에 흩어지면 하나만 고치는 사고가 난다.
    //
    // **0 아래로 내려가지 않는다.** 음수 잠식은 "죽기까지의 여유"를 몰래 저장하는
    // 것이라, 안전한 구간에서 쌓아두고 위험 구간에서 꺼내 쓰는 경로가 생긴다.
    void purgeCorruption(Fixed amount) {
        if (amount.raw <= 0 || hero.corruption.raw <= 0) return;
        hero.corruption -= amount;
        if (hero.corruption.raw < 0) hero.corruption = Fixed{};
        notifyCorruptionChanged();
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

    // ── 플레이어 입력 ────────────────────────────────────────────
    //
    // 모든 입력이 `(틱 번호, 값)` 한 형식이다. 서버는 클라이언트가 보낸 로그를
    // 그대로 재생해 체크섬을 비교한다 (§10).
    bool applyInput(const InputEvent& e) {
        switch (e.kind) {
            case InputKind::ManualTarget:
                if (e.value == 0) { clearManualTarget(); return true; }
                return setManualTarget(EntityId{e.value});
            case InputKind::QteGrade: {
                if (!hero.qte.open()) return false;
                if (e.value > static_cast<uint32_t>(QteGrade::Perfect)) return false;
                // **시뮬은 등급을 믿지 않고 틱으로 검증한다.**
                hero.qte.input    = hero.qte.judge(tick_, static_cast<QteGrade>(e.value));
                hero.qte.hasInput = 1;
                return true;
            }
            case InputKind::CardChoice:   // SimConfig가 필요하므로 sim.h가 처리한다
            case InputKind::None:
            case InputKind::Count:
                break;
        }
        return false;
    }

    // ── 플레이어 입력 (§3 수동 타게팅) ───────────────────────────
    //
    // 살아있는 대상만 받는다. 실패하면 false — 이미 죽은 것을 찍었다는 뜻이고,
    // 그 경우 자동 우선순위가 그대로 유지된다.
    bool setManualTarget(EntityId id) {
        if (!entities.alive(id)) return false;
        hero.manualTarget = id;
        return true;
    }
    void clearManualTarget() { hero.manualTarget = EntityId::invalid(); }

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
    // 아이템이 스탯에 실어둔 기여분(permille). **[상태]다** — 다음 갱신 때
    // 정확히 같은 값을 빼야 하므로 저장이 필수다. 스냅샷에도 따라간다.
    int32_t     itemStatApplied[STAT_COUNT] = {0};
    // 레벨업 카드 (§4). 각인·유물 누적과 전설 풀 획득 비트마스크가 여기 산다.
    CardState   cards{};
    RunState    run{};
    SpawnState  spawn{};
    OrbState    orbs{};

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
