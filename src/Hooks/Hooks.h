#pragma once

namespace Hooks
{
	// Install all trampoline hooks. Must be called from SKSEPlugin_Load,
	// before the game initialises its renderer. Allocates from the SKSE
	// trampoline, so SKSE::AllocTrampoline must have been called first.
	void Install();

	// True once the D3D11 init hook has run and the ImGui context exists.
	[[nodiscard]] bool RendererReady();
}
