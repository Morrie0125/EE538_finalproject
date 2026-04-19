#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "../include/demo_config.h"

using namespace std;

namespace {

struct JsonValue {
    enum class Type {
        Null,
        Bool,
        Number,
        String,
        Array,
        Object,
    };

    Type type = Type::Null;
    double number = 0.0;
    bool boolean = false;
    string text;
    vector<JsonValue> array;
    vector<pair<string, JsonValue>> object;
};

class JsonParser {
public:
    explicit JsonParser(string source) : source_(std::move(source)) {}

    JsonValue parse() {
        skip_ws();
        JsonValue value = parse_value();
        skip_ws();
        if (pos_ != source_.size()) {
            throw runtime_error("unexpected trailing content in JSON");
        }
        return value;
    }

private:
    string source_;
    size_t pos_ = 0;

    void skip_ws() {
        while (pos_ < source_.size() && isspace(static_cast<unsigned char>(source_[pos_])) != 0) {
            ++pos_;
        }
    }

    char peek() const {
        return pos_ < source_.size() ? source_[pos_] : '\0';
    }

    char get() {
        if (pos_ >= source_.size()) {
            throw runtime_error("unexpected end of JSON");
        }
        return source_[pos_++];
    }

    bool consume(char expected) {
        skip_ws();
        if (peek() != expected) {
            return false;
        }
        ++pos_;
        return true;
    }

    JsonValue parse_value() {
        skip_ws();
        const char c = peek();
        if (c == '{') return parse_object();
        if (c == '[') return parse_array();
        if (c == '"') return parse_string_value();
        if (c == '-' || isdigit(static_cast<unsigned char>(c)) != 0) return parse_number();
        if (source_.compare(pos_, 4, "true") == 0) {
            pos_ += 4;
            JsonValue value;
            value.type = JsonValue::Type::Bool;
            value.boolean = true;
            return value;
        }
        if (source_.compare(pos_, 5, "false") == 0) {
            pos_ += 5;
            JsonValue value;
            value.type = JsonValue::Type::Bool;
            value.boolean = false;
            return value;
        }
        if (source_.compare(pos_, 4, "null") == 0) {
            pos_ += 4;
            return JsonValue{};
        }
        throw runtime_error("invalid JSON value");
    }

    JsonValue parse_object() {
        JsonValue value;
        value.type = JsonValue::Type::Object;
        get();
        skip_ws();
        if (consume('}')) {
            return value;
        }
        while (true) {
            skip_ws();
            if (peek() != '"') {
                throw runtime_error("expected string key in JSON object");
            }
            const JsonValue key = parse_string_value();
            if (!consume(':')) {
                throw runtime_error("expected ':' after JSON object key");
            }
            JsonValue element = parse_value();
            value.object.push_back({key.text, std::move(element)});
            skip_ws();
            if (consume('}')) {
                return value;
            }
            if (!consume(',')) {
                throw runtime_error("expected ',' in JSON object");
            }
        }
    }

    JsonValue parse_array() {
        JsonValue value;
        value.type = JsonValue::Type::Array;
        get();
        skip_ws();
        if (consume(']')) {
            return value;
        }
        while (true) {
            value.array.push_back(parse_value());
            skip_ws();
            if (consume(']')) {
                return value;
            }
            if (!consume(',')) {
                throw runtime_error("expected ',' in JSON array");
            }
        }
    }

    JsonValue parse_string_value() {
        JsonValue value;
        value.type = JsonValue::Type::String;
        value.text = parse_string();
        return value;
    }

    string parse_string() {
        if (get() != '"') {
            throw runtime_error("expected string");
        }
        string out;
        while (true) {
            const char c = get();
            if (c == '"') {
                return out;
            }
            if (c == '\\') {
                const char esc = get();
                switch (esc) {
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    default:
                        throw runtime_error("unsupported JSON escape sequence");
                }
                continue;
            }
            out.push_back(c);
        }
    }

    JsonValue parse_number() {
        const size_t start = pos_;
        if (peek() == '-') {
            ++pos_;
        }
        while (isdigit(static_cast<unsigned char>(peek())) != 0) {
            ++pos_;
        }
        if (peek() == '.') {
            ++pos_;
            while (isdigit(static_cast<unsigned char>(peek())) != 0) {
                ++pos_;
            }
        }
        if (peek() == 'e' || peek() == 'E') {
            ++pos_;
            if (peek() == '+' || peek() == '-') {
                ++pos_;
            }
            while (isdigit(static_cast<unsigned char>(peek())) != 0) {
                ++pos_;
            }
        }
        JsonValue value;
        value.type = JsonValue::Type::Number;
        value.number = stod(source_.substr(start, pos_ - start));
        return value;
    }
};

const JsonValue* find_member(const JsonValue& object, const string& key) {
    if (object.type != JsonValue::Type::Object) {
        return nullptr;
    }
    for (const auto& entry : object.object) {
        if (entry.first == key) {
            return &entry.second;
        }
    }
    return nullptr;
}

const JsonValue* require_member(const JsonValue& object, const string& key) {
    const JsonValue* value = find_member(object, key);
    if (value == nullptr) {
        throw runtime_error("missing key: " + key);
    }
    return value;
}

string require_string(const JsonValue& object, const string& key) {
    const JsonValue* value = require_member(object, key);
    if (value->type != JsonValue::Type::String) {
        throw runtime_error("expected string for key: " + key);
    }
    return value->text;
}

int require_int(const JsonValue& object, const string& key) {
    const JsonValue* value = require_member(object, key);
    if (value->type != JsonValue::Type::Number) {
        throw runtime_error("expected number for key: " + key);
    }
    return static_cast<int>(value->number);
}

unsigned require_uint(const JsonValue& object, const string& key) {
    const JsonValue* value = require_member(object, key);
    if (value->type != JsonValue::Type::Number) {
        throw runtime_error("expected number for key: " + key);
    }
    return static_cast<unsigned>(value->number);
}

double require_double(const JsonValue& object, const string& key) {
    const JsonValue* value = require_member(object, key);
    if (value->type != JsonValue::Type::Number) {
        throw runtime_error("expected number for key: " + key);
    }
    return value->number;
}

vector<pair<int, int>> parse_size_choices(const JsonValue& array_value) {
    if (array_value.type != JsonValue::Type::Array) {
        throw runtime_error("size_choices must be an array");
    }

    vector<pair<int, int>> out;
    for (const auto& entry : array_value.array) {
        if (entry.type != JsonValue::Type::Array || entry.array.size() != 2) {
            throw runtime_error("size_choices entries must be two-element arrays");
        }
        if (entry.array[0].type != JsonValue::Type::Number || entry.array[1].type != JsonValue::Type::Number) {
            throw runtime_error("size_choices entries must contain numbers");
        }
        out.push_back({static_cast<int>(entry.array[0].number), static_cast<int>(entry.array[1].number)});
    }
    return out;
}

DemoPreset parse_demo_preset(const JsonValue& root, const string& mode_name) {
    DemoPreset preset;
    preset.mode_name = mode_name;

    const JsonValue& generator = *require_member(root, "generator");
    const JsonValue& sa = *require_member(root, "sa");

    preset.generator.output_path = require_string(generator, "output_path");
    preset.generator.grid_w = require_int(generator, "grid_w");
    preset.generator.grid_h = require_int(generator, "grid_h");
    preset.generator.num_components = require_int(generator, "num_components");
    preset.generator.num_nets = require_int(generator, "num_nets");
    preset.generator.seed = require_uint(generator, "seed");
    preset.generator.fixed_chance_pct = require_int(generator, "fixed_chance_pct");
    preset.generator.pin_min = require_int(generator, "pin_min");
    preset.generator.pin_max = require_int(generator, "pin_max");
    preset.generator.net_degree_min = require_int(generator, "net_degree_min");
    preset.generator.net_degree_max = require_int(generator, "net_degree_max");
    preset.generator.size_choices = parse_size_choices(*require_member(generator, "size_choices"));

    preset.sa.input_path = require_string(sa, "input_path");
    preset.sa.output_path = require_string(sa, "output_path");
    preset.sa.seed = require_uint(sa, "seed");
    preset.sa.max_iters = require_int(sa, "max_iters");
    preset.sa.t0 = require_double(sa, "t0");
    preset.sa.alpha = require_double(sa, "alpha");
    preset.sa.temp_floor = require_double(sa, "temp_floor");
    preset.sa.cost_mode = require_string(sa, "cost_mode");
    preset.sa.moves_per_temp = require_int(sa, "moves_per_temp");
    preset.sa.illegal_retry = require_int(sa, "illegal_retry");

    return preset;
}

string json_file_path(const string& mode_name) {
    return "demo/" + mode_name + ".json";
}

}  // namespace

bool load_demo_preset(const string& mode_name, DemoPreset& preset, string& error) {
    try {
        const string path = json_file_path(mode_name);
        ifstream fin(path);
        if (!fin) {
            error = "failed to open demo config: " + path;
            return false;
        }

        stringstream buffer;
        buffer << fin.rdbuf();
        JsonParser parser(buffer.str());
        const JsonValue root = parser.parse();
        preset = parse_demo_preset(root, mode_name);
        return true;
    } catch (const exception& e) {
        error = e.what();
        return false;
    }
}
