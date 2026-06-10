#include "Input/InputTranslator.h"

#include "Settings.h"

#include <imgui.h>

namespace Input
{
	namespace
	{
		constexpr std::uint32_t DIK_ESCAPE = 0x01;

		// ------------------------------------------------------------------
		// Event queue (input thread -> render thread)
		// ------------------------------------------------------------------
		struct Record
		{
			enum class Kind : std::uint8_t
			{
				kMouseMove,
				kButton
			};

			Kind          kind{};
			RE::INPUT_DEVICE device{};
			std::uint32_t id = 0;
			float         value = 0.0f;
			bool          pressed = false;
			bool          down = false;
			float         dx = 0.0f;
			float         dy = 0.0f;
		};

		constexpr std::size_t kMaxQueuedEvents = 512;  // bound memory if frames stall

		std::mutex          g_queueLock;
		std::vector<Record> g_queue;
		std::atomic<bool>   g_resetPending{ false };

		// Render-thread-only state.
		float g_mouseX = 0.0f;
		float g_mouseY = 0.0f;
		bool  g_shiftDown = false;

		// DirectInput scan code -> ImGuiKey. Covers everything the overlay
		// needs (text fields, navigation); unmapped keys are ignored.
		ImGuiKey DIKToImGuiKey(std::uint32_t a_dik)
		{
			switch (a_dik) {
			case 0x01: return ImGuiKey_Escape;
			case 0x02: return ImGuiKey_1;
			case 0x03: return ImGuiKey_2;
			case 0x04: return ImGuiKey_3;
			case 0x05: return ImGuiKey_4;
			case 0x06: return ImGuiKey_5;
			case 0x07: return ImGuiKey_6;
			case 0x08: return ImGuiKey_7;
			case 0x09: return ImGuiKey_8;
			case 0x0A: return ImGuiKey_9;
			case 0x0B: return ImGuiKey_0;
			case 0x0C: return ImGuiKey_Minus;
			case 0x0D: return ImGuiKey_Equal;
			case 0x0E: return ImGuiKey_Backspace;
			case 0x0F: return ImGuiKey_Tab;
			case 0x10: return ImGuiKey_Q;
			case 0x11: return ImGuiKey_W;
			case 0x12: return ImGuiKey_E;
			case 0x13: return ImGuiKey_R;
			case 0x14: return ImGuiKey_T;
			case 0x15: return ImGuiKey_Y;
			case 0x16: return ImGuiKey_U;
			case 0x17: return ImGuiKey_I;
			case 0x18: return ImGuiKey_O;
			case 0x19: return ImGuiKey_P;
			case 0x1A: return ImGuiKey_LeftBracket;
			case 0x1B: return ImGuiKey_RightBracket;
			case 0x1C: return ImGuiKey_Enter;
			case 0x1D: return ImGuiKey_LeftCtrl;
			case 0x1E: return ImGuiKey_A;
			case 0x1F: return ImGuiKey_S;
			case 0x20: return ImGuiKey_D;
			case 0x21: return ImGuiKey_F;
			case 0x22: return ImGuiKey_G;
			case 0x23: return ImGuiKey_H;
			case 0x24: return ImGuiKey_J;
			case 0x25: return ImGuiKey_K;
			case 0x26: return ImGuiKey_L;
			case 0x27: return ImGuiKey_Semicolon;
			case 0x28: return ImGuiKey_Apostrophe;
			case 0x29: return ImGuiKey_GraveAccent;
			case 0x2A: return ImGuiKey_LeftShift;
			case 0x2B: return ImGuiKey_Backslash;
			case 0x2C: return ImGuiKey_Z;
			case 0x2D: return ImGuiKey_X;
			case 0x2E: return ImGuiKey_C;
			case 0x2F: return ImGuiKey_V;
			case 0x30: return ImGuiKey_B;
			case 0x31: return ImGuiKey_N;
			case 0x32: return ImGuiKey_M;
			case 0x33: return ImGuiKey_Comma;
			case 0x34: return ImGuiKey_Period;
			case 0x35: return ImGuiKey_Slash;
			case 0x36: return ImGuiKey_RightShift;
			case 0x39: return ImGuiKey_Space;
			case 0x3A: return ImGuiKey_CapsLock;
			case 0x47: return ImGuiKey_Keypad7;
			case 0x48: return ImGuiKey_Keypad8;
			case 0x49: return ImGuiKey_Keypad9;
			case 0x4B: return ImGuiKey_Keypad4;
			case 0x4C: return ImGuiKey_Keypad5;
			case 0x4D: return ImGuiKey_Keypad6;
			case 0x4F: return ImGuiKey_Keypad1;
			case 0x50: return ImGuiKey_Keypad2;
			case 0x51: return ImGuiKey_Keypad3;
			case 0x52: return ImGuiKey_Keypad0;
			case 0x53: return ImGuiKey_KeypadDecimal;
			case 0x9C: return ImGuiKey_KeypadEnter;
			case 0x9D: return ImGuiKey_RightCtrl;
			case 0xC7: return ImGuiKey_Home;
			case 0xC8: return ImGuiKey_UpArrow;
			case 0xC9: return ImGuiKey_PageUp;
			case 0xCB: return ImGuiKey_LeftArrow;
			case 0xCD: return ImGuiKey_RightArrow;
			case 0xCF: return ImGuiKey_End;
			case 0xD0: return ImGuiKey_DownArrow;
			case 0xD1: return ImGuiKey_PageDown;
			case 0xD2: return ImGuiKey_Insert;
			case 0xD3: return ImGuiKey_Delete;
			default:   return ImGuiKey_None;
			}
		}

		// Printable character for text input (US layout, enough for stage
		// IDs and filter text).
		char DIKToChar(std::uint32_t a_dik, bool a_shift)
		{
			constexpr std::string_view row1 = "1234567890-=";
			constexpr std::string_view row1s = "!@#$%^&*()_+";
			constexpr std::string_view rowQ = "qwertyuiop[]";
			constexpr std::string_view rowA = "asdfghjkl;'";
			constexpr std::string_view rowZ = "zxcvbnm,./";

			const auto upper = [&](char c) {
				return a_shift && c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
			};

			if (a_dik >= 0x02 && a_dik <= 0x0D) {
				return (a_shift ? row1s : row1)[a_dik - 0x02];
			}
			if (a_dik >= 0x10 && a_dik <= 0x1B) {
				return upper(rowQ[a_dik - 0x10]);
			}
			if (a_dik >= 0x1E && a_dik <= 0x28) {
				return upper(rowA[a_dik - 0x1E]);
			}
			if (a_dik >= 0x2C && a_dik <= 0x35) {
				return upper(rowZ[a_dik - 0x2C]);
			}
			if (a_dik == 0x39) {
				return ' ';
			}
			switch (a_dik) {  // numpad
			case 0x52: return '0';
			case 0x4F: return '1';
			case 0x50: return '2';
			case 0x51: return '3';
			case 0x4B: return '4';
			case 0x4C: return '5';
			case 0x4D: return '6';
			case 0x47: return '7';
			case 0x48: return '8';
			case 0x49: return '9';
			case 0x53: return '.';
			default:   return '\0';
			}
		}

		bool ChainContainsKeyDown(RE::InputEvent* const* a_events, std::uint32_t a_dik)
		{
			for (auto* event = *a_events; event; event = event->next) {
				if (event->GetEventType() != RE::INPUT_EVENT_TYPE::kButton) {
					continue;
				}
				const auto* button = static_cast<const RE::ButtonEvent*>(event);
				if (button->GetDevice() == RE::INPUT_DEVICE::kKeyboard &&
					button->GetIDCode() == a_dik && button->IsDown()) {
					return true;
				}
			}
			return false;
		}

		// Render thread: apply one translated record to ImGui.
		void Apply(const Record& a_rec, ImGuiIO& a_io)
		{
			if (a_rec.kind == Record::Kind::kMouseMove) {
				g_mouseX = std::clamp(g_mouseX + a_rec.dx, 0.0f, a_io.DisplaySize.x - 1.0f);
				g_mouseY = std::clamp(g_mouseY + a_rec.dy, 0.0f, a_io.DisplaySize.y - 1.0f);
				a_io.AddMousePosEvent(g_mouseX, g_mouseY);
				return;
			}

			switch (a_rec.device) {
			case RE::INPUT_DEVICE::kMouse:
				if (a_rec.id == 8) {  // wheel up
					a_io.AddMouseWheelEvent(0.0f, a_rec.value);
				} else if (a_rec.id == 9) {  // wheel down
					a_io.AddMouseWheelEvent(0.0f, -a_rec.value);
				} else if (a_rec.id < 5) {
					a_io.AddMouseButtonEvent(static_cast<int>(a_rec.id), a_rec.pressed);
				}
				break;

			case RE::INPUT_DEVICE::kKeyboard:
				if (a_rec.id == 0x2A || a_rec.id == 0x36) {  // shift
					g_shiftDown = a_rec.pressed;
					a_io.AddKeyEvent(ImGuiMod_Shift, a_rec.pressed);
				} else if (a_rec.id == 0x1D || a_rec.id == 0x9D) {  // ctrl
					a_io.AddKeyEvent(ImGuiMod_Ctrl, a_rec.pressed);
				} else if (a_rec.id == 0x38 || a_rec.id == 0xB8) {  // alt
					a_io.AddKeyEvent(ImGuiMod_Alt, a_rec.pressed);
				}

				if (const auto key = DIKToImGuiKey(a_rec.id); key != ImGuiKey_None) {
					a_io.AddKeyEvent(key, a_rec.pressed);
				}

				if (a_rec.down) {
					if (const char c = DIKToChar(a_rec.id, g_shiftDown); c != '\0') {
						a_io.AddInputCharacter(static_cast<unsigned int>(c));
					}
				}
				break;

			default:
				break;
			}
		}
	}

	bool ChainContainsToggle(RE::InputEvent* const* a_events)
	{
		return ChainContainsKeyDown(a_events, Settings::Get().toggleKey);
	}

	bool ChainContainsEscape(RE::InputEvent* const* a_events)
	{
		return ChainContainsKeyDown(a_events, DIK_ESCAPE);
	}

	void RecordEvents(RE::InputEvent* const* a_events)
	{
		std::scoped_lock guard(g_queueLock);
		for (auto* event = *a_events; event; event = event->next) {
			if (g_queue.size() >= kMaxQueuedEvents) {
				return;
			}

			Record rec;
			switch (event->GetEventType()) {
			case RE::INPUT_EVENT_TYPE::kMouseMove: {
				const auto* move = static_cast<const RE::MouseMoveEvent*>(event);
				rec.kind = Record::Kind::kMouseMove;
				rec.dx = static_cast<float>(move->mouseInputX);
				rec.dy = static_cast<float>(move->mouseInputY);
				break;
			}
			case RE::INPUT_EVENT_TYPE::kButton: {
				const auto* button = static_cast<const RE::ButtonEvent*>(event);
				rec.kind = Record::Kind::kButton;
				rec.device = button->GetDevice();
				rec.id = button->GetIDCode();
				rec.value = button->Value();
				rec.pressed = button->IsPressed();
				rec.down = button->IsDown();
				break;
			}
			default:
				continue;
			}
			g_queue.push_back(rec);
		}
	}

	void NotifyOverlayOpened()
	{
		{
			std::scoped_lock guard(g_queueLock);
			g_queue.clear();
		}
		g_resetPending.store(true, std::memory_order_release);
	}

	void DrainToImGui()
	{
		ImGuiIO& io = ImGui::GetIO();

		if (g_resetPending.exchange(false, std::memory_order_acq_rel)) {
			io.ClearInputKeys();
			g_shiftDown = false;
			g_mouseX = io.DisplaySize.x * 0.5f;
			g_mouseY = io.DisplaySize.y * 0.5f;
			io.AddMousePosEvent(g_mouseX, g_mouseY);
		}

		std::vector<Record> local;
		{
			std::scoped_lock guard(g_queueLock);
			local.swap(g_queue);
		}
		for (const auto& rec : local) {
			Apply(rec, io);
		}
	}
}
