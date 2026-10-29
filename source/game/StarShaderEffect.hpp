#pragma once

#include "StarJson.hpp"
#include "StarVector.hpp"
#include "StarList.hpp"
#include "StarString.hpp"
#include "StarArray.hpp"
#include "StarTime.hpp"

namespace Star {

// Looks up the live value a `params` entry names ("speed", "tag:charge", ...) on
// the carrying entity, 0 when it has no such thing.
typedef function<float(String const&)> ShaderParamLookup;

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
  // Per component of params, what to look up instead of the number in it
  Array<String, 4> bindings;

  Vec4F paramsFor(ShaderParamLookup const& lookup) const;
};

struct ShaderEffectInstance {
  shared_ptr<String const> source;
  Vec2F position;
  float radius;
  float reach;
  float age;
  Vec4F params;
  // the viewing client is the master of the entity
  bool local = false;
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

  List<ShaderEffectInstance> instances(Vec2F const& position, ShaderParamLookup const& lookup = {});

private:
  Json m_json;
  List<ShaderEffectSpec> m_specs;
  Maybe<double> m_start;
};

// Checks shader source before it is compiled.
// No preprocessor directives, no while or do, every for loop counts an int
// between int literals or `const int` names in at most MaxShaderLoopIterations
// steps and leaves its variable alone, loops multiply to at most
// MaxShaderLoopProduct and the only out/inout parameter is the one
// effectInstance has.
// Returns why it was rejected, if it was.
size_t const MaxShaderLoopIterations = 64;
size_t const MaxShaderLoopProduct = 256;
Maybe<String> checkShaderEffectSource(String const& source);

ShaderEffectSpec shaderEffectSpecFromJson(Json const& json);

// Reads the "shaderEffects" list of a parameters object.
// Bad entries are skipped.
List<ShaderEffectSpec> shaderEffectSpecsFromParameters(Json const& parameters);

}
