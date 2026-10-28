#pragma once

#include "StarJson.hpp"
#include "StarVector.hpp"
#include "StarList.hpp"
#include "StarString.hpp"
#include "StarTime.hpp"

namespace Star {

// A screen region effect an entity asks the client to draw.
// GLSL source that  is shared between instances plus where, how big
// and how old this one is.
// The source travels with the entity in its parameters so viewers do not
// need the shader installed.
// Retail peers ignore the parameter.
struct ShaderEffectSpec {
  // Source of the author's `effectInstance` function, see doc/json/shadereffect.md
  shared_ptr<String const> source;
  Vec2F offset;
  float radius = 1.0f; // blocks
  float reach = 1.0f;  // how many radii around the center the effect touches
  Vec4F params;
};

struct ShaderEffectInstance {
  shared_ptr<String const> source;
  Vec2F position;
  float radius;
  float reach;
  float age;
  Vec4F params;
};

// Biggest source accepted from entity data
size_t const MaxShaderEffectSourceSize = 16 * 1024;

// The shader effects of an entity that keeps them in Json it may change at any
// time (object parameters).
// They are parsed again only when the Json differs.
// Age is the time since this client first drew the effects.
class ShaderEffectSet {
public:
  void set(Json const& shaderEffects);
  void clear();
  bool empty() const;

  List<ShaderEffectInstance> instances(Vec2F const& position);

private:
  Json m_json;
  List<ShaderEffectSpec> m_specs;
  Maybe<double> m_start;
};

ShaderEffectSpec shaderEffectSpecFromJson(Json const& json);

// Reads the "shaderEffects" list of a parameters object.
// Bad entries are skipped.
List<ShaderEffectSpec> shaderEffectSpecsFromParameters(Json const& parameters);

}
