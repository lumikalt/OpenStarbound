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
//
// The source comes from other players, so everything is limited:
//  - the source was already checked by checkShaderEffectSource
//  - one new shader is compiled per frame, a failing one is not tried again
//  - MaxLoadedShaders compiled at once, MaxPasses drawn and MaxInstances in a frame
//  - the GPU time of every pass is measured now and then, a shader that is
//    too slow is disabled until the next renderInit
class ShaderEffectPainter {
public:
  struct Settings {
    // draw effects of entities that this client does not master
    bool remote = true;
    // limit how bright and how dark an effect can make the picture
    bool reduced = false;
  };

  static unsigned const InstancesPerPass = 8;
  // Compiled shaders kept at once, LRU dropped.
  static unsigned const MaxLoadedShaders = 32;
  static unsigned const MaxPasses = 8;
  static unsigned const MaxInstances = 64;

  // A pass slower than this is a strike, three in a row or one over HardLimit unload the shader.
  static double constexpr PassBudget = 0.012;
  static double constexpr PassHardLimit = 0.25;
  static unsigned const StrikeLimit = 3;
  // Every pass is timed the first few times, then every 120 passes.
  static unsigned const FirstTimedPasses = 3;
  static unsigned const TimedPassInterval = 120;

  // Brightest light an effect may add and the largest share of the picture it
  // may hide, with and without Settings::reduced.
  static float constexpr ReducedLightCap = 0.25f;
  static float constexpr ReducedBlockCap = 0.35f;
  static float constexpr LightCap = 16.0f;
  static float constexpr BlockCap = 1.0f;

  void renderInit(RendererPtr renderer);

  // Switches the renderer's effect.
  // Call where post processing is drawn.
  void render(WorldCamera const& camera, List<ShaderEffectInstance> const& instances, Settings const& settings);

private:
  struct LoadedShader {
    String effectName; // empty if the source did not compile or was unloaded
    uint64_t lastUsed = 0;
    unsigned timedPasses = 0;
    unsigned strikes = 0;
    uint64_t passes = 0;
  };

  // Null when the effect cannot be drawn (yet).
  LoadedShader* loadedShader(shared_ptr<String const> const& source);
  void timePass(String const& source, LoadedShader& shader, double seconds);
  void unloadStale();

  RendererPtr m_renderer;
  // by source text, the same source from different entities is one shader
  HashMap<String, LoadedShader> m_shaders;
  unsigned m_nextId = 0;
  uint64_t m_frame = 0;
  bool m_compiledThisFrame = false;
};

}
