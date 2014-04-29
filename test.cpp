#include <string>
#include <cstdio>
#include <iostream>
#include <sstream>
#include "json11.h"
#include <cassert>
#include <list>
#include <set>
#include <unordered_map>
#include <string.h>

using namespace json11;
using std::string;

void parse_from_stdin() {
    string buf;
    while (!std::cin.eof()) buf += std::cin.get();

    string err;
    auto json = parse(buf, err);
    if (!err.empty()) {
        printf("Failed: %s\n", err.c_str());
    } else {
        printf("Result: %s\n", json.to_string().c_str());
    }
}

int main(int argc, char **argv) {
    if (argc == 2 && argv[1] == string("--stdin")) {
        parse_from_stdin();
        return 0;
    }

    const string simple_test =
        R"({"k1":"v1", "k2":42, "k3":["a",123,true,false,null]})";

    string err;
    auto json = parse(simple_test, err);

    std::cout << "k1: " << json["k1"].string_value() << "\n";
    std::cout << "k3: " << json["k3"].to_string() << "\n";

    for (auto &k : json["k3"].array_items()) {
        std::cout << "    - " << k.to_string() << "\n";
    }

    std::list<int> l1 { 1, 2, 3 };
    std::vector<int> l2 { 1, 2, 3 };
    std::set<int> l3 { 1, 2, 3 };
    assert(Value(l1) == Value(l2));
    assert(Value(l2) == Value(l3));

    std::map<string, string> m1 { { "k1", "v1" }, { "k2", "v2" } };
    std::unordered_map<string, string> m2 { { "k1", "v1" }, { "k2", "v2" } };
    assert(Value(m1) == Value(m2));

    // Value literals
    Value obj = Object({
        { "k1", "v1" },
        { "k2", 42.0 },
        { "k3", Array({ "a", 123.0, true, false, nullptr }) },
    });

    std::cout << "obj: " << obj.to_string() << "\n";

    assert(Value("a").number_value() == 0);
    assert(Value("a").string_value() == "a");
    assert(Value().number_value() == 0);

    assert(obj == json);
    assert(Value(42) == Value(42.0));
    assert(Value(42) != Value(42.1));

    const string unicode_escape_test =
        R"([ "blah\ud83d\udca9blah\ud83dblah\udca9blah\u0000blah\u1234" ])";

    const char utf8[] = "blah" "\xf0\x9f\x92\xa9" "blah" "\xed\xa0\xbd" "blah"
                        "\xed\xb2\xa9" "blah" "\0" "blah" "\xe1\x88\xb4";

    const Value uni = parse(unicode_escape_test, err);
    assert(uni[0].string_value().size() == (sizeof utf8) - 1);
    assert(memcmp(uni[0].string_value().data(), utf8, sizeof utf8) == 0);

    Value my_json = Object {
        { "key1", "value1" },
        { "key2", false },
        { "key3", Array{ 1, 2, 3 } },
    };
    std::string json_str = my_json.to_string();
    printf("%s\n", json_str.c_str());

    my_json["key1"] = Array{1,2,3};
    my_json["key5"].append(1);
    my_json["key6"] = Object{{"t1", 1}, {"t2", "t3"}};
    my_json["key7"] = Object{{"t1", 1}};
    json_str = my_json.to_string();
    printf("%s\n", json_str.c_str());

    class Point {
    public:
        int x;
        int y;
        Point (int x, int y) : x(x), y(y) {}
        Value to_json() const { return Object { {"x", x}, {"y", y} }; }
    };

    std::vector<Point> points = { { 1, 2 }, { 10, 20 }, { 100, 200 } };
    std::string points_json = Value(points).to_string();
    printf("%s\n", points_json.c_str());
}
