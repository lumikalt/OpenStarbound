#include "StarShaderEffectPainter.hpp"
#include "StarLogging.hpp"
#include "StarTime.hpp"

namespace Star {

// The vertex shader draws the quad, same as the post process effects
static char const* const VertexShader = R"GLSL(#version 140

in vec2 vertexPosition;

void main() {
  gl_Position = vec4(vertexPosition, 0.0, 1.0);
}
)GLSL";

// Everything around the author's effectInstance function.
// Instances are folded the way a post process toy would do by hand.
// Each one may bend where the picture is looked up via `source`, add
// light and block part of the picture.
static char const* const FragmentPrefix = R"GLSL(#version 140

uniform sampler2D screenTexture;
uniform vec2 screenSize;
uniform int instanceCount;
uniform vec4 instanceArea[8]; // xy: center in screen pixels, z: radius in pixels, w: age in seconds
uniform vec4 instanceParams[8];
uniform float lightCap;         // brightest light the effects may add
uniform float blockCap;         // largest share of the picture they may hide

out vec4 outColor;

)GLSL";

static char const* const FragmentSuffix = R"GLSL(

void main() {
  vec2 position = gl_FragCoord.xy;
  vec2 source = position;
  vec3 light = vec3(0.0);
  float transmit = 1.0;

  for (int i = 0; i < 8; i++) {
    if (i >= instanceCount)
      break;
    vec4 area = instanceArea[i];
    if (area.z <= 0.5)
      continue;
    vec4 result = effectInstance(position, area.xy, area.z, area.w, instanceParams[i], source);
    light += result.rgb;
    transmit *= 1.0 - result.a;
  }

  light = clamp(light, vec3(0.0), vec3(lightCap));
  transmit = max(transmit, 1.0 - blockCap);
  vec2 uv = clamp(source / screenSize, vec2(0.001), vec2(0.999));
  outColor = vec4(texture(screenTexture, uv).rgb * transmit + light, 1.0);
}
)GLSL";

void ShaderEffectPainter::renderInit(RendererPtr renderer) {
  if (m_renderer) {
    for (auto const& pair : m_shaders) {
      if (!pair.second.effectName.empty())
        m_renderer->unloadEffect(pair.second.effectName);
    }
  }
  m_renderer = std::move(renderer);
  m_shaders.clear();
}

auto ShaderEffectPainter::loadedShader(shared_ptr<String const> const& source) -> LoadedShader* {
  if (auto loaded = m_shaders.ptr(*source)) {
    loaded->lastUsed = m_frame;
    return loaded->effectName.empty() ? nullptr : loaded;
  }

  // compiling can take a while, so one new shader a frame
  if (m_compiledThisFrame)
    return nullptr;
  m_compiledThisFrame = true;

  JsonObject parameters;
  parameters["instanceCount"] = JsonObject{{"type", "int"}, {"default", 0}, {"uniform", "instanceCount"}, {"scriptable", true}};
  parameters["lightCap"] = JsonObject{{"type", "float"}, {"default", LightCap}, {"uniform", "lightCap"}, {"scriptable", true}};
  parameters["blockCap"] = JsonObject{{"type", "float"}, {"default", BlockCap}, {"uniform", "blockCap"}, {"scriptable", true}};
  for (unsigned i = 0; i < InstancesPerPass; ++i) {
    parameters[strf("instanceArea{}", i)] = JsonObject{{"type", "vec4"}, {"default", JsonArray{0, 0, 0, 0}}, {"uniform", strf("instanceArea[{}]", i)}, {"scriptable", true}};
    parameters[strf("instanceParams{}", i)] = JsonObject{{"type", "vec4"}, {"default", JsonArray{0, 0, 0, 0}}, {"uniform", strf("instanceParams[{}]", i)}, {"scriptable", true}};
  }
  Json config = JsonObject{
    // reads the picture drawn so far and replaces it
    {"frameBuffer", "main"},
    {"frameBufferTextures", JsonArray{JsonObject{{"framebuffer", "main"}, {"texture", "screen"}}}},
    {"effectParameters", parameters},
    {"effectTextures", JsonObject{{"screen", JsonObject{{"textureUniform", "screenTexture"}}}}}
  };

  StringMap<String> shaders = {
    {"vertex", VertexShader},
    {"fragment", String(FragmentPrefix) + *source + FragmentSuffix}
  };

  LoadedShader loaded;
  loaded.lastUsed = m_frame;
  String name = strf("shaderEffect{}", m_nextId++);
  String error;
  if (m_renderer->loadRuntimeEffect(name, config, shaders, error))
    loaded.effectName = name;
  else
    Logger::warn("Shader effect did not compile, skipping it: {}", error);

  bool ok = !loaded.effectName.empty();
  auto added = m_shaders.insert({*source, std::move(loaded)});
  return ok ? &added.first->second : nullptr;
}

void ShaderEffectPainter::timePass(String const& source, LoadedShader& shader, double seconds) {
  ++shader.timedPasses;
  if (shader.timedPasses == 1)
    Logger::info("Shader effect {} takes {:.2f} ms a pass", shader.effectName, seconds * 1000);

  if (seconds > PassHardLimit || (seconds > PassBudget && ++shader.strikes >= StrikeLimit)) {
    Logger::warn("Shader effect {} takes {:.1f} ms a pass, too slow: unloading it", shader.effectName, seconds * 1000);
    m_renderer->unloadEffect(shader.effectName);
    // stays in the table without an effect, so the same source is not compiled again
    shader.effectName = String();
  } else if (seconds <= PassBudget) {
    shader.strikes = 0;
  }
}

void ShaderEffectPainter::unloadStale() {
  if (m_shaders.size() <= MaxLoadedShaders)
    return;
  // sources that are not drawn any more, oldest first
  List<pair<uint64_t, String>> order;
  for (auto const& pair : m_shaders)
    order.append({pair.second.lastUsed, pair.first});
  order.sort();
  for (auto const& entry : order) {
    if (m_shaders.size() <= MaxLoadedShaders || entry.first == m_frame)
      break;
    auto loaded = m_shaders.take(entry.second);
    if (!loaded.effectName.empty())
      m_renderer->unloadEffect(loaded.effectName);
  }
}

void ShaderEffectPainter::render(WorldCamera const& camera, List<ShaderEffectInstance> const& instances, Settings const& settings) {
  ++m_frame;
  m_compiledThisFrame = false;
  if (instances.empty())
    return;

  Vec2F screenSize = Vec2F(m_renderer->screenSize());
  float pixelsPerBlock = TilePixels * camera.pixelRatio();

  // by shader, in the order they first showed up
  OrderedHashMap<String, List<ShaderEffectInstance const*>> byShader;
  unsigned counted = 0;
  for (auto const& instance : instances) {
    if (!instance.source || (!instance.local && !settings.remote))
      continue;
    Vec2F center = camera.worldToScreen(instance.position);
    float reach = instance.radius * max(instance.reach, 1.0f) * pixelsPerBlock;
    if (center[0] + reach < 0 || center[1] + reach < 0 || center[0] - reach > screenSize[0] || center[1] - reach > screenSize[1])
      continue;
    if (counted >= MaxInstances)
      break;

    auto found = byShader.ptr(*instance.source);
    if (!found) {
      if (byShader.size() >= MaxPasses)
        continue;
      if (!loadedShader(instance.source))
        continue;
      found = &byShader[*instance.source];
    }
    found->append(&instance);
    ++counted;
  }

  if (!byShader.empty()) {
    auto quad = renderFlatRect(RectF::withSize(screenSize / -2, screenSize), Vec4B::filled(0), 0.0f);
    for (auto const& pair : byShader) {
      // looked up again as loading another shader this frame may have moved the table
      auto loaded = m_shaders.ptr(pair.first);
      if (!loaded || loaded->effectName.empty())
        continue;
      auto& shader = *loaded;
      auto const& list = pair.second;
      String effectName = shader.effectName;

      // One pass per shader.
      // Switching to the effect that is already current
      // would not swap the framebuffers so the extra instances are dropped
      size_t count = min<size_t>(InstancesPerPass, list.size());
      for (size_t i = 0; i < count; ++i) {
        auto const& instance = *list[i];
        Vec2F center = camera.worldToScreen(instance.position);
        m_renderer->setEffectScriptableParameter(effectName, strf("instanceArea{}", i), Vec4F(center[0], center[1], instance.radius * pixelsPerBlock, instance.age));
        m_renderer->setEffectScriptableParameter(effectName, strf("instanceParams{}", i), instance.params);
      }
      m_renderer->setEffectScriptableParameter(effectName, "instanceCount", (int)count);
      m_renderer->setEffectScriptableParameter(effectName, "lightCap", settings.reduced ? ReducedLightCap : LightCap);
      m_renderer->setEffectScriptableParameter(effectName, "blockCap", settings.reduced ? ReducedBlockCap : BlockCap);

      bool timed = shader.timedPasses < FirstTimedPasses || shader.passes % TimedPassInterval == 0;
      ++shader.passes;
      if (timed)
        m_renderer->finish();
      double start = Time::monotonicTime();
      m_renderer->switchEffectConfig(effectName);
      m_renderer->render(quad);
      if (timed) {
        m_renderer->finish();
        timePass(pair.first, shader, Time::monotonicTime() - start);
      }
    }
  }

  unloadStale();
}

}
