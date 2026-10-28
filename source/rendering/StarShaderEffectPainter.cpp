#include "StarShaderEffectPainter.hpp"
#include "StarLogging.hpp"

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

Maybe<String> ShaderEffectPainter::shaderEffectName(shared_ptr<String const> const& source) {
  if (auto loaded = m_shaders.ptr(*source)) {
    loaded->lastUsed = m_frame;
    if (loaded->effectName.empty())
      return {};
    return loaded->effectName;
  }

  JsonObject parameters;
  parameters["instanceCount"] = JsonObject{{"type", "int"}, {"default", 0}, {"uniform", "instanceCount"}, {"scriptable", true}};
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

  m_shaders.add(*source, loaded);
  if (loaded.effectName.empty())
    return {};
  return loaded.effectName;
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

void ShaderEffectPainter::render(WorldCamera const& camera, List<ShaderEffectInstance> const& instances) {
  ++m_frame;
  if (instances.empty())
    return;

  Vec2F screenSize = Vec2F(m_renderer->screenSize());
  float pixelsPerBlock = TilePixels * camera.pixelRatio();

  // by shader, in the order they first showed up
  OrderedHashMap<String, List<ShaderEffectInstance const*>> byShader;
  for (auto const& instance : instances) {
    if (!instance.source)
      continue;
    Vec2F center = camera.worldToScreen(instance.position);
    float reach = instance.radius * max(instance.reach, 1.0f) * pixelsPerBlock;
    if (center[0] + reach < 0 || center[1] + reach < 0 || center[0] - reach > screenSize[0] || center[1] - reach > screenSize[1])
      continue;
    if (auto name = shaderEffectName(instance.source))
      byShader[*name].append(&instance);
  }

  if (!byShader.empty()) {
    auto quad = renderFlatRect(RectF::withSize(screenSize / -2, screenSize), Vec4B::filled(0), 0.0f);
    for (auto const& pair : byShader) {
      // One pass per shader.
      // Switching to the effect that is already current
      // would not swap the framebuffers so the extra instances are dropped
      auto const& list = pair.second;
      size_t count = min<size_t>(InstancesPerPass, list.size());
      for (size_t i = 0; i < count; ++i) {
        auto const& instance = *list[i];
        Vec2F center = camera.worldToScreen(instance.position);
        m_renderer->setEffectScriptableParameter(pair.first, strf("instanceArea{}", i), Vec4F(center[0], center[1], instance.radius * pixelsPerBlock, instance.age));
        m_renderer->setEffectScriptableParameter(pair.first, strf("instanceParams{}", i), instance.params);
      }
      m_renderer->setEffectScriptableParameter(pair.first, "instanceCount", (int)count);
      m_renderer->switchEffectConfig(pair.first);
      m_renderer->render(quad);
    }
  }

  unloadStale();
}

}
