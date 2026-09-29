// 정수 전용 JSON 파서 (§0 · CLAUDE.md "데이터 파일에는 정수만 담는다").
//
// ## 왜 코어가 JSON을 직접 읽는가
//
// 파이썬이 바이너리를 굽고 C++가 그걸 읽는 방법도 있다. 그러면 같은 수치의 표현이
// 셋(JSON → 블롭 → C++)이 되고, 블롭이 낡는 순간 **파이썬과 C++가 서로 다른 게임을
// 잰다.** 이 저장소가 반복해서 물린 고장이 정확히 그것이다. 한쪽을 읽게 하면
// `verify_core_constants`의 대조 80항목이 필요 없어져서 사라진다 —
// 검사를 늘리는 게 아니라 검사할 이유를 없앤다.
//
// ## 소수를 파싱하지 않는 것이 기능이다
//
// JSON 숫자는 명세상 부동소수점이고, 그건 곧 파서·플랫폼에 따라 값이 갈리는
// 서버-클라 불일치 경로다. 그래서 이 파서는 `.` `e` `E`를 만나면 **파싱을 실패시킨다.**
// 데이터 규약을 문서가 아니라 로더가 강제하게 만드는 자리다 — 누가 `1.5`를 적으면
// 소리 없이 반올림되는 대신 로드가 죽는다.
//
// ## 파일을 읽지 않는다
//
// 버퍼만 받는다. Unity는 `TextAsset`, 서버는 `File.ReadAllBytes`, 도구는 `fread`로
// 각자 읽어 넘긴다 — 코어에 파일 I/O가 들어가면 레이어 분리가 깨진다 (CLAUDE.md).
//
// ## 결정론
//
// 파싱은 틱 루프 밖에서 한 번 일어나고 시뮬 상태에 들어가는 것은 **정수뿐**이다.
// 노드는 평면 배열에 담고 키 조회는 선형 탐색이다 — 해시 컨테이너를 쓰지 않으므로
// `std::hash` 금지에도, 순회 순서 규칙에도 걸리지 않는다. 오브젝트 하나의 키가
// 수십 개 규모라 선형 탐색이 오히려 빠르다.
#ifndef DC_JSON_H
#define DC_JSON_H

#include <cstddef>
#include <cstdint>
#include <vector>

namespace dc::json {

enum class Type : uint8_t { Null, Bool, Int, String, Array, Object };

// 노드 하나. **문자열을 복사하지 않는다** — 원문 버퍼 안의 (시작, 길이)를 들고 있다.
// 호출자가 버퍼를 파서보다 오래 살려 두어야 한다는 뜻이고, 그 대가로 로드 중
// 할당이 노드 배열 하나뿐이다.
struct Node {
    Type     type     = Type::Null;
    int64_t  num      = 0;     // Int · Bool(0/1)
    uint32_t textAt   = 0;     // String 값의 원문 위치
    uint32_t textLen  = 0;
    uint32_t keyAt    = 0;     // Object 멤버라면 키의 원문 위치
    uint32_t keyLen   = 0;
    uint32_t firstKid = 0;     // Array·Object의 첫 자식. 0 = 없음 (0번은 루트라 자식일 수 없다)
    uint32_t nextSib  = 0;     // 다음 형제. 0 = 끝
    uint32_t kidCount = 0;
};

class Doc;

// 문서 안의 한 자리를 가리키는 커서. 값 타입이라 그냥 복사한다.
//
// **없는 키를 조용히 넘기지 않는다.** 조회가 빗나가면 문서에 오류가 기록되고,
// 로더는 마지막에 `ok()` 한 번만 보면 된다. 기본값을 돌려주는 API였다면
// 오타 하나가 "그 수치는 0이었다"로 통과한다 — 이 저장소가 가장 자주 물린
// 고장 방식(빨간불이 아니라 아무것도 안 보는 초록불)이 그것이다.
class Value {
  public:
    Value() = default;
    Value(const Doc* doc, uint32_t idx) : doc_(doc), idx_(idx) {}

    bool valid() const { return doc_ != nullptr && idx_ != 0; }
    Type type() const;

    bool isInt()    const { return type() == Type::Int; }
    bool isArray()  const { return type() == Type::Array; }
    bool isObject() const { return type() == Type::Object; }

    uint32_t size() const;              // Array·Object의 원소 수
    Value    at(uint32_t i) const;      // Array 인덱스 (범위 밖이면 오류 기록)
    Value    operator[](const char* key) const;   // Object 키 (없으면 오류 기록)
    bool     has(const char* key) const;          // **오류를 기록하지 않는 존재 확인**

    // 값 꺼내기. 타입이 다르면 오류를 기록하고 0을 돌려준다 — 0이 정상값일 수
    // 있으므로 **반환값으로 성공을 판단하지 말 것.** `Doc::ok()`가 판단한다.
    int64_t  asInt() const;
    int32_t  asI32() const;             // int32 범위를 벗어나면 오류
    bool     asBool() const;
    bool     strEquals(const char* s) const;
    // 두 문자열 노드가 같은가. 조합식 재료(`"C1"`)를 아이템 id 목록과 맞출 때 쓴다 —
    // 양쪽 다 원문 안의 범위라 복사 없이 비교한다.
    bool     strSame(const Value& other) const;

  private:
    const Doc* doc_ = nullptr;
    uint32_t   idx_ = 0;
};

class Doc {
  public:
    // 파싱. 실패하면 false, `error()`·`errorOffset()`이 이유를 말한다.
    bool parse(const char* text, size_t len);

    bool        ok()          const { return err_ == nullptr; }
    const char* error()       const { return err_ ? err_ : ""; }
    size_t      errorOffset() const { return errOff_; }
    // 조회 실패가 기록한 키 (있으면). 파싱 오류와 구분해서 보고하기 위한 것이다.
    const char* errorKey()    const { return errKey_; }

    Value root() const { return Value(this, nodes_.empty() ? 0 : 1); }

    // 파싱 이후의 조회 실패를 기록한다. Value가 부른다.
    void fail(const char* msg, const char* key = nullptr) const {
        if (err_ != nullptr) return;    // 첫 오류를 남긴다 — 뒤는 그 여파다
        err_    = msg;
        errKey_ = key;
    }

    const Node& node(uint32_t i) const { return nodes_[i]; }
    uint32_t    nodeCount() const { return static_cast<uint32_t>(nodes_.size()); }
    const char* text() const { return text_; }

  private:
    // 재귀 하강. 깊이 상한을 두어 악의적·손상된 입력이 스택을 넘기지 못하게 한다.
    static constexpr int32_t MAX_DEPTH = 32;

    uint32_t parseValue(int32_t depth);
    void     skipWs();
    bool     parseString(uint32_t* at, uint32_t* len);
    bool     parseNumber(int64_t* out);
    bool     literal(const char* s);
    void     setError(const char* msg) {
        if (err_ == nullptr) { err_ = msg; errOff_ = pos_; }
    }

    const char* text_ = nullptr;
    size_t      len_  = 0;
    size_t      pos_  = 0;

    std::vector<Node> nodes_;

    // mutable — 조회 실패는 const 경로에서 기록된다
    mutable const char* err_    = nullptr;
    mutable const char* errKey_ = nullptr;
    size_t              errOff_ = 0;
};

// ── Value 구현 ────────────────────────────────────────────────────────────

inline Type Value::type() const {
    return valid() ? doc_->node(idx_).type : Type::Null;
}

inline uint32_t Value::size() const {
    return valid() ? doc_->node(idx_).kidCount : 0;
}

inline Value Value::at(uint32_t i) const {
    if (!valid()) return Value();
    const Node& n = doc_->node(idx_);
    if (n.type != Type::Array && n.type != Type::Object) {
        doc_->fail("배열이 아닌 것에 인덱스 조회");
        return Value();
    }
    if (i >= n.kidCount) {
        doc_->fail("배열 범위 밖 인덱스");
        return Value();
    }
    uint32_t k = n.firstKid;
    for (uint32_t step = 0; step < i; ++step) k = doc_->node(k).nextSib;
    return Value(doc_, k);
}

namespace detail {
inline bool keyIs(const char* text, const Node& n, const char* key) {
    uint32_t i = 0;
    for (; i < n.keyLen; ++i) {
        if (key[i] == '\0' || text[n.keyAt + i] != key[i]) return false;
    }
    return key[i] == '\0';
}
}  // namespace detail

inline bool Value::has(const char* key) const {
    if (!valid() || doc_->node(idx_).type != Type::Object) return false;
    for (uint32_t k = doc_->node(idx_).firstKid; k != 0; k = doc_->node(k).nextSib) {
        if (detail::keyIs(doc_->text(), doc_->node(k), key)) return true;
    }
    return false;
}

inline Value Value::operator[](const char* key) const {
    if (!valid()) return Value();
    const Node& n = doc_->node(idx_);
    if (n.type != Type::Object) {
        doc_->fail("오브젝트가 아닌 것에 키 조회", key);
        return Value();
    }
    for (uint32_t k = n.firstKid; k != 0; k = doc_->node(k).nextSib) {
        if (detail::keyIs(doc_->text(), doc_->node(k), key)) return Value(doc_, k);
    }
    doc_->fail("없는 키", key);
    return Value();
}

inline int64_t Value::asInt() const {
    if (!valid()) return 0;                        // 조회 단계에서 이미 기록됐다
    const Node& n = doc_->node(idx_);
    if (n.type != Type::Int) { doc_->fail("정수가 아니다"); return 0; }
    return n.num;
}

inline int32_t Value::asI32() const {
    const int64_t v = asInt();
    if (v > INT32_MAX || v < INT32_MIN) {
        doc_->fail("int32 범위를 벗어난다");
        return 0;
    }
    return static_cast<int32_t>(v);
}

inline bool Value::asBool() const {
    if (!valid()) return false;
    const Node& n = doc_->node(idx_);
    if (n.type != Type::Bool) { doc_->fail("불리언이 아니다"); return false; }
    return n.num != 0;
}

inline bool Value::strEquals(const char* s) const {
    if (!valid()) return false;
    const Node& n = doc_->node(idx_);
    if (n.type != Type::String) { doc_->fail("문자열이 아니다"); return false; }
    uint32_t i = 0;
    for (; i < n.textLen; ++i) {
        if (s[i] == '\0' || doc_->text()[n.textAt + i] != s[i]) return false;
    }
    return s[i] == '\0';
}

inline bool Value::strSame(const Value& other) const {
    if (!valid() || !other.valid()) return false;
    const Node& a = doc_->node(idx_);
    const Node& b = other.doc_->node(other.idx_);
    if (a.type != Type::String || b.type != Type::String) {
        doc_->fail("문자열이 아니다");
        return false;
    }
    if (a.textLen != b.textLen) return false;
    for (uint32_t i = 0; i < a.textLen; ++i) {
        if (doc_->text()[a.textAt + i] != other.doc_->text()[b.textAt + i]) return false;
    }
    return true;
}

// ── Doc 구현 ──────────────────────────────────────────────────────────────

inline void Doc::skipWs() {
    while (pos_ < len_) {
        const char c = text_[pos_];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { ++pos_; continue; }
        break;
    }
}

inline bool Doc::literal(const char* s) {
    size_t i = 0;
    while (s[i] != '\0') {
        if (pos_ + i >= len_ || text_[pos_ + i] != s[i]) return false;
        ++i;
    }
    pos_ += i;
    return true;
}

// 문자열. **이스케이프는 건너뛰기만 한다** — 값을 해석하지 않고 원문 범위만
// 들고 있으므로, `\"`가 문자열을 끝내지 않는다는 것만 알면 된다.
// 데이터의 문자열은 전부 식별자(아이템 ID 등)와 주석이라 이걸로 충분하다.
inline bool Doc::parseString(uint32_t* at, uint32_t* len) {
    if (pos_ >= len_ || text_[pos_] != '"') { setError("문자열이 와야 한다"); return false; }
    ++pos_;
    const size_t start = pos_;
    while (pos_ < len_) {
        const char c = text_[pos_];
        if (c == '\\') {
            pos_ += 2;                       // 다음 한 글자는 무조건 내용이다
            continue;
        }
        if (c == '"') {
            *at  = static_cast<uint32_t>(start);
            *len = static_cast<uint32_t>(pos_ - start);
            ++pos_;
            return true;
        }
        ++pos_;
    }
    setError("문자열이 닫히지 않았다");
    return false;
}

// 정수만. **소수점·지수를 만나면 실패시킨다** (파일 머리 참조).
inline bool Doc::parseNumber(int64_t* out) {
    const size_t start = pos_;
    bool neg = false;
    if (pos_ < len_ && text_[pos_] == '-') { neg = true; ++pos_; }
    if (pos_ >= len_ || text_[pos_] < '0' || text_[pos_] > '9') {
        setError("숫자가 와야 한다");
        return false;
    }
    int64_t v = 0;
    while (pos_ < len_ && text_[pos_] >= '0' && text_[pos_] <= '9') {
        const int32_t d = text_[pos_] - '0';
        // **오버플로는 UB다** — 곱하기 전에 막는다.
        if (v > (INT64_MAX - d) / 10) { setError("정수 범위를 넘는다"); return false; }
        v = v * 10 + d;
        ++pos_;
    }
    if (pos_ < len_) {
        const char c = text_[pos_];
        if (c == '.' || c == 'e' || c == 'E') {
            // 데이터 규약 위반. 여기서 죽이는 것이 이 파서의 존재 이유 중 하나다.
            pos_ = start;
            setError("소수는 담을 수 없다 — 정수 + 단위 접미사로 적을 것"
                     " (_permille · _ticks · _millitile)");
            return false;
        }
    }
    *out = neg ? -v : v;
    return true;
}

inline uint32_t Doc::parseValue(int32_t depth) {
    if (depth > MAX_DEPTH) { setError("중첩이 너무 깊다"); return 0; }
    skipWs();
    if (pos_ >= len_) { setError("값이 와야 하는데 끝났다"); return 0; }

    const uint32_t self = static_cast<uint32_t>(nodes_.size());
    nodes_.push_back(Node{});

    const char c = text_[pos_];
    if (c == '{' || c == '[') {
        const bool obj = (c == '{');
        nodes_[self].type = obj ? Type::Object : Type::Array;
        ++pos_;
        skipWs();
        if (pos_ < len_ && text_[pos_] == (obj ? '}' : ']')) { ++pos_; return self; }

        uint32_t prev = 0;
        for (;;) {
            skipWs();
            uint32_t keyAt = 0, keyLen = 0;
            if (obj) {
                if (!parseString(&keyAt, &keyLen)) return 0;
                skipWs();
                if (pos_ >= len_ || text_[pos_] != ':') { setError("':'가 와야 한다"); return 0; }
                ++pos_;
            }
            const uint32_t kid = parseValue(depth + 1);
            if (kid == 0) return 0;
            nodes_[kid].keyAt  = keyAt;
            nodes_[kid].keyLen = keyLen;
            if (prev == 0) nodes_[self].firstKid = kid;
            else           nodes_[prev].nextSib  = kid;
            prev = kid;
            ++nodes_[self].kidCount;

            skipWs();
            if (pos_ < len_ && text_[pos_] == ',') { ++pos_; continue; }
            if (pos_ < len_ && text_[pos_] == (obj ? '}' : ']')) { ++pos_; return self; }
            setError(obj ? "',' 또는 '}'가 와야 한다" : "',' 또는 ']'가 와야 한다");
            return 0;
        }
    }
    if (c == '"') {
        nodes_[self].type = Type::String;
        if (!parseString(&nodes_[self].textAt, &nodes_[self].textLen)) return 0;
        return self;
    }
    if (c == 't' || c == 'f') {
        nodes_[self].type = Type::Bool;
        if (literal("true"))  { nodes_[self].num = 1; return self; }
        if (literal("false")) { nodes_[self].num = 0; return self; }
        setError("true/false가 와야 한다");
        return 0;
    }
    if (c == 'n') {
        nodes_[self].type = Type::Null;
        if (literal("null")) return self;
        setError("null이 와야 한다");
        return 0;
    }
    nodes_[self].type = Type::Int;
    if (!parseNumber(&nodes_[self].num)) return 0;
    return self;
}

inline bool Doc::parse(const char* text, size_t len) {
    text_   = text;
    len_    = len;
    pos_    = 0;
    err_    = nullptr;
    errKey_ = nullptr;
    errOff_ = 0;
    nodes_.clear();
    nodes_.push_back(Node{});     // 0번은 "없음" 자리 — 인덱스 0을 널로 쓸 수 있게 비워 둔다

    if (text == nullptr) { setError("입력이 널이다"); return false; }
    if (parseValue(0) == 0) return false;

    skipWs();
    if (pos_ != len_) { setError("문서 뒤에 남은 것이 있다"); return false; }
    return true;
}

}  // namespace dc::json

#endif  // DC_JSON_H
