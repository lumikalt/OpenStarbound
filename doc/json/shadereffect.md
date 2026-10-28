# Shader Effects

OpenStarbound can draw a screen region shader effect for an entity.
Since the shader travels with the entity, other OpenStarbound clients draw it without having the shader installed.

An effect is an entry in the `shaderEffects` list of the **parameters** of an entity (`parameters` of `world.spawnProjectile`, object and monster parameters, …).
For now, only projectiles work, more will follow.

This is compatible with Retail clients, which simply ignore the parameter and don't load the shader.

```json
"shaderEffects": [
  {
    "shader": "<GLSL source of effectInstance, see below>",
    "radius": 2.5,
    "reach": 6.0,
    "offset": [0, 0],
    "params": [14, 0, 0, 0]
  }
]
```

Where:

- `shader` is the source text for the shader, at most 16 KiB. Any entities with the same source share the same compiled shader.
  If a shader fails to compile, a log warning is printed and the shader is inert.
- `radius` is the size, in blocks, that's handed to the shader as pixels.
- `reach` states how many radii around the center the effect touches. An instance is skipped if its circle is offscreen.
- `offset` selects the origin point, in blocks, for the shader as an offset from the entity position.
- `params` is a tuple of 4 floats for the shader to use as needed.

## The Shader

The engine supplies the surrounding code (version, uniforms, `main`, the loop over instances).
You write the one function.

```glsl
// position: this pixel, in screen pixels, origin bottom left
// center, radius: the instance, in screen pixels
// age: seconds since the entity was spawned
// params: the four numbers from the Json
// source: where the picture is looked up for this pixel, start = position, bend it for lenses
// returns: rgb is light added in front of the picture, a is how much of the picture it blocks
vec4 effectInstance(vec2 position, vec2 center, float radius, float age, vec4 params, inout vec2 source)
```

`uniform sampler2D screenTexture` is available, representing  the world drawn so far, without the interface.
`uniform vec2 screenSize` is also available and self-explanatory.

Up to 8 instances of one shader are drawn per frame, the rest are dropped.
This is to prevent some abuse.

Effects draw over the world and under the interface, so they cannot cover interface panes.
