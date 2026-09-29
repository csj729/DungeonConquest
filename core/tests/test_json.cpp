#include <cstring>
#include <string>

// 정수 전용 JSON 파서.
//
// **가장 중요한 절은 "소수를 거부한다"와 "없는 키가 조용히 0이 되지 않는다"** 둘이다.
// 둘 다 이 저장소가 반복해서 물린 고장 방식(빨간불이 아니라 아무것도 안 보는
// 초록불)을 로더 단계에서 막는 장치이고, 나머지 절은 그 둘을 떠받친다.
#include "dc/json.h"
#include "test_main.h"

using namespace dc;

static json::Doc parseOk(const char* s) {
    json::Doc d;
    const bool ok = d.parse(s, std::strlen(s));
    if (!ok) printf("    (파싱 실패: %s @%zu)\n", d.error(), d.errorOffset());
    CHECK(ok);
    return d;
}

static bool parseFails(const char* s) {
    json::Doc d;
    return !d.parse(s, std::strlen(s));
}

int main() {
    printf("=== JSON (정수 전용) ===\n");

    dctest::section("스칼라");
    {
        json::Doc d = parseOk("42");
        CHECK(d.root().isInt());
        CHECK_EQ(d.root().asInt(), 42);
        CHECK(d.ok());
    }
    {
        json::Doc d = parseOk("-7");
        CHECK_EQ(d.root().asInt(), -7);
    }
    {
        json::Doc d = parseOk("true");
        CHECK(d.root().asBool());
        CHECK(d.ok());
    }
    {
        json::Doc d = parseOk("null");
        CHECK(d.root().type() == json::Type::Null);
    }

    dctest::section("**소수를 거부한다** — 규약을 문서가 아니라 로더가 강제한다");
    {
        // JSON 숫자는 명세상 부동소수점이고, 그건 곧 파서·플랫폼에 따라 값이
        // 갈리는 서버-클라 불일치 경로다. 조용히 반올림하는 대신 로드를 죽인다.
        CHECK(parseFails("1.5"));
        CHECK(parseFails("0.0"));
        CHECK(parseFails("[1, 2.5, 3]"));
        CHECK(parseFails("{\"a\": 1e3}"));
        CHECK(parseFails("{\"a\": 1E3}"));
        CHECK(parseFails("-0.1"));
        // 정수는 당연히 통과한다
        CHECK(!parseFails("[1, 2, 3]"));
        CHECK(!parseFails("{\"a\": -1000}"));

        json::Doc d;
        d.parse("1.5", 3);
        CHECK(std::strstr(d.error(), "정수") != nullptr);   // 이유를 말해 준다
    }

    dctest::section("**없는 키가 0이 되지 않는다** — 오타가 통과하면 안 된다");
    {
        json::Doc d = parseOk("{\"hp\": 20}");
        CHECK(d.ok());
        CHECK_EQ(d.root()["hp"].asInt(), 20);
        CHECK(d.ok());                       // 여기까진 멀쩡하다

        const int64_t v = d.root()["hpp"].asInt();   // 오타
        CHECK_EQ(v, 0);                      // 값은 0이지만
        CHECK(!d.ok());                      // **문서가 실패 상태가 된다**
        CHECK(std::strcmp(d.errorKey(), "hpp") == 0);   // 어느 키인지 말해 준다
    }
    {
        // has()는 오류를 기록하지 않는다 — 선택적 키를 볼 때 쓴다
        json::Doc d = parseOk("{\"a\": 1}");
        CHECK(d.root().has("a"));
        CHECK(!d.root().has("b"));
        CHECK(d.ok());
    }
    {
        // 첫 오류가 남는다 — 뒤따르는 실패는 그 여파라 덮어쓰면 원인이 사라진다
        json::Doc d = parseOk("{\"a\": 1}");
        (void)d.root()["first"].asInt();
        (void)d.root()["second"].asInt();
        CHECK(std::strcmp(d.errorKey(), "first") == 0);
    }

    dctest::section("타입이 다르면 오류다");
    {
        json::Doc d = parseOk("{\"s\": \"text\", \"a\": [1], \"o\": {}}");
        (void)d.root()["s"].asInt();
        CHECK(!d.ok());
    }
    {
        json::Doc d = parseOk("{\"n\": 5}");
        (void)d.root()["n"].at(0);           // 배열이 아니다
        CHECK(!d.ok());
    }
    {
        json::Doc d = parseOk("[1, 2]");
        (void)d.root().at(5);                // 범위 밖
        CHECK(!d.ok());
    }
    {
        // int32 범위 — SimConfig가 대부분 int32라 경계를 막아 둔다
        json::Doc d = parseOk("{\"big\": 3000000000}");
        CHECK_EQ(d.root()["big"].asInt(), 3000000000LL);   // int64로는 멀쩡하고
        CHECK(d.ok());
        (void)d.root()["big"].asI32();                     // int32로는 오류다
        CHECK(!d.ok());
    }

    dctest::section("배열 · 오브젝트 · 중첩");
    {
        json::Doc d = parseOk("[10, 20, 30]");
        CHECK(d.root().isArray());
        CHECK_EQ(d.root().size(), 3);
        CHECK_EQ(d.root().at(0).asInt(), 10);
        CHECK_EQ(d.root().at(2).asInt(), 30);
        CHECK(d.ok());
    }
    {
        json::Doc d = parseOk("{\"a\": 1, \"b\": {\"c\": [2, {\"d\": 3}]}}");
        CHECK_EQ(d.root()["a"].asInt(), 1);
        CHECK_EQ(d.root()["b"]["c"].at(0).asInt(), 2);
        CHECK_EQ(d.root()["b"]["c"].at(1)["d"].asInt(), 3);
        CHECK(d.ok());
    }
    {
        json::Doc d = parseOk("{\"e\": [], \"o\": {}}");
        CHECK_EQ(d.root()["e"].size(), 0);
        CHECK_EQ(d.root()["o"].size(), 0);
        CHECK(d.ok());
    }
    {
        // 공백·개행이 섞여도 같다
        json::Doc d = parseOk("{\n  \"a\" : [ 1 ,\t2 ]\r\n}");
        CHECK_EQ(d.root()["a"].at(1).asInt(), 2);
        CHECK(d.ok());
    }

    dctest::section("문자열 — 복사하지 않고 원문을 가리킨다");
    {
        json::Doc d = parseOk("{\"id\": \"GB_WARCHIEF\"}");
        CHECK(d.root()["id"].strEquals("GB_WARCHIEF"));
        CHECK(!d.root()["id"].strEquals("GB_WARCHIE"));    // 접두사는 다르다
        CHECK(!d.root()["id"].strEquals("GB_WARCHIEFX"));
        CHECK(d.ok());
    }
    {
        // 이스케이프는 해석하지 않고 건너뛰기만 한다 — 값을 쓰지 않기 때문이다.
        // 중요한 건 `\"`가 문자열을 끝내지 않는다는 것뿐이다.
        json::Doc d = parseOk("{\"a\": \"x\\\"y\", \"b\": 7}");
        CHECK_EQ(d.root()["b"].asInt(), 7);    // 뒤 키를 제대로 찾는다
        CHECK(d.ok());
    }
    {
        // data/*.json의 `_` 주석 키에는 개행 이스케이프가 잔뜩 들어 있다
        json::Doc d = parseOk("{\"_note\": \"첫 줄\\n둘째 줄\", \"v\": 3}");
        CHECK_EQ(d.root()["v"].asInt(), 3);
        CHECK(d.ok());
    }

    dctest::section("깨진 입력은 크래시가 아니라 실패다");
    {
        CHECK(parseFails(""));
        CHECK(parseFails("{"));
        CHECK(parseFails("["));
        CHECK(parseFails("{\"a\"}"));           // ':' 없음
        CHECK(parseFails("{\"a\": }"));
        CHECK(parseFails("[1, ]"));
        CHECK(parseFails("{\"a\": 1,}"));
        CHECK(parseFails("\"닫히지 않은"));
        CHECK(parseFails("tru"));
        CHECK(parseFails("nul"));
        CHECK(parseFails("{} {}"));             // 뒤에 남은 것
        CHECK(parseFails("1 2"));
        CHECK(parseFails("-"));
        CHECK(parseFails("--1"));

        json::Doc d;
        CHECK(!d.parse(nullptr, 0));
    }
    {
        // 중첩 상한 — 손상된 입력이 스택을 넘기지 못한다
        std::string deep;
        for (int i = 0; i < 200; ++i) deep += "[";
        for (int i = 0; i < 200; ++i) deep += "]";
        CHECK(parseFails(deep.c_str()));
    }
    {
        // 정수 오버플로는 UB다 — 넘기기 전에 실패시킨다
        CHECK(parseFails("99999999999999999999999"));
    }

    dctest::section("실제 데이터 모양 — data/*.json이 쓰는 형태");
    {
        // monsters.json의 boss.patterns를 줄인 것. `_` 주석 키가 섞여 있다.
        const char* src =
            "{\n"
            "  \"_patterns\": \"고정 순환이다\\n랜덤이 아니다\",\n"
            "  \"patterns\": [\n"
            "    {\"name\": \"삼연격\", \"hits\": 3, \"damage\": 63,"
            " \"windup_ticks\": 0, \"cooldown_ticks\": 80},\n"
            "    {\"name\": \"대곤봉 강타\", \"hits\": 1, \"damage\": 293,"
            " \"windup_ticks\": 80, \"cooldown_ticks\": 120}\n"
            "  ],\n"
            "  \"phase2_at_permille\": 500\n"
            "}";
        json::Doc d = parseOk(src);
        const json::Value ps = d.root()["patterns"];
        CHECK_EQ(ps.size(), 2);
        CHECK(ps.at(0)["name"].strEquals("삼연격"));      // UTF-8이 그대로 통과한다
        CHECK_EQ(ps.at(0)["hits"].asInt() * ps.at(0)["damage"].asInt(), 189);
        CHECK_EQ(ps.at(1)["windup_ticks"].asI32(), 80);
        CHECK_EQ(d.root()["phase2_at_permille"].asI32(), 500);
        CHECK(d.ok());
    }

    return dctest::summary("test_json");
}
