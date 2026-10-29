# Shader Effects

OpenStarbound can draw a screen region shader effect for an entity.
Since the shader travels with the entity, other OpenStarbound clients draw it without having the shader installed.

An effect is an entry in the `shaderEffects` list of the **parameters** of an entity (`parameters` of `world.spawnProjectile`, object and monster parameters, …).
For now, carriers can be projectiles, objects, stagehands, monsters and NPCs (see "Carriers" below), more will follow.

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
- `params` is a tuple of 4 values for the shader to use as needed.
  Each is either a number or the name of a live value of the carrier that is read again every frame (see "Live Parameters").

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

`uniform sampler2D screenTexture` is available, representing the world drawn so far, without the interface.
`uniform vec2 screenSize` is also available and self-explanatory.

Instances of one shader are drawn 8 at a time, one fullscreen pass per batch.
This is to prevent some abuse.

Effects draw over the world and under the interface, so they cannot cover interface panes.

## Carriers

The effect lives and dies with the entity that carries it.
A carrier is mastered by whoever created it.
Pick one by how long the effect should last.

### Projectile

Shader list goes in `parameters` of `world.spawnProjectile`.

Lasts until the projectile ends.
A client spawned projectile goes when that client leaves the world.
`age` is the time since it was fired.

### Object

Shader list goes in `shaderEffects` in the object file or in the object's `parameters`, which win, synced to other clients.

Lives as long as the object.
It's saved with the world and not tied to a player.
Centered on the object's position, i.e., its origin tile, so use `offset` to move it.

### Stagehand

Shader list goes in `shaderEffects` in the stagehand config or overrides.

Lives as long as the stagehand, which the server owns and saves.
Other clients only get a stagehand whose `broadcastArea`, default 10 x 10 blocks around it, reaches their view, so set it as large as the effect's reach.

### Monster

Shader list goes in `shaderEffects` in the monster's parameters, the third argument of `world.spawnMonster` or in the monster type's parameters.

Lives as long as the monster.
Follows it, centered on its position, so use `offset` as needed.

### NPC

Shader list goes in `shaderEffects` in the NPC's `overrides`, the fifth argument of `/spawnnpc` or the `world.spawnNpc` overrides.

Lives as long as the NPC.
Follows it.
Set only in the overrides, an NPC type's own config is not read for it.

### Notes

For objects and stagehands, `age` is the time since this client first drew the effect, so it differs between clients.
Use it for looping, not for a one shot that must line up.

A vanilla stagehand type with no behavior of its own makes a good carrier.
For example, in admin on a server:

```
/spawnstagehand waypoint {"scripts":[],"keepAlive":true,"broadcastArea":[-20,-20,20,20],"shaderEffects":[{"shader":"...","radius":2.5,"reach":6,"params":[14,0,0,0]}]}
```

On a Retail client or server, or one with outdated oSB, the carrier is just that entity and the effect is ignored.

## Limits and Settings

The source comes from other players and runs on every viewer's GPU, so it's checked and limited to prevent arbitrary code execution.

Before compilation, an effect that fails these check is skipped with a warning in the log naming the problem.

- No preprocessor lines (`#version`, `#define`, …).
- No `while` or `do`. A `for` loop must look like `for (int i = 0; i < 8; i++) { ... }`, so an `int` variable from an int literal, or a `const int NAME = 8;` declared in the source, to another, stepping by `++`, `--`, `+= N` or `-= N` in the direction of the condition. The for loop carries braces and its body may not assign to the variable. One loop runs at most 64 times and nested loops at most 256 times in all.
- `out` and `inout` only appear once, on `effectInstance` itself, so helper functions return their results.
- At most 16 KiB of source.

While drawing, there are also certain limitations.

- One new shader is compiled a frame, 32 stay loaded being disposed by LRU and a source that does not compile is not tried again.
- A frame draws at most 16 passes (8 instances of one shader each) and 64 instances in all. The rest are dropped.
- The GPU time of each pass is measured. The first three times it is drawn and then every 120th. A pass over 12 ms is a strike, three strikes in a row or one pass over 250 ms unload the shader until the game restarts or the renderer is reloaded. The first measurement of each shader is logged at info level.

### Client Settings

These are set in `starbound.config`, no menu yet.

`remoteShaderEffects`, default `true`, draws only effects of entities this client masters, i.e., its own projectiles.
Objects and stagehands are mastered by the server so they are hidden too,
even in single player.

`reducedShaderEffects`, default `false`, caps the light an effect adds at 0.25 and the share of the picture it can hide at 35 %, which bounds how hard an effect can flash.
It does not remove motion.
Lensing (bending the picture) is likewise not affected.

Effects draw under the interface, so they cannot cover panes, chat or the cursor.

## Live Parameters

An entry of `params` can be a string instead of a number.
If it is, the client asks the carrying entity for that value every frame, on every client, from state the entity already syncs.
This ensures nothing extra goes over the network so a Retail peer is unaffected.
 A name the carrier does not know gives 0.

```json
"params" : ["health", 0, "tag:charge", 0]
```

Where:

`speed` is the carrier speed in blocks/s.
Available on projectiles, monsters and NPCs.

`timeLeft` is the carrier's remaining time to live percentage, from 1 to 0.
Only for projectiles.

`health` is the health percentage of the carrier, from 1 to 0.
Available for monsters and NPCs.

`tag:<name>` is the animator's global tag `<name>` read as a number.
You can set it with `animator.setGlobalTag`.
Available for monsters and objects.

Objects and monsters can also change the tag from their own scripts, which makes any state reach the shader.
