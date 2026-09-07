#include <cstdio>
#include <fstream>

#include "../src/util/ini.h"
#include "../src/util/json.h"
#include "../src/util/math.h"
#include "../src/util/rng.h"
#include "../src/util/test.h"

using namespace rcai;

RCAI_TEST(json_roundtrip) {
    json::Value v;
    v.set("name", std::string("rcai"));
    v.set("n", 42);
    v.set("f", 1.5f);
    v.set("flag", true);
    json::Value arr;
    arr.push(json::Value(1));
    arr.push(json::Value(std::string("two")));
    v.set("arr", std::move(arr));
    const std::string text = v.dump(2);
    const json::Value back = json::Value::parse(text);
    CHECK_EQ(back.find("name")->asString(), std::string("rcai"));
    CHECK_EQ(back.find("n")->asInt(), 42);
    CHECK_NEAR(back.find("f")->asFloat(), 1.5f, 1e-6);
    CHECK_EQ(back.find("flag")->asBool(), true);
    CHECK_EQ(back.find("arr")->asArray().size(), 2u);
}

RCAI_TEST(json_escapes_and_errors) {
    const std::string s = json::Value::escape(std::string("a\"b\\c\nd"));
    CHECK(s.find("\\\"") != std::string::npos);
    bool threw = false;
    try { (void)json::Value::parse("{bad}"); } catch (...) { threw = true; }
    CHECK(threw);
    threw = false;
    try { (void)json::Value::parse("[1, 2] trailing"); } catch (...) { threw = true; }
    CHECK(threw);
}

RCAI_TEST(ini_roundtrip) {
    const std::string path = "test_ini_roundtrip.ini";
    {
        IniFile ini;
        ini.set("Papyrus", "fUpdateBudgetMS", "2.0000");
        ini.set("General", "uGridsToLoad", "5");
        ini.save(path);
    }
    const IniFile back = IniFile::load(path);
    CHECK_EQ(back.get("Papyrus", "fUpdateBudgetMS"), std::string("2.0000"));
    CHECK_EQ(back.getInt("General", "uGridsToLoad"), 5);
    CHECK(!back.has("General", "missing"));
    CHECK_EQ(back.get("General", "missing", "dflt"), std::string("dflt"));
    std::remove(path.c_str());
}

RCAI_TEST(ini_comments_and_update) {
    const std::string path = "test_ini_update.ini";
    {
        std::ofstream out(path);
        out << "; comment\n[Display]\nbUseTAA = 1\n[General]\nkey = old ; trailing\n";
    }
    IniFile ini = IniFile::load(path);
    CHECK_EQ(ini.get("Display", "bUseTAA"), std::string("1"));
    CHECK_EQ(ini.get("General", "key"), std::string("old"));
    ini.set("Display", "bUseTAA", "0");
    ini.save(path);
    const IniFile back = IniFile::load(path);
    CHECK_EQ(back.get("Display", "bUseTAA"), std::string("0"));
    std::remove(path.c_str());
}

RCAI_TEST(math_los) {
    // Crossing segments intersect; parallel ones do not.
    CHECK(segmentsIntersect({0, 0}, {10, 0}, {5, -1}, {5, 1}));
    CHECK(!segmentsIntersect({0, 0}, {10, 0}, {0, 1}, {10, 1}));
    CHECK_NEAR(length({3, 4}), 5.f, 1e-5f);
    CHECK_NEAR(dot(normalize({1, 1}), {1, 0}), 0.7071f, 1e-3f);
}

RCAI_TEST(rng_deterministic) {
    Rng a(123), b(123);
    for (int i = 0; i < 100; ++i) CHECK_EQ(a.nextU64(), b.nextU64());
    Rng c(124);
    CHECK(a.nextU64() != c.nextU64());
}
