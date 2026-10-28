#pragma once

#include "StarShaderEffect.hpp"
#include "StarWorldCamera.hpp"
#include "StarRenderer.hpp"
#include "StarHash.hpp"
#include "StarOrderedMap.hpp"

namespace Star {

STAR_CLASS(ShaderEffectPainter);

// Draws the shader effects entities ask for (see doc/json/shadereffect.md).
// Effects are compiled when first seen from the source the entity carried
// and shared between instances with the same source.
// Up to InstancesPerPass instances of a shader run in one fullscreen pass.
class ShaderEffectPainter {
public:
  static unsigned const InstancesPerPass = 8;
  // Compiled shaders kept at once, LRU dropped.
  static unsigned const MaxLoadedShaders = 32;

  void renderInit(RendererPtr renderer);

  // Switches the renderer's effect.
  // Call where post processing is drawn.
  void render(WorldCamera const& camera, List<ShaderEffectInstance> const& instances);

private:
  struct LoadedShader {
    String effectName; // empty if the source did not compile
    uint64_t lastUsed = 0;
  };

  Maybe<String> shaderEffectName(shared_ptr<String const> const& source);
  void unloadStale();

  RendererPtr m_renderer;
  // by source text, the same source from different entities is one shader
  HashMap<String, LoadedShader> m_shaders;
  unsigned m_nextId = 0;
  uint64_t m_frame = 0;
};

}
