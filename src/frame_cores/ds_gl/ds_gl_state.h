// Copyright 2026 AzaharVR contributors
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <string>

/**
 * Two sets of what OpenGL has bound and set (0, which is there from the start, and 1), as two
 * contexts of one share group: for two of melonDS's renderers, which bind some of their uniform
 * buffers once, when they are made.
 */
namespace DsGl {

/// The gl* calls go on in this state.
void UseState(int state);
/// Deletes what was made in state 1 and is still there, and forgets the state. State 0 is in use
/// after that.
void FreeState();

/// What is done to a shader's source before it is compiled, from here on. nullptr: nothing.
using ShaderPatch = void (*)(std::string& source);
void PatchShaders(ShaderPatch patch);

/// Whether a program can be made of these two, without a device. `log`: why not.
bool Compiles(const std::string& vertex, const std::string& fragment, std::string& log);

} // namespace DsGl
