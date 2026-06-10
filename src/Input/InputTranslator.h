#pragma once

// Thread model: the game dispatches input events on its main thread, but
// ImGui's IO functions are not thread-safe and must only be called on the
// render thread (inside the present hook). Events are therefore *recorded*
// into a mutex-guarded queue on the input side and *drained* into ImGui at
// the start of each overlay frame on the render side.

namespace Input
{
	// --- input-dispatch thread ----------------------------------------

	// True if any keyboard button-down event in the chain matches the
	// configured toggle key (Settings::Get().toggleKey, a DIK scan code).
	[[nodiscard]] bool ChainContainsToggle(RE::InputEvent* const* a_events);

	// True if Escape was pressed in the chain.
	[[nodiscard]] bool ChainContainsEscape(RE::InputEvent* const* a_events);

	// Queue a BSInput event chain for the render thread. Bounded: excess
	// events are dropped if the overlay stops rendering.
	void RecordEvents(RE::InputEvent* const* a_events);

	// Ask the render thread to reset transient state on its next frame
	// (clear held keys, centre the virtual cursor).
	void NotifyOverlayOpened();

	// --- render thread -------------------------------------------------

	// Translate all queued events into ImGui IO. Call once per frame,
	// before ImGui::NewFrame().
	void DrainToImGui();
}
