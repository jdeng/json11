/* json11
 *
 * json11 is a tiny JSON library for C++11, providing JSON parsing and serialization.
 *
 * The core Object provided by the library is json11::Value. A Json Object represents any JSON
 * value: null, bool, number (int or double), string (std::string), Array (std::vector), or
 * Object (std::map).
 *
 * Json Objects act like values: they can be assigned, copied, moved, compared for equality or
 * order, etc. There are also helper methods Value::to_string, to serialize a Value to a string, and
 * Value::parse (static) to parse a std::string as a Json Object.
 *
 * Internally, the various types of Json Object are represented by the JsonValue class
 * hierarchy.
 *
 * A note on numbers - JSON specifies the syntax of number formatting but not its semantics,
 * so some JSON implementations distinguish between integers and floating-point numbers, while
 * some don't. In json11, we choose the latter. Because some JSON implementations (namely
 * Javascript itself) treat all numbers as the same type, distinguishing the two leads
 * to JSON that will be *silently* changed by a round-trip through those implementations.
 * Dangerous! To avoid that risk, json11 stores all numbers as double internally, but also
 * provides integer helpers.
 *
 * Fortunately, double-precision IEEE754 ('double') can precisely store any integer in the
 * range +/-2^53, which includes every 'int' on most systems. (Timestamps often use int64
 * or long long to avoid the Y2038K problem; a double storing microseconds since some epoch
 * will be exact for +/- 275 years.)
 */

/* Copyright (c) 2013 Dropbox, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <initializer_list>

namespace json11 {

class Value;
typedef std::vector<Value> Array;
typedef std::map<std::string, Value> Object;

class Value final {
public:
    // Types
    enum Type {
        NUL, INTEGER, NUMBER, BOOL, STRING, ARRAY, OBJECT
    };

    // Constructors for the various types of JSON value.
    Value() noexcept :type_(NUL) {}
    Value(std::nullptr_t) noexcept :type_(NUL) {}
    Value(int value): type_(INTEGER), value_(value) {}
    Value(long value): type_(INTEGER), value_(value) {}
    Value(int64_t value): type_(INTEGER), value_(value) {}
    Value(double value): type_(NUMBER), value_(value) {}
    Value(bool value): type_(BOOL), value_(value) {}
    Value(const std::string &value): type_(STRING), value_(new std::string(value)) {}
    Value(std::string &&value): type_(STRING), value_(new std::string(std::move(value))) {}
    Value(const char * value): type_(STRING), value_(new std::string(value)) {}
    Value(const Array &values): type_(ARRAY), value_(new Array(values)) {}
    Value(Array &&values): type_(ARRAY), value_(new Array(std::move(values))) {}
    Value(const Object &values): type_(OBJECT), value_(new Object(values)) {}
    Value(Object &&values): type_(OBJECT), value_(new Object(std::move(values))) {}

    Value(const Value& o) { copy(o); }
    const Value& operator =(const Value& o) { clear(); copy(o); return *this; }

    Value(Value&& o) noexcept: type_(o.type_), value_(o.value_) { o.type_ = NUL; o.value_ = ValueHolder(); }
    const Value& operator =(Value&& o) noexcept {
      type_ = o.type_; value_ = o.value_;
      o.type_ = NUL; o.value_ = ValueHolder();
      return *this;
    } 

    ~Value() { clear(); }

    // Implicit constructor: anything with a to_json() function.
    template <class T, class = decltype(&T::to_json)>
    Value(const T & t) : Value(t.to_json()) {}

    // Implicit constructor: map-like Objects (std::map, std::unordered_map, etc)
    template <class M, typename std::enable_if<
        std::is_constructible<std::string, typename M::key_type>::value
        && std::is_constructible<Value, typename M::mapped_type>::value,
            int>::type = 0>
    Value(const M & m) : Value(Object(m.begin(), m.end())) {}

    // Implicit constructor: vector-like Objects (std::list, std::vector, std::set, etc)
    template <class V, typename std::enable_if<
        std::is_constructible<Value, typename V::value_type>::value,
            int>::type = 0>
    Value(const V & v) : Value(Array(v.begin(), v.end())) {}

    // This prevents Value(some_pointer) from accidentally producing a bool. Use
    // Value(bool(some_pointer)) if that behavior is desired.
    Value(void *) = delete;

    // Accessors
    Type type() const { return type_; }

    bool is_null()   const { return type() == NUL; }
    bool is_number() const { return type() == NUMBER || type() == INTEGER; }
    bool is_bool()   const { return type() == BOOL; }
    bool is_string() const { return type() == STRING; }
    bool is_array()  const { return type() == ARRAY; }
    bool is_object() const { return type() == OBJECT; }

    // Return the enclosed value if this is a number, 0 otherwise. Note that json11 does not
    // distinguish between integer and non-integer numbers - number_value() and int_value()
    // can both be applied to a NUMBER-typed Object.
    double number_value() const {
      if (type_ == INTEGER) return value_.l_;
      else if (type_ == NUMBER) return value_.d_;
      return 0.0;
    }

    long int_value() const {
      if (type_ == INTEGER) return value_.l_;
      else if (type_ == NUMBER) return (long)value_.d_;
      return 0;
    }

    // Return the enclosed value if this is a boolean, false otherwise.
    bool bool_value() const {
      return (type_ == BOOL? value_.b_: false);
    }

    // Return the enclosed string if this is a string, "" otherwise.
    const std::string &string_value() const {
      static std::string empty_string;
      return type_ == STRING && value_.s_? *value_.s_: empty_string;
    }

    // Return the enclosed std::vector if this is an Array, or an empty vector otherwise.
    const Array &array_value() const {
      static Array empty_array;
      return type_ == ARRAY && value_.a_? *value_.a_: empty_array;
    }
    // Return the enclosed std::map if this is an Object, or an empty map otherwise.
    const Object &object_value() const {
      static Object empty_object;
      return type_ == OBJECT && value_.o_? *value_.o_: empty_object;
    }

    // Return a reference to arr[i] if this is an Array, Value() otherwise.
    const Value & operator[](size_t i) const {
      static Value empty_object;
      return type_ == ARRAY && value_.a_ && i < value_.a_->size()? (*value_.a_)[i]: empty_object;
    }
    // Return a reference to obj[key] if this is an Object, Value() otherwise.
    const Value & operator[](const std::string &key) const {
      static Value empty_object;
      if (type_ != OBJECT || !value_.o_) return empty_object;
      auto it = value_.o_->find(key);
      return it == value_.o_->end()? empty_object: it->second;
    }

    Value& operator[](const std::string &key) {
      if (type_ == NUL) { 
        type_ = OBJECT; value_.o_ = new Object();
      }
      //XXX: assert(value_.o_);
      return (*value_.o_)[key];
    }

    size_t size() const {
      return type_ == ARRAY && value_.a_? value_.a_->size(): 0;
    }

    bool append(Value&& o) { 
      if (type_ == NUL) {
        type_ = ARRAY; value_.a_ = new Array{std::move(o)}; 
        return true;
      }
      if (type_ != ARRAY) return false;
      value_.a_->push_back(o);
      return true;
    }
    bool append(const Value& o) { return append(Value(o));}

    void to_string(std::string &out) const {
      switch(type_) {
      case NUL: out += "null"; break;
      case NUMBER: out += std::to_string(value_.d_); break;
      case INTEGER: out += std::to_string(value_.l_); break;
      case BOOL: out += value_.b_? "true":"false"; break;
      case STRING: escape(*value_.s_, out); break;
      case ARRAY: to_string(*value_.a_, out); break;
      case OBJECT: to_string(*value_.o_, out); break;
      }
    }

    static std::string escape(const std::string& value) { std::string out; escape(value, out); return out; }
    std::string to_string() const { std::string out; to_string(out); return out; }

    bool operator== (const Value &rhs) const {
      if (is_number() && rhs.is_number())
        return number_value() == rhs.number_value();
      if (type_ != rhs.type_) return false;
      switch(type_) {
      case NUL: return true;
//      case NUMBER: return value_.d_ == rhs.value_.d_;
//      case INTEGER: return value_.l_ == rhs.value_.l_;
      case BOOL: return value_.b_ == rhs.value_.b_;
      case STRING: return *value_.s_ == *rhs.value_.s_;
      case ARRAY: return *value_.a_ == *rhs.value_.a_;
      case OBJECT: return *value_.o_ == *rhs.value_.o_;
      default: break;
      }
      return false;
    }

    bool operator<  (const Value &rhs) const {
      if (is_number() && rhs.is_number())
        return number_value() < rhs.number_value();

      if (type_ != rhs.type_) return type_ < rhs.type_;
      switch(type_) {
      case NUL: return false;
//      case NUMBER: return value_.d_ < rhs.value_.d_;
//      case INTEGER: return value_.l_ < rhs.value_.l_;
      case BOOL: return value_.b_ < rhs.value_.b_;
      case STRING: return *value_.s_ < *rhs.value_.s_;
      case ARRAY: return *value_.a_ < *rhs.value_.a_;
      case OBJECT: return *value_.o_ < *rhs.value_.o_;
      default: break;
      }
      return false;
    }

    bool operator!= (const Value &rhs) const { return !(*this == rhs); }
    bool operator<= (const Value &rhs) const { return !(rhs < *this); }
    bool operator>  (const Value &rhs) const { return  (rhs < *this); }
    bool operator>= (const Value &rhs) const { return !(*this < rhs); }

    /* has_shape(types, err)
     *
     * Return true if this is a JSON Object and, for each item in types, has a field of
     * the given type. If not, return false and set err to a descriptive message.
     */
    typedef std::initializer_list<std::pair<std::string, Type>> Shape;
    bool has_shape(const Shape & types, std::string & err) const {
      if (!is_object()) {
        err = "expected JSON object, got " + to_string();
        return false;
      }
      for (auto & item : types) {
        if ((*this)[item.first].type() != item.second) {
            err = "bad type for " + item.first + " in " + to_string();
            return false;
        }
      }
      return true;
    }

private:
    Type type_;
    
    union ValueHolder {
      bool b_;
      long l_;
      double d_;
      std::string *s_;
      Array *a_;
      Object *o_;    

      ValueHolder() = default;
      ValueHolder(const ValueHolder&) = default;
      ValueHolder(bool v): b_(v) {}
      ValueHolder(int v): l_(v) {}
      ValueHolder(long v): l_(v) {}
      ValueHolder(int64_t v): l_(v) {}
      ValueHolder(double v): d_(v) {}
      ValueHolder(std::string *v): s_(v) {}
      ValueHolder(Array *v): a_(v) {}
      ValueHolder(Object *v): o_(v) {}
    } value_;

  void clear() {
    if (type_ == STRING) delete value_.s_;
    else if (type_ == ARRAY) delete value_.a_;
    else if (type_ == OBJECT) delete value_.o_;
    type_ = NUL; value_ = ValueHolder();
  }

  void copy(const Value& o) {
    type_ = o.type_; value_ = o.value_;
    if (type_ == STRING) value_.s_ = new std::string(*value_.s_);
    else if (type_ == ARRAY) value_.a_ = new Array(*value_.a_);
    else if (type_ == OBJECT) value_.o_ = new Object(*value_.o_);
  }

    // Serialize.
  static void escape(const std::string &value, std::string &out) {
      out += '"';
      for (size_t i = 0; i < value.length(); i++) {
        const char ch = value[i];
        if (ch == '\\') {
            out += "\\\\";
        } else if (ch == '"') {
            out += "\\\"";
        } else if (ch == '\b') {
            out += "\\b";
        } else if (ch == '\f') {
            out += "\\f";
        } else if (ch == '\n') {
            out += "\\n";
        } else if (ch == '\r') {
            out += "\\r";
        } else if (ch == '\t') {
            out += "\\t";
        } else if ((uint8_t)ch <= 0x1f) {
            char buf[8];
            snprintf(buf, sizeof buf, "\\u%04x", ch);
            out += buf;
        } else if ((uint8_t)ch == 0xe2 && (uint8_t)value[i+1] == 0x80
                   && (uint8_t)value[i+2] == 0xa8) {
            out += "\\u2028";
            i += 2;
        } else if ((uint8_t)ch == 0xe2 && (uint8_t)value[i+1] == 0x80
                   && (uint8_t)value[i+2] == 0xa9) {
            out += "\\u2029";
            i += 2;
        } else {
            out += ch;
        }
      }
      out += '"';
  }

  static void to_string(const Array &values, std::string &out) {
      bool first = true;
      out += "[";
      for (const auto &value : values) {
        if (!first)
            out += ", ";
        value.to_string(out);
        first = false;
      }
      out += "]";
  }

  static void to_string(const Object &values, std::string &out) {
      bool first = true;
      out += "{";
      for (const auto &kv : values) {
        if (!first) out += ", ";
        escape(kv.first, out);
        out += ": ";
        kv.second.to_string(out);
        first = false;
      }
      out += "}";
  }
};

// Parse. If parse fails, return Value() and assign an error message to err.
Value parse(const std::string & in, std::string & err);

// Parse multiple Objects, concatenated or separated by whitespace
std::vector<Value> parse_multi(const std::string & in, std::string & err);

inline Value parse(const std::string& content) { std::string err; return parse(content, err); }

} // namespace json11
