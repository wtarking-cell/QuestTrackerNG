#include "Hooks/Hooks.h"

#include "Input/InputTranslator.h"
#include "UI/QuestTrackerUI.h"

#include <d3d11.h>

#include <imgui.h>
#include <imgui_impl_dx11.h>

namespace Hooks
{
	namespace
	{
		std::atomic<bool> g_rendererReady{ false };

		// -------------------------------------------------------------------
		// Hook 1: BSGraphics::InitD3D — runs once, right after the game has
		// created its D3D11 device, context, and swap chain. We piggyback on
		// it to initialise ImGui against the game's device.
		//
		// Call site: SE 75595 + 0x9 / AE 77226 + 0x275 (Address Library IDs).
		// -------------------------------------------------------------------
		struct D3DInitHook
		{
			static void thunk()
			{
				func();

				auto* data = RE::BSGraphics::Renderer::GetRendererData();
				if (!data || !data->forwarder || !data->context) {
					logger::error("D3DInitHook: renderer data unavailable, ImGui disabled");
					return;
				}

				auto* window = RE::BSGraphics::Renderer::GetCurrentRenderWindow();
				if (!window) {
					window = &data->renderWindows[0];
				}

				auto* device = reinterpret_cast<ID3D11Device*>(data->forwarder);
				auto* context = reinterpret_cast<ID3D11DeviceContext*>(data->context);

				IMGUI_CHECKVERSION();
				ImGui::CreateContext();

				ImGuiIO& io = ImGui::GetIO();
				io.IniFilename = nullptr;  // no imgui.ini next to the game exe
				io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
				io.DisplaySize = ImVec2(
					static_cast<float>(window->windowWidth),
					static_cast<float>(window->windowHeight));

				if (!ImGui_ImplDX11_Init(device, context)) {
					logger::error("D3DInitHook: ImGui_ImplDX11_Init failed");
					ImGui::DestroyContext();
					return;
				}

				g_rendererReady.store(true, std::memory_order_release);
				logger::info("ImGui initialised ({}x{})", window->windowWidth, window->windowHeight);
			}

			static inline REL::Relocation<decltype(thunk)> func;

			static void Install(SKSE::Trampoline& a_trampoline)
			{
				const REL::RelocationID id(75595, 77226);
				const auto              offset = REL::Module::IsAE() ? 0x275 : 0x9;
				func = a_trampoline.write_call<5>(id.address() + offset, thunk);
			}
		};

		// -------------------------------------------------------------------
		// Hook 2: the engine's per-frame call leading into IDXGISwapChain
		// presentation. We draw the overlay here, on the render thread.
		//
		// Call site: SE 75461 + 0x9 / AE 77246 + 0x9.
		// -------------------------------------------------------------------
		struct PresentHook
		{
			static void thunk(std::uint32_t a_timer)
			{
				func(a_timer);

				if (!g_rendererReady.load(std::memory_order_acquire)) {
					return;
				}

				auto& ui = UI::QuestTrackerUI::Get();
				if (!ui.IsVisible()) {
					return;
				}

				ImGuiIO& io = ImGui::GetIO();
				if (auto* data = RE::BSGraphics::Renderer::GetRendererData()) {
					auto* window = RE::BSGraphics::Renderer::GetCurrentRenderWindow();
					if (!window) {
						window = &data->renderWindows[0];
					}
					io.DisplaySize = ImVec2(
						static_cast<float>(window->windowWidth),
						static_cast<float>(window->windowHeight));
				}

				static auto lastFrame = std::chrono::steady_clock::now();
				const auto  now = std::chrono::steady_clock::now();
				io.DeltaTime = std::clamp(
					std::chrono::duration<float>(now - lastFrame).count(), 1.0e-4f, 0.1f);
				lastFrame = now;

				io.MouseDrawCursor = true;

				Input::DrainToImGui();

				ImGui_ImplDX11_NewFrame();
				ImGui::NewFrame();
				ui.Draw();
				ImGui::Render();
				ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
			}

			static inline REL::Relocation<decltype(thunk)> func;

			static void Install(SKSE::Trampoline& a_trampoline)
			{
				const REL::RelocationID id(75461, 77246);
				func = a_trampoline.write_call<5>(id.address() + 0x9, thunk);
			}
		};

		// -------------------------------------------------------------------
		// Hook 3: the engine's input event dispatch. While the overlay is
		// open we translate events for ImGui and forward an empty chain to
		// the game, so gameplay controls are fully suppressed.
		//
		// Call site: SE 67315 + 0x7B / AE 68617 + 0x7B.
		// -------------------------------------------------------------------
		struct DispatchInputHook
		{
			static void thunk(RE::BSTEventSource<RE::InputEvent*>* a_source, RE::InputEvent* const* a_events)
			{
				auto& ui = UI::QuestTrackerUI::Get();

				if (a_events && Input::ChainContainsToggle(a_events)) {
					if (g_rendererReady.load(std::memory_order_acquire) && ui.MayToggle()) {
						ui.Toggle();
					}
				}

				if (ui.IsVisible() && g_rendererReady.load(std::memory_order_acquire)) {
					if (a_events) {
						if (Input::ChainContainsEscape(a_events)) {
							ui.SetVisible(false);
						} else {
							Input::RecordEvents(a_events);
						}
					}
					// Swallow everything: the game sees an empty event chain.
					constexpr RE::InputEvent* const dummy[]{ nullptr };
					func(a_source, dummy);
					return;
				}

				func(a_source, a_events);
			}

			static inline REL::Relocation<decltype(thunk)> func;

			static void Install(SKSE::Trampoline& a_trampoline)
			{
				const REL::RelocationID id(67315, 68617);
				func = a_trampoline.write_call<5>(id.address() + 0x7B, thunk);
			}
		};
	}

	void Install()
	{
		auto& trampoline = SKSE::GetTrampoline();
		D3DInitHook::Install(trampoline);
		PresentHook::Install(trampoline);
		DispatchInputHook::Install(trampoline);
		logger::info("Hooks installed (D3D init, present, input dispatch)");
	}

	bool RendererReady()
	{
		return g_rendererReady.load(std::memory_order_acquire);
	}
}
