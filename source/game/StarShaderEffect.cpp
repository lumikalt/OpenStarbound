#include "StarShaderEffect.hpp"
#include "StarJsonExtra.hpp"
#include "StarLogging.hpp"

#include <algorithm>
#include <map>
#include <cstring>

namespace Star {


namespace {

// Source split into identifiers, numbers and operators, comments dropped.
// Fails on a preprocessor directive.
Maybe<std::vector<std::string>> tokenizeShader(std::string const& s) {
  std::vector<std::string> tokens;
  size_t i = 0;
  auto isIdent = [](char c) { return isalnum((unsigned char)c) || c == '_'; };
  while (i < s.size()) {
    char c = s[i];
    if (isspace((unsigned char)c)) {
      ++i;
    } else if (c == '#') {
      return {};
    } else if (c == '/' && i + 1 < s.size() && s[i + 1] == '/') {
      while (i < s.size() && s[i] != '\n')
        ++i;
    } else if (c == '/' && i + 1 < s.size() && s[i + 1] == '*') {
      auto end = s.find("*/", i + 2);
      i = end == std::string::npos ? s.size() : end + 2;
    } else if (isIdent(c) || (c == '.' && i + 1 < s.size() && isdigit((unsigned char)s[i + 1]))) {
      size_t start = i;
      while (i < s.size() && (isIdent(s[i]) || s[i] == '.'))
        ++i;
      tokens.push_back(s.substr(start, i - start));
    } else {
      static char const* const Operators[] = {"<<=", ">>=", "++", "--", "+=", "-=", "*=", "/=", "%=", "<<", ">>", "<=", ">=", "==", "!=", "&&", "||", "^^", "&=", "|=", "^="};
      size_t length = 1;
      for (auto op : Operators) {
        size_t opLength = strlen(op);
        if (opLength > length && s.compare(i, opLength, op) == 0)
          length = opLength;
      }
      tokens.push_back(s.substr(i, length));
      i += length;
    }
  }
  return tokens;
}

bool isIntLiteral(std::string const& t) {
  return !t.empty() && t.size() < 8 && std::all_of(t.begin(), t.end(), [](char c) { return isdigit((unsigned char)c); });
}

}

Maybe<String> checkShaderEffectSource(String const& source) {
  auto tokenized = tokenizeShader(source.utf8());
  if (!tokenized)
    return String("preprocessor directives are not allowed");
  auto const& tokens = *tokenized;

  // const int NAME = literal;
  std::map<std::string, long> constants;
  for (size_t i = 0; i + 5 < tokens.size(); ++i) {
    if (tokens[i] == "const" && tokens[i + 1] == "int" && tokens[i + 3] == "=" && isIntLiteral(tokens[i + 4]) && tokens[i + 5] == ";")
      constants[tokens[i + 2]] = std::stol(tokens[i + 4]);
  }
  auto intValue = [&](std::string const& t) -> Maybe<long> {
    if (isIntLiteral(t))
      return std::stol(t);
    auto find = constants.find(t);
    if (find != constants.end())
      return find->second;
    return {};
  };

  String const shape = "for loops must look like for (int i = 0; i < 8; i++) { ... }, between int literals or const int names, with braces";
  unsigned outParameters = 0;
  // last token of the body of each loop around the token being looked at, and
  // how many times it and the loops around it run
  std::vector<pair<size_t, long>> active;
  for (size_t i = 0; i < tokens.size(); ++i) {
    while (!active.empty() && i > active.back().first)
      active.pop_back();
    auto const& t = tokens[i];

    if (t == "while" || t == "do")
      return String("only for loops are allowed");
    if ((t == "out" || t == "inout") && ++outParameters > 1)
      return String("out and inout parameters are only allowed on effectInstance");
    if (t != "for")
      continue;

    // for (int v = A; v < B; v++) {
    size_t p = i + 1;
    auto take = [&]() -> std::string { return p < tokens.size() ? tokens[p++] : std::string(); };
    if (take() != "(" || take() != "int")
      return shape;
    std::string var = take();
    if (take() != "=")
      return shape;
    auto start = intValue(take());
    if (take() != ";" || take() != var)
      return shape;
    std::string compare = take();
    auto limit = intValue(take());
    if (take() != ";" || !start || !limit)
      return shape;

    long step = 0;
    std::string first = take();
    if (first == "++" || first == "--") {
      if (take() != var)
        return shape;
      step = first == "++" ? 1 : -1;
    } else if (first == var) {
      std::string op = take();
      if (op == "++") {
        step = 1;
      } else if (op == "--") {
        step = -1;
      } else if (op == "+=" || op == "-=") {
        auto amount = intValue(take());
        if (!amount || *amount <= 0)
          return shape;
        step = op == "+=" ? *amount : -*amount;
      } else {
        return shape;
      }
    } else {
      return shape;
    }
    if (take() != ")" || take() != "{")
      return shape;

    size_t bodyStart = p;
    size_t bodyEnd = bodyStart;
    for (int depth = 1; bodyEnd < tokens.size(); ++bodyEnd) {
      if (tokens[bodyEnd] == "{")
        ++depth;
      else if (tokens[bodyEnd] == "}" && --depth == 0)
        break;
    }
    if (bodyEnd >= tokens.size())
      return shape;

    long span;
    if (step > 0 && (compare == "<" || compare == "<="))
      span = *limit - *start + (compare == "<=" ? 1 : 0);
    else if (step < 0 && (compare == ">" || compare == ">="))
      span = *start - *limit + (compare == ">=" ? 1 : 0);
    else
      return String("the condition of a for loop must be reached by its step");
    long stride = step > 0 ? step : -step;
    long iterations = span > 0 ? (span + stride - 1) / stride : 0;
    if (iterations > (long)MaxShaderLoopIterations)
      return String(strf("a for loop may run at most {} times, this one {}", MaxShaderLoopIterations, iterations));
    long product = (active.empty() ? 1 : active.back().second) * max<long>(iterations, 1);
    if (product > (long)MaxShaderLoopProduct)
      return String(strf("nested for loops may run at most {} times in all", MaxShaderLoopProduct));

    // the loop must be the only thing counting
    for (size_t j = bodyStart; j < bodyEnd; ++j) {
      if (tokens[j] != var)
        continue;
      static char const* const Writes[] = {"=", "+=", "-=", "*=", "/=", "%=", "<<=", ">>=", "&=", "|=", "^=", "++", "--"};
      bool written = j > 0 && (tokens[j - 1] == "++" || tokens[j - 1] == "--");
      for (auto op : Writes)
        written = written || (j + 1 < tokens.size() && tokens[j + 1] == op);
      if (written)
        return String(strf("the body of a for loop may not change its variable '{}'", var));
    }

    active.push_back({bodyEnd, product});
  }
  return {};
}

ShaderEffectSpec shaderEffectSpecFromJson(Json const& json) {
  ShaderEffectSpec spec;
  String source = json.getString("shader");
  if (source.utf8Size() > MaxShaderEffectSourceSize)
    throw JsonException::format("shader effect source is {} bytes, the limit is {}", source.utf8Size(), MaxShaderEffectSourceSize);
  if (auto problem = checkShaderEffectSource(source))
    throw JsonException::format("shader effect refused: {}", *problem);
  spec.source = make_shared<String const>(std::move(source));
  if (auto offset = json.opt("offset"))
    spec.offset = jsonToVec2F(*offset);
  spec.radius = json.getFloat("radius", 1.0f);
  spec.reach = json.getFloat("reach", 1.0f);
  if (auto params = json.opt("params")) {
    // a number or the name of a live value of the carrier
    auto list = params->toArray();
    for (size_t i = 0; i < 4 && i < list.size(); ++i) {
      if (list[i].isType(Json::Type::String))
        spec.bindings[i] = list[i].toString();
      else
        spec.params[i] = list[i].toFloat();
    }
  }
  return spec;
}

List<ShaderEffectSpec> shaderEffectSpecsFromParameters(Json const& parameters) {
  List<ShaderEffectSpec> specs;
  if (!parameters)
    return specs;
  for (auto const& entry : parameters.getArray("shaderEffects", {})) {
    try {
      specs.append(shaderEffectSpecFromJson(entry));
    } catch (StarException const& e) {
      Logger::warn("Ignoring shader effect: {}", e.what());
    }
  }
  return specs;
}

void ShaderEffectSet::set(Json const& shaderEffects) {
  if (shaderEffects == m_json)
    return;
  m_json = shaderEffects;
  m_specs = shaderEffectSpecsFromParameters(JsonObject{{"shaderEffects", shaderEffects}});
}

void ShaderEffectSet::clear() {
  m_json = Json();
  m_specs.clear();
  m_start.reset();
}

bool ShaderEffectSet::empty() const {
  return m_specs.empty();
}

Vec4F ShaderEffectSpec::paramsFor(ShaderParamLookup const& lookup) const {
  Vec4F result = params;
  if (lookup) {
    for (size_t i = 0; i < 4; ++i) {
      if (!bindings[i].empty())
        result[i] = lookup(bindings[i]);
    }
  }
  return result;
}

List<ShaderEffectInstance> ShaderEffectSet::instances(Vec2F const& position, ShaderParamLookup const& lookup) {
  if (!m_start)
    m_start = Time::monotonicTime();
  float age = Time::monotonicTime() - *m_start;
  List<ShaderEffectInstance> instances;
  for (auto const& spec : m_specs)
    instances.append({spec.source, position + spec.offset, spec.radius, spec.reach, age, spec.paramsFor(lookup)});
  return instances;
}

}
