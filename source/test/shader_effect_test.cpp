#include "StarShaderEffect.hpp"

#include "gtest/gtest.h"

using namespace Star;

static String wrap(String const& body) {
  return String("vec4 effectInstance(vec2 position, vec2 center, float radius, float age, vec4 params, inout vec2 source) {\n"
    ) + body + "\nreturn vec4(0.0);\n}\n";
}

TEST(ShaderEffectTest, AcceptsPlainCode) {
  EXPECT_FALSE(checkShaderEffectSource(wrap("float d = length(position - center);")));
  EXPECT_FALSE(checkShaderEffectSource(String("const float TILT = 0.24;\n") + wrap("// while (true) {}\n/* do */")));
  EXPECT_FALSE(checkShaderEffectSource(wrap("for (int i = 0; i < 8; i++) { source += vec2(i); }")));
  EXPECT_FALSE(checkShaderEffectSource(wrap("for (int i = 10; i > 0; i -= 2) { source += vec2(i); }")));
  EXPECT_FALSE(checkShaderEffectSource(String("const int N = 16;\n") + wrap("for (int i = 0; i <= N; ++i) { source += vec2(i); }")));
  EXPECT_FALSE(checkShaderEffectSource(wrap("for (int i = 0; i < 16; i++) { for (int j = 0; j < 16; j++) { source += vec2(i, j); } }")));
}

TEST(ShaderEffectTest, RefusesPreprocessorAndUnboundedLoops) {
  EXPECT_TRUE(checkShaderEffectSource(String("#extension GL_ARB_foo : enable\n") + wrap("")));
  EXPECT_TRUE(checkShaderEffectSource(String("#define W while\n") + wrap("")));
  EXPECT_TRUE(checkShaderEffectSource(wrap("while (true) {}")));
  EXPECT_TRUE(checkShaderEffectSource(wrap("do { } while (true);")));
  EXPECT_TRUE(checkShaderEffectSource(wrap("for (;;) {}")));
  EXPECT_TRUE(checkShaderEffectSource(wrap("for (int i = 0; i < 8; i++) source += vec2(i);"))); // no braces
  EXPECT_TRUE(checkShaderEffectSource(wrap("for (int i = 0; i < age; i++) {}")));              // not constant
  EXPECT_TRUE(checkShaderEffectSource(wrap("for (int i = 0; i < 8; i--) {}")));                // never ends
  EXPECT_TRUE(checkShaderEffectSource(wrap("for (int i = 0; i > 8; i++) {}")));
  EXPECT_TRUE(checkShaderEffectSource(wrap("for (int i = 0; i < 100000; i++) {}")));            // too many
  EXPECT_TRUE(checkShaderEffectSource(wrap("for (int i = 0; i < 64; i++) { for (int j = 0; j < 64; j++) {} }"))); // product
}

TEST(ShaderEffectTest, RefusesChangingTheLoopVariable) {
  EXPECT_TRUE(checkShaderEffectSource(wrap("for (int i = 0; i < 8; i++) { i = 0; }")));
  EXPECT_TRUE(checkShaderEffectSource(wrap("for (int i = 0; i < 8; i++) { i -= 1; }")));
  EXPECT_TRUE(checkShaderEffectSource(wrap("for (int i = 0; i < 8; i++) { --i; }")));
  EXPECT_FALSE(checkShaderEffectSource(wrap("for (int i = 0; i < 8; i++) { if (i == 3) { source.x = float(i); } }")));
}

TEST(ShaderEffectTest, RefusesOtherOutParameters) {
  EXPECT_TRUE(checkShaderEffectSource(String("void grow(inout int i) { i += 1; }\n") + wrap("")));
  EXPECT_TRUE(checkShaderEffectSource(String("void grow(out int i) { i = 1; }\n") + wrap("")));
}

TEST(ShaderEffectTest, ParsingSkipsRefusedEntries) {
  Json good = JsonObject{{"shader", wrap("")}, {"radius", 2}};
  Json bad = JsonObject{{"shader", wrap("while (true) {}")}};
  auto specs = shaderEffectSpecsFromParameters(JsonObject{{"shaderEffects", JsonArray{bad, good}}});
  EXPECT_EQ(specs.size(), 1u);
  EXPECT_EQ(specs[0].radius, 2.0f);

  Json big = JsonObject{{"shader", String(std::string(MaxShaderEffectSourceSize + 1, ' '))}};
  EXPECT_TRUE(shaderEffectSpecsFromParameters(JsonObject{{"shaderEffects", JsonArray{big}}}).empty());
}
