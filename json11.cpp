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

#include "json11.h"
#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <limits>
#include <type_traits>

namespace json11 {

static const int max_depth = 200;

// Check that Value has the properties we want.
#define CHECK_TRAIT(x) static_assert(std::x::value, #x)
CHECK_TRAIT(is_nothrow_constructible<Value>);
CHECK_TRAIT(is_nothrow_default_constructible<Value>);
CHECK_TRAIT(is_copy_constructible<Value>);
CHECK_TRAIT(is_nothrow_move_constructible<Value>);
CHECK_TRAIT(is_copy_assignable<Value>);
CHECK_TRAIT(is_nothrow_move_assignable<Value>);
CHECK_TRAIT(is_nothrow_destructible<Value>);

/* * * * * * * * * * * * * * * * * * * *
 * Parsing
 */

/* esc(c)
 *
 * Format char c suitable for printing in an error message.
 */
static inline std::string esc(char c) {
    char buf[12];
    if ((uint8_t)c >= 0x20 && (uint8_t)c <= 0x7f) {
        snprintf(buf, sizeof buf, "'%c' (%d)", c, c);
    } else {
        snprintf(buf, sizeof buf, "(%d)", c);
    }
    return std::string(buf);
}

static inline bool in_range(long x, long lower, long upper) {
    return (x >= lower && x <= upper);
}

/* JsonParser
 *
 * Object that tracks all state of an in-progress parse.
 */
struct JsonParser {

    /* State
     */
    const std::string &str;
    size_t i;
    std::string &err;
    bool failed;

    /* fail(msg, err_ret = Value())
     *
     * Mark this parse as failed.
     */
    Value fail(std::string &&msg) {
        return fail(std::move(msg), Value());
    }

    template <typename T>
    T fail(std::string &&msg, const T err_ret) {
        if (!failed)
            err = std::move(msg);
        failed = true;
        return err_ret;
    }

    /* consume_whitespace()
     *
     * Advance until the current character is non-whitespace.
     */
    void consume_whitespace() {
        while (str[i] == ' ' || str[i] == '\r' || str[i] == '\n' || str[i] == '\t')
            i++;
    }

    /* get_next_token()
     *
     * Return the next non-whitespace character. If the end of the input is reached,
     * flag an error and return 0.
     */
    char get_next_token() {
        consume_whitespace();
        if (i == str.size())
            return fail("unexpected end of input", 0);

        return str[i++];
    }

    /* encode_utf8(pt, out)
     *
     * Encode pt as UTF-8 and add it to out.
     */
    void encode_utf8(long pt, std::string & out) {
        if (pt < 0)
            return;

        if (pt < 0x80) {
            out += pt;
        } else if (pt < 0x800) {
            out += (pt >> 6) | 0xC0;
            out += (pt & 0x3F) | 0x80;
        } else if (pt < 0x10000) {
            out += (pt >> 12) | 0xE0;
            out += ((pt >> 6) & 0x3F) | 0x80;
            out += (pt & 0x3F) | 0x80;
        } else {
            out += (pt >> 18) | 0xF0;
            out += ((pt >> 12) & 0x3F) | 0x80;
            out += ((pt >> 6) & 0x3F) | 0x80;
            out += (pt & 0x3F) | 0x80;
        }
    }

    /* parse_string()
     *
     * Parse a std::string, starting at the current position.
     */
    std::string parse_string() {
        std::string out;
        long last_escaped_codepoint = -1;
        while (true) {
            if (i == str.size())
                return fail("unexpected end of input in std::string", "");

            char ch = str[i++];

            if (ch == '"') {
                encode_utf8(last_escaped_codepoint, out);
                return out;
            }

            if (in_range(ch, 0, 0x1f))
                return fail("unescaped " + esc(ch) + " in std::string", "");

            // The usual case: non-escaped characters
            if (ch != '\\') {
                encode_utf8(last_escaped_codepoint, out);
                last_escaped_codepoint = -1;
                out += ch;
                continue;
            }

            // Handle escapes
            if (i == str.size())
                return fail("unexpected end of input in std::string", "");

            ch = str[i++];

            if (ch == 'u') {
                // Extract 4-byte escape sequence
                std::string esc = str.substr(i, 4);
                // Explicitly check length of the substring. The following loop
                // relies on std::string returning the terminating NUL when
                // accessing str[length]. Checking here reduces brittleness.
                if (esc.length() < 4) {
                    return fail("bad \\u escape: " + esc, "");
                }

                for (int j = 0; j < 4; j++) {
                    if (!in_range(esc[j], 'a', 'f') && !in_range(esc[j], 'A', 'F')
                            && !in_range(esc[j], '0', '9'))
                        return fail("bad \\u escape: " + esc, "");
                }

                long codepoint = strtol(esc.data(), nullptr, 16);

                // JSON specifies that characters outside the BMP shall be encoded as a pair
                // of 4-hex-digit \u escapes encoding their surrogate pair components. Check
                // whether we're in the middle of such a beast: the previous codepoint was an
                // escaped lead (high) surrogate, and this is a trail (low) surrogate.
                if (in_range(last_escaped_codepoint, 0xD800, 0xDBFF)
                        && in_range(codepoint, 0xDC00, 0xDFFF)) {
                    // Reassemble the two surrogate pairs into one astral-plane character, per
                    // the UTF-16 algorithm.
                    encode_utf8((((last_escaped_codepoint - 0xD800) << 10)
                                 | (codepoint - 0xDC00)) + 0x10000, out);
                    last_escaped_codepoint = -1;
                } else {
                    encode_utf8(last_escaped_codepoint, out);
                    last_escaped_codepoint = codepoint;
                }

                i += 4;
                continue;
            }

            encode_utf8(last_escaped_codepoint, out);
            last_escaped_codepoint = -1;

            if (ch == 'b') {
                out += '\b';
            } else if (ch == 'f') {
                out += '\f';
            } else if (ch == 'n') {
                out += '\n';
            } else if (ch == 'r') {
                out += '\r';
            } else if (ch == 't') {
                out += '\t';
            } else if (ch == '"' || ch == '\\' || ch == '/') {
                out += ch;
            } else {
                return fail("invalid escape character " + esc(ch), "");
            }
        }
    }

    /* parse_number()
     *
     * Parse a double.
     */
    Value parse_number() {
        size_t start_pos = i;

        if (str[i] == '-')
            i++;

        // Integer part
        if (str[i] == '0') {
            i++;
            if (in_range(str[i], '0', '9'))
                return fail("leading 0s not permitted in numbers");
        } else if (in_range(str[i], '1', '9')) {
            i++;
            while (in_range(str[i], '0', '9'))
                i++;
        } else {
            return fail("invalid " + esc(str[i]) + " in number");
        }

        if (str[i] != '.' && str[i] != 'e' && str[i] != 'E'
                && (i - start_pos) <= (size_t)std::numeric_limits<int>::digits10) {
            return std::atol(str.c_str() + start_pos);
        }

        // Decimal part
        if (str[i] == '.') {
            i++;
            if (!in_range(str[i], '0', '9'))
                return fail("at least one digit required in fractional part");

            while (in_range(str[i], '0', '9'))
                i++;
        }

        // Exponent part
        if (str[i] == 'e' || str[i] == 'E') {
            i++;

            if (str[i] == '+' || str[i] == '-')
                i++;

            if (!in_range(str[i], '0', '9'))
                return fail("at least one digit required in exponent");

            while (in_range(str[i], '0', '9'))
                i++;
        }

        return std::strtod(str.c_str() + start_pos, nullptr);
    }

    /* expect(str, res)
     *
     * Expect that 'str' starts at the character that was just read. If it does, advance
     * the input and return res. If not, flag an error.
     */
    Value expect(const std::string &expected, Value res) {
        assert(i != 0);
        i--;
        if (str.compare(i, expected.length(), expected) == 0) {
            i += expected.length();
            return res;
        } else {
            return fail("parse error: expected " + expected + ", got " + str.substr(i, expected.length()));
        }
    }

    /* parse_json()
     *
     * Parse a JSON object.
     */
    Value parse_json(int depth) {
        if (depth > max_depth) {
            return fail("exceeded maximum nesting depth");
        }

        char ch = get_next_token();
        if (failed)
            return Value();

        if (ch == '-' || (ch >= '0' && ch <= '9')) {
            i--;
            return parse_number();
        }

        if (ch == 't')
            return expect("true", true);

        if (ch == 'f')
            return expect("false", false);

        if (ch == 'n')
            return expect("null", Value());

        if (ch == '"')
            return parse_string();

        if (ch == '{') {
            std::map<std::string, Value> data;
            ch = get_next_token();
            if (ch == '}')
                return std::move(data);

            while (1) {
                if (ch != '"')
                    return fail("expected '\"' in object, got " + esc(ch));

                std::string key = parse_string();
                if (failed)
                    return Value();

                ch = get_next_token();
                if (ch != ':')
                    return fail("expected ':' in object, got " + esc(ch));

                data[std::move(key)] = parse_json(depth + 1);
                if (failed)
                    return Value();

                ch = get_next_token();
                if (ch == '}')
                    break;
                if (ch != ',')
                    return fail("expected ',' in object, got " + esc(ch));

                ch = get_next_token();
            }
            return std::move(data);
        }

        if (ch == '[') {
            std::vector<Value> data;
            ch = get_next_token();
            if (ch == ']')
                return std::move(data);

            while (1) {
                i--;
                data.emplace_back(parse_json(depth + 1));
                if (failed)
                    return Value();

                ch = get_next_token();
                if (ch == ']')
                    break;
                if (ch != ',')
                    return fail("expected ',' in list, got " + esc(ch));

                ch = get_next_token();
                (void)ch;
            }
            return std::move(data);
        }

        return fail("expected value, got " + esc(ch));
    }
};

Value parse(const std::string &in, std::string &err) {
    JsonParser parser { in, 0, err, false };
    Value result = parser.parse_json(0);

    // Check for any trailing garbage
    parser.consume_whitespace();
    if (parser.i != in.size())
        return parser.fail("unexpected trailing " + esc(in[parser.i]));

    return result;
}

// Documented in json11.hpp
std::vector<Value> parse_multi(const std::string &in, std::string &err) {
    JsonParser parser { in, 0, err, false };

    std::vector<Value> vals;
    while (parser.i != in.size() && !parser.failed) {
        vals.push_back(parser.parse_json(0));
        // Check for another object
        parser.consume_whitespace();

    }
    return vals;
}

} // namespace json11

