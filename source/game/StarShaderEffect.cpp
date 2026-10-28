#include "StarShaderEffect.hpp"
#include "StarJsonExtra.hpp"
#include "StarLogging.hpp"

namespace Star {

ShaderEffectSpec shaderEffectSpecFromJson(Json const& json) {
  ShaderEffectSpec spec;
  String source = json.getString("shader");
  if (source.utf8Size() > MaxShaderEffectSourceSize)
    throw JsonException::format("shader effect source is {} bytes, the limit is {}", source.utf8Size(), MaxShaderEffectSourceSize);
  spec.source = make_shared<String const>(std::move(source));
  if (auto offset = json.opt("offset"))
    spec.offset = jsonToVec2F(*offset);
  spec.radius = json.getFloat("radius", 1.0f);
  spec.reach = json.getFloat("reach", 1.0f);
  if (auto params = json.opt("params"))
    spec.params = jsonToVec4F(*params);
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

List<ShaderEffectInstance> ShaderEffectSet::instances(Vec2F const& position) {
  if (!m_start)
    m_start = Time::monotonicTime();
  float age = Time::monotonicTime() - *m_start;
  List<ShaderEffectInstance> instances;
  for (auto const& spec : m_specs)
    instances.append({spec.source, position + spec.offset, spec.radius, spec.reach, age, spec.params});
  return instances;
}

}
