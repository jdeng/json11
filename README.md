Changes
-----

 * Removed std::smart_ptr and virtual functions.
 * Renamed Json to Value, Json::object to Object, Json::array to Array, Json::dump to Value::dump, Json::parse to parse.


json11
------

json11 is a tiny JSON library for C++11, providing JSON parsing and serialization.

The core object provided by the library is json11::Json. A Json object represents any JSON
value: null, bool, number (int or double), string (std::string), array (std::vector), or
object (std::map).

Json objects act like values. They can be assigned, copied, moved, compared for equality or
order, and so on. There are also helper methods json11::Value::to_string, to serialize a Json to a string, and
json11::parse (static) to parse a std::string as a Json object.

It's easy to make a JSON object with C++11's new initializer syntax:

    Json my_json = json11::Object {
        { "key1", "value1" },
        { "key2", false },
        { "key3", json11::Array { 1, 2, 3 } },
    };
    std::string json_str = my_json.to_string();

There are also implicit constructors that allow standard and user-defined types to be
automatically converted to JSON. For example:

    class Point {
    public:
        int x;
        int y;
        Point (int x, int y) : x(x), y(y) {}
        Json to_json() const { return json11::Array { x, y }; }
    };

    std::vector<Point> points = { { 1, 2 }, { 10, 20 }, { 100, 200 } };
    std::string points_json = Json(points).to_string();

JSON values can have their values queried and inspected:

    Json json = json11::Array { json11::Object { { "k", "v" } } };
    std::string str = json[0]["k"].string_value();

More documentation is still to come. For now, see json11.hpp.
