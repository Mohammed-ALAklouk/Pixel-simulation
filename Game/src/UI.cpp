#include "Game.h"
#include "UI.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <cstdint>
#include <cstring>
#include <string>

using namespace scree;
namespace c = scree::ui::col;
namespace m = scree::ui::metrics;

ImFont* scree::ui::Font = nullptr;

#if defined(_WIN32)
// Declared by hand rather than including <windows.h>, which collides with raylib over
// Rectangle, CloseWindow and ShowCursor among others. raylib's own OpenURL is not usable
// here: it runs system("explorer ...") -- that spawns a console this exe deliberately does
// not have, and explorer rejects the forward slashes GetApplicationDirectory returns.
extern "C" __declspec(dllimport) void* __stdcall ShellExecuteA(
	void* hwnd, const char* op, const char* file, const char* params,
	const char* dir, int showCmd);
#if defined(_MSC_VER)
#pragma comment(lib, "shell32.lib")
#endif
#endif

namespace
{
	ImU32 U32(const ImVec4& v) { return ImGui::GetColorU32(v); }

	// Design units to pixels. Every offset, padding and control size below is written at
	// 1.0 and passed through here, so metrics::Scale resizes the whole interface.
	constexpr float S(float v) { return v * m::Scale; }

	// PushFont(nullptr, size) keeps the current face and changes only the size, which is
	// what happens when no system font was found.
	struct FontScope
	{
		explicit FontScope(float size) { ImGui::PushFont(scree::ui::Font, size); }
		~FontScope() { ImGui::PopFont(); }
	};

	float TextW(const char* text) { return ImGui::CalcTextSize(text).x; }

	// Bytes in the UTF-8 sequence starting at this lead byte. Stepping a byte at a time
	// would hand ImGui half a codepoint, which it draws as one '?' per byte.
	int GlyphBytes(const char* ch)
	{
		const unsigned char lead = static_cast<unsigned char>(*ch);
		if (lead < 0x80) return 1;
		if ((lead & 0xE0) == 0xC0) return 2;
		if ((lead & 0xF0) == 0xE0) return 3;
		if ((lead & 0xF8) == 0xF0) return 4;
		return 1;
	}

	float TrackedW(const char* text, float tracking)
	{
		float w = 0.0f;
		for (const char* ch = text; *ch; )
		{
			const int n = GlyphBytes(ch);
			char glyph[5] = {};
			std::memcpy(glyph, ch, n);
			w += ImGui::CalcTextSize(glyph).x + tracking;
			ch += n;
		}
		return w > 0.0f ? w - tracking : 0.0f;
	}

	// ImGui has no letter-spacing and the design leans on it for every small header,
	// so the string goes down a glyph at a time.
	void DrawTracked(ImDrawList* dl, ImVec2 pos, const char* text, float tracking, const ImVec4& colour)
	{
		const ImU32 col = U32(colour);
		float x = pos.x;
		for (const char* ch = text; *ch; )
		{
			const int n = GlyphBytes(ch);
			char glyph[5] = {};
			std::memcpy(glyph, ch, n);
			dl->AddText(ImVec2(x, pos.y), col, glyph);
			x += ImGui::CalcTextSize(glyph).x + tracking;
			ch += n;
		}
	}

	void DrawLabel(ImDrawList* dl, ImVec2 pos, const char* text, const ImVec4& colour)
	{
		dl->AddText(pos, U32(colour), text);
	}

	// Vertically centres an item of `itemH` inside a bar of `barH`.
	float CenterY(float barH, float itemH) { return (barH - itemH) * 0.5f; }

	bool BeginChrome(const char* id, ImVec2 pos, ImVec2 size, const ImVec4& bg, bool scroll = false)
	{
		ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
			ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
			ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
			ImGuiWindowFlags_NoNavFocus;
		if (!scroll) flags |= ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

		ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
		ImGui::SetNextWindowSize(size, ImGuiCond_Always);
		ImGui::PushStyleColor(ImGuiCol_WindowBg, bg);
		return ImGui::Begin(id, nullptr, flags);
	}

	void EndChrome()
	{
		ImGui::End();
		ImGui::PopStyleColor();
	}

	bool ChromeButton(const char* label, ImVec2 size, bool active = false)
	{
		ImGui::PushStyleColor(ImGuiCol_Button, active ? ui::theme.AccentBg : ui::theme.Button);
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, active ? ui::theme.AccentBg : ui::theme.ButtonHover);
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ui::theme.AccentBg);
		ImGui::PushStyleColor(ImGuiCol_Border, active ? ui::theme.AccentBorder : ui::theme.ButtonBorder);
		ImGui::PushStyleColor(ImGuiCol_Text, active ? ui::theme.AccentLight : ui::theme.TextDim);
		const bool pressed = ImGui::Button(label, size);
		ImGui::PopStyleColor(5);
		return pressed;
	}

	const char* MaterialClass(const MaterialData& data, MaterialID id)
	{
		if (id == MaterialRegistry::AIR_ID)      return "void";
		if (data.movement.Y_direction < 0)       return "gas";
		if (data.movement.is_liquid)             return "liquid";
		if (data.movement.can_cascade)           return "powder";
		if (data.movement.can_fall)              return "grain";
		return "solid";
	}

	ImVec4 SwatchColour(const MaterialData& data)
	{
		const RGB mid = scree::ui::SwatchRGB(data);
		return scree::ui::hex(mid.r, mid.g, mid.b);
	}

	// Hands the file to whatever the desktop has registered for it.
	void OpenInEditor(std::string path)
	{
#if defined(_WIN32)
		for (char& ch : path) if (ch == '/') ch = '\\';
		// A return of 32 or less is a failure code, not a handle. The usual one here is
		// SE_ERR_NOASSOC: plenty of machines have nothing registered for .json, and "open"
		// then does nothing at all. "openas" puts up the Open With picker instead, so the
		// click always leads somewhere.
		const auto rc = reinterpret_cast<std::intptr_t>(
			ShellExecuteA(nullptr, "open", path.c_str(), nullptr, nullptr, 1 /* SW_SHOWNORMAL */));
		if (rc <= 32)
			ShellExecuteA(nullptr, "openas", path.c_str(), nullptr, nullptr, 1);
#else
		OpenURL(path.c_str());
#endif
	}

	ImVec4 Mix(const ImVec4& from, const ImVec4& to, float t)
	{
		return ImVec4(from.x + (to.x - from.x) * t,
			from.y + (to.y - from.y) * t,
			from.z + (to.z - from.z) * t,
			from.w + (to.w - from.w) * t);
	}

	// ---------------------------------------------------------------- top bar

	void ThemePicker(ImVec2 org, float x, float y, float w);

	void TopBar(Game& g)
	{
		const float W = static_cast<float>(g.Window_size.x);
		const float H = m::TopBarH;

		BeginChrome("##topbar", ImVec2(0, 0), ImVec2(W, H), ui::theme.Panel);
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const ImVec2 org = ImGui::GetWindowPos();
		dl->AddLine(ImVec2(org.x, org.y + H - 0.5f), ImVec2(org.x + W, org.y + H - 0.5f), U32(ui::theme.Border));

		// --- left: wordmark, then the theme picker beside it
		float x = S(14.0f);
		{
			FontScope f(m::FontTitle);
			const float th = ImGui::GetTextLineHeight();
			DrawTracked(dl, ImVec2(org.x + x, org.y + CenterY(H, th)), "SCREE", m::TrackTitle, ui::theme.Text);
			x += TrackedW("SCREE", m::TrackTitle) + S(9.0f);
		}
		{
			FontScope f(m::FontLabel);
			const float th = ImGui::GetTextLineHeight();
			DrawTracked(dl, ImVec2(org.x + x, org.y + CenterY(H, th) + S(2.0f)), "640\xc2\xb2", m::TrackLabel, ui::theme.TextFaint);
			x += TrackedW("640\xc2\xb2", m::TrackLabel) + S(16.0f);
		}
		{
			FontScope f(m::FontSmall);
			const float pickerW = S(140.0f);
			ThemePicker(org, x, CenterY(H, S(31.0f)), pickerW);
			x += pickerW;
		}
		const float leftEnd = x;

		// --- right: the materials file, then the frame rate at the very edge
		float r = W - S(14.0f);
		{
			FontScope f(m::FontSmall);
			const float th = ImGui::GetTextLineHeight();

			char fps[32];
			std::snprintf(fps, sizeof(fps), "%.0f fps", g.fps_display);
			const float fpsW = S(82.0f);
			r -= fpsW;
			DrawLabel(dl, ImVec2(org.x + r + fpsW - TextW(fps), org.y + CenterY(H, th)), fps,
				g.fps_display >= 55.0f ? c::Good : c::Warn);
			r -= S(14.0f);

			const char* name = "materials.json";
			const float reloadW = S(94.0f);
			const float chipH = S(39.0f);
			const float chipW = S(14.0f) + TextW(name) + S(10.0f) + reloadW + S(8.0f);
			r -= chipW;

			const ImVec2 chipMin(org.x + r, org.y + CenterY(H, chipH));
			const ImVec2 chipMax(chipMin.x + chipW, chipMin.y + chipH);
			dl->AddRectFilled(chipMin, chipMax, U32(ui::theme.Button), S(3.0f));
			dl->AddRect(chipMin, chipMax, U32(ui::theme.ButtonBorder), S(3.0f));
			// The file name opens the file in whatever the desktop has registered for it.
			const float ty = chipMin.y + (chipH - th) * 0.5f;
			const float nameW = TextW(name);
			ImGui::SetCursorPos(ImVec2(r + S(14.0f), ty - org.y));
			if (ImGui::InvisibleButton("##openjson", ImVec2(nameW, th)))
				OpenInEditor(g.materials_path());

			const bool hot = ImGui::IsItemHovered();
			if (hot) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
			DrawLabel(dl, ImVec2(chipMin.x + S(14.0f), ty), name,
				hot ? ui::theme.AccentLight : ui::theme.TextDim);
			if (hot)
				dl->AddLine(ImVec2(chipMin.x + S(14.0f), ty + th - S(2.0f)),
					ImVec2(chipMin.x + S(14.0f) + nameW, ty + th - S(2.0f)), U32(ui::theme.AccentLight));

			ImGui::SetCursorPos(ImVec2(r + S(14.0f) + nameW + S(10.0f), CenterY(H, S(31.0f))));
			ImGui::PushStyleColor(ImGuiCol_Text, ui::theme.AccentLight);
			if (ChromeButton("RELOAD", ImVec2(reloadW, S(31.0f))))
				g.load_materials();
			ImGui::PopStyleColor();
		}
		const float rightStart = r;

		// --- centre: run state and speed, centred in the window and pushed off the two
		// clusters above only when the window is too narrow to hold all three apart.
		{
			FontScope f(m::FontSmall);
			const float btnH = S(37.0f);
			const float runW = S(154.0f), stepW = S(79.0f), clearW = S(87.0f), gap = S(7.0f);
			const float stepBtn = S(30.0f), stepVal = S(40.0f);

			float labelW = 0.0f;
			{
				FontScope l(m::FontLabel);
				labelW = TrackedW("STEPS", m::TrackLabel);
			}
			const float groupW = runW + gap + stepW + gap + clearW + S(18.0f) + S(18.0f)
				+ labelW + S(10.0f) + stepBtn * 2.0f + stepVal;

			float cx = (W - groupW) * 0.5f;
			cx = std::max(cx, leftEnd + S(20.0f));
			cx = std::min(cx, rightStart - groupW - S(20.0f));

			ImGui::SetCursorPos(ImVec2(cx, CenterY(H, btnH)));
			if (ChromeButton(g.paused ? "    PAUSED" : "    RUNNING", ImVec2(runW, btnH)))
				g.paused = !g.paused;
			const ImVec2 bmin = ImGui::GetItemRectMin();
			dl->AddRectFilled(ImVec2(bmin.x + S(14.0f), bmin.y + btnH * 0.5f - S(4.0f)),
				ImVec2(bmin.x + S(22.0f), bmin.y + btnH * 0.5f + S(4.0f)),
				U32(g.paused ? c::Warn : c::Good), S(1.0f));
			cx += runW + gap;

			ImGui::SetCursorPos(ImVec2(cx, CenterY(H, btnH)));
			ImGui::BeginDisabled(!g.paused);
			if (ChromeButton("STEP", ImVec2(stepW, btnH)))
				g.step_once = true;
			ImGui::EndDisabled();
			cx += stepW + gap;

			ImGui::SetCursorPos(ImVec2(cx, CenterY(H, btnH)));
			if (ChromeButton("CLEAR", ImVec2(clearW, btnH)))
				g.grid.Clear();
			cx += clearW + S(18.0f);

			dl->AddLine(ImVec2(org.x + cx, org.y + H * 0.5f - S(10.0f)),
				ImVec2(org.x + cx, org.y + H * 0.5f + S(10.0f)), U32(ui::theme.Border));
			cx += S(18.0f);

			{
				FontScope l(m::FontLabel);
				const float lh = ImGui::GetTextLineHeight();
				DrawTracked(dl, ImVec2(org.x + cx, org.y + CenterY(H, lh)), "STEPS", m::TrackLabel,
					ui::theme.TextFaint);
			}
			cx += labelW + S(10.0f);

			ImGui::SetCursorPos(ImVec2(cx, CenterY(H, stepBtn)));
			if (ChromeButton("-##stepsdown", ImVec2(stepBtn, stepBtn)) && g.updates_per_frame > 1)
				g.updates_per_frame--;

			char steps[16];
			std::snprintf(steps, sizeof(steps), "%d", g.updates_per_frame);
			const float th = ImGui::GetTextLineHeight();
			DrawLabel(dl, ImVec2(org.x + cx + stepBtn + (stepVal - TextW(steps)) * 0.5f,
				org.y + CenterY(H, th)), steps, ui::theme.Text);

			ImGui::SetCursorPos(ImVec2(cx + stepBtn + stepVal, CenterY(H, stepBtn)));
			if (ChromeButton("+##stepsup", ImVec2(stepBtn, stepBtn)) && g.updates_per_frame < 10)
				g.updates_per_frame++;
		}

		EndChrome();
	}

	// ----------------------------------------------------------------- banner

	void Banner(Game& g)
	{
		if (g.layout_banner_h <= 0.0f) return;

		const float W = static_cast<float>(g.Window_size.x);
		const float H = g.layout_banner_h;
		const ImVec4& edge = g.material_load_failed ? c::Bad : c::Warn;

		BeginChrome("##banner", ImVec2(0, m::TopBarH), ImVec2(W, H), ui::theme.PanelSunk, true);
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const ImVec2 org = ImGui::GetWindowPos();
		dl->AddRectFilled(ImVec2(org.x, org.y), ImVec2(org.x + S(3.0f), org.y + H), U32(edge));
		dl->AddLine(ImVec2(org.x, org.y + H - 0.5f), ImVec2(org.x + W, org.y + H - 0.5f), U32(ui::theme.Border));

		{
			FontScope f(m::FontLabel);
			const char* tier = g.material_load_failed ? "LOAD FAILED" : "LOADER";
			DrawTracked(dl, ImVec2(org.x + S(16.0f), org.y + S(10.0f)), tier, m::TrackLabel, edge);
		}

		{
			FontScope f(m::FontSmall);
			ImGui::SetCursorPos(ImVec2(m::BannerTextInset, S(8.0f)));
			ImGui::PushStyleColor(ImGuiCol_Text, ui::theme.TextDim);
			ImGui::PushTextWrapPos(W - m::BannerTextRight);
			ImGui::TextUnformatted(g.material_load_error_message.c_str());
			ImGui::PopTextWrapPos();
			ImGui::PopStyleColor();
		}

		{
			FontScope f(m::FontLabel);
			ImGui::SetCursorPos(ImVec2(W - S(103.0f), S(11.0f)));
			if (ChromeButton("DISMISS", ImVec2(S(87.0f), S(31.0f))))
				g.material_load_error_message.clear();
		}

		EndChrome();
	}

	// -------------------------------------------------------------- left rail

	void LeftRail(Game& g)
	{
		const float top = m::TopBarH + g.layout_banner_h;
		const float H = static_cast<float>(g.Window_size.y) - top;
		const float W = m::LeftRailW;
		const float footerH = S(88.0f);

		BeginChrome("##rail", ImVec2(0, top), ImVec2(W, H), ui::theme.Panel);
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const ImVec2 org = ImGui::GetWindowPos();
		dl->AddLine(ImVec2(org.x + W - 0.5f, org.y), ImVec2(org.x + W - 0.5f, org.y + H), U32(ui::theme.Border));

		{
			FontScope f(m::FontLabel);
			DrawTracked(dl, ImVec2(org.x + S(12.0f), org.y + S(12.0f)), "MATERIALS", m::TrackLabel, ui::theme.TextFaint);
			const float hint = TrackedW("1-9", m::TrackLabel);
			DrawTracked(dl, ImVec2(org.x + W - S(12.0f) - hint, org.y + S(12.0f)), "1-9", m::TrackLabel, ui::theme.TextGhost);
		}

		ImGui::SetCursorPos(ImVec2(S(6.0f), S(42.0f)));
		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
		ImGui::BeginChild("##matlist", ImVec2(W - S(12.0f), H - S(42.0f) - footerH), 0,
			ImGuiWindowFlags_NoSavedSettings);
		{
			ImDrawList* ldl = ImGui::GetWindowDrawList();
			const float rowH = m::RowH;
			// The rows carry their own padding, so the global item spacing would only add a
			// second gap on top of it and leave the list looking sparse.
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 2.0f));

			for (int i = 0; i < g.material_registry.GetMaterialsCount(); i++)
			{
				const MaterialID id = static_cast<MaterialID>(i);
				const MaterialData& data = g.material_registry.Get(id);
				const bool selected = (id == g.SelectedMaterial);

				ImGui::PushID(i);
				const ImVec2 p = ImGui::GetCursorScreenPos();
				const float rowW = ImGui::GetContentRegionAvail().x;
				if (ImGui::InvisibleButton("row", ImVec2(rowW, rowH)))
					g.SelectedMaterial = id;
				const bool hovered = ImGui::IsItemHovered();

				if (selected)
				{
					ldl->AddRectFilled(p, ImVec2(p.x + rowW, p.y + rowH), U32(ui::theme.AccentBg), 2.0f);
					ldl->AddRectFilled(p, ImVec2(p.x + 2.0f, p.y + rowH), U32(ui::theme.Accent));
				}
				else if (hovered)
				{
					ldl->AddRectFilled(p, ImVec2(p.x + rowW, p.y + rowH), U32(ui::theme.RowHover), 2.0f);
				}

				// Snapped to whole pixels: the swatch's centred Y was landing on a half pixel,
				// so the fill's anti-aliased corners spilled past the 1px stroke at the bottom.
				// The border is a slightly larger filled rect drawn behind the fill rather than
				// a stroke over it, so the fill can never bleed past it whatever the corner
				// rounding does.
				const float ss = S(17.0f);
				const float sx = std::floor(p.x + S(9.0f));
				const float sy = std::floor(p.y + (rowH - ss) * 0.5f);
				ldl->AddRectFilled(ImVec2(sx, sy), ImVec2(sx + ss, sy + ss), U32(ui::theme.ButtonBorder), S(3.0f));
				ldl->AddRectFilled(ImVec2(sx + 1.0f, sy + 1.0f), ImVec2(sx + ss - 1.0f, sy + ss - 1.0f),
					U32(SwatchColour(data)), S(2.0f));

				{
					FontScope f(m::FontBody);
					const float th = ImGui::GetTextLineHeight();
					DrawLabel(ldl, ImVec2(p.x + S(36.0f), p.y + (rowH - th) * 0.5f),
						g.material_registry.GetName(id).c_str(), selected ? ui::theme.AccentText : ui::theme.TextDim);
				}
				{
					FontScope f(m::FontLabel);
					const float th = ImGui::GetTextLineHeight();
					const float ty = p.y + (rowH - th) * 0.5f;

					if (i >= 1 && i <= 9)
					{
						char key[4];
						std::snprintf(key, sizeof(key), "%d", i);
						DrawLabel(ldl, ImVec2(p.x + rowW - S(8.0f) - TextW(key), ty), key, ui::theme.TextGhost);
					}

					const char* cls = MaterialClass(data, id);
					DrawLabel(ldl, ImVec2(p.x + rowW - S(31.0f) - TextW(cls), ty), cls, ui::theme.TextMute);
				}

				ImGui::PopID();
			}
			ImGui::PopStyleVar();
		}
		ImGui::EndChild();
		ImGui::PopStyleColor();

		// --- footer readouts
		{
			const float fy = H - footerH;
			dl->AddLine(ImVec2(org.x, org.y + fy), ImVec2(org.x + W, org.y + fy), U32(ui::theme.BorderSoft));

			FontScope f(m::FontLabel);
			const float th = ImGui::GetTextLineHeight();

			char loaded[48];
			std::snprintf(loaded, sizeof(loaded), "%d mats / %d tags",
				g.material_registry.GetMaterialsCount() - 1, g.material_registry.GetTagsCount());
			char particles[32];
			std::snprintf(particles, sizeof(particles), "%d", g.particle_count);
			char chunks[32];
			std::snprintf(chunks, sizeof(chunks), "%d/%d", g.awake_chunk_count,
				g.grid.Get_width_chunks() * g.grid.Get_height_chunks());

			const float r1 = org.y + fy + S(7.0f);
			const float r2 = r1 + th + S(3.0f);
			const float r3 = r2 + th + S(3.0f);
			DrawLabel(dl, ImVec2(org.x + S(12.0f), r1), "materials", ui::theme.TextMute);
			DrawLabel(dl, ImVec2(org.x + W - S(12.0f) - TextW(loaded), r1), loaded, ui::theme.TextDim);
			DrawLabel(dl, ImVec2(org.x + S(12.0f), r2), "particles", ui::theme.TextMute);
			DrawLabel(dl, ImVec2(org.x + W - S(12.0f) - TextW(particles), r2), particles, ui::theme.TextDim);
			DrawLabel(dl, ImVec2(org.x + S(12.0f), r3), "chunks awake", ui::theme.TextMute);
			DrawLabel(dl, ImVec2(org.x + W - S(12.0f) - TextW(chunks), r3), chunks, ui::theme.TextDim);
		}

		EndChrome();
	}

	// Swatch plus name, both in the combo box and in each row of its list. `org` is the
	// owning window's position, since the box is placed in window-local coordinates but
	// the swatch is drawn in screen ones.
	void ThemePicker(ImVec2 org, float x, float y, float w)
	{
		ImGui::PushStyleColor(ImGuiCol_FrameBg, ui::theme.Button);
		ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ui::theme.ButtonHover);
		ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ui::theme.Button);
		ImGui::PushStyleColor(ImGuiCol_Border, ui::theme.ButtonBorder);
		ImGui::PushStyleColor(ImGuiCol_Text, ui::theme.TextDim);
		ImGui::PushStyleColor(ImGuiCol_PopupBg, ui::theme.Panel);
		ImGui::PushStyleColor(ImGuiCol_Header, ui::theme.AccentBg);
		ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ui::theme.RowHover);
		ImGui::PushStyleColor(ImGuiCol_HeaderActive, ui::theme.AccentBg);

		ImGui::SetCursorPos(ImVec2(x, y));
		ImGui::SetNextItemWidth(w);

		// Leading spaces reserve room for the swatch drawn over the box below; a combo's
		// preview takes a string and nothing else.
		char preview[32];
		std::snprintf(preview, sizeof(preview), "    %s", ui::ThemeOptions[ui::ActiveTheme].name);

		// The global window padding is zero so the docked bars can sit flush; the popup is
		// the one window that needs its own breathing room back.
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, S(4.0f)));
		const bool open = ImGui::BeginCombo("##theme", preview, ImGuiComboFlags_HeightLargest);
		ImGui::PopStyleVar();

		if (open)
		{
			for (int i = 0; i < ui::ThemeCount; i++)
			{
				ImGui::PushID(i);
				const bool selected = (i == ui::ActiveTheme);
				if (ImGui::Selectable("##opt", selected, 0, ImVec2(0.0f, S(25.0f))))
				{
					ui::ActiveTheme = i;
					ui::SaveSettings();   // persist the pick so it survives the next launch
				}

				const ImVec2 p = ImGui::GetItemRectMin();
				ImDrawList* pdl = ImGui::GetWindowDrawList();
				const RGB& seed = ui::ThemeOptions[i].seed;
				pdl->AddRectFilled(ImVec2(p.x + S(7.0f), p.y + S(7.0f)), ImVec2(p.x + S(22.0f), p.y + S(22.0f)),
					U32(scree::ui::hex(seed.r, seed.g, seed.b)), 2.0f);
				pdl->AddText(ImVec2(p.x + S(29.0f), p.y + S(2.0f)),
					U32(selected ? ui::theme.AccentText : ui::theme.TextDim), ui::ThemeOptions[i].name);
				ImGui::PopID();
			}
			ImGui::EndCombo();
		}

		// Back in the owning window now, so this lands on the box rather than the popup.
		const RGB& active = ui::ThemeOptions[ui::ActiveTheme].seed;
		ImDrawList* dl = ImGui::GetWindowDrawList();
		dl->AddRectFilled(ImVec2(org.x + x + S(8.0f), org.y + y + S(9.0f)),
			ImVec2(org.x + x + S(23.0f), org.y + y + S(24.0f)),
			U32(scree::ui::hex(active.r, active.g, active.b)), 2.0f);

		ImGui::PopStyleColor(9);
	}

	// ------------------------------------------------------------- bottom bar

	void BottomBar(Game& g)
	{
		const float W = static_cast<float>(g.Window_size.x) - m::LeftRailW;
		const float H = m::BottomBarH;
		const float y = static_cast<float>(g.Window_size.y) - H;

		BeginChrome("##bottombar", ImVec2(m::LeftRailW, y), ImVec2(W, H), ui::theme.Panel);
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const ImVec2 org = ImGui::GetWindowPos();
		dl->AddLine(ImVec2(org.x, org.y + 0.5f), ImVec2(org.x + W, org.y + 0.5f), U32(ui::theme.Border));

		float x = S(14.0f);
		{
			FontScope f(m::FontLabel);
			const float th = ImGui::GetTextLineHeight();
			DrawTracked(dl, ImVec2(org.x + x, org.y + CenterY(H, th)), "BRUSH", m::TrackLabel, ui::theme.TextFaint);
			x += TrackedW("BRUSH", m::TrackLabel) + S(10.0f);
		}
		{
			FontScope f(m::FontSmall);
			ImGui::SetCursorPos(ImVec2(x, CenterY(H, S(25.0f))));
			ImGui::SetNextItemWidth(S(190.0f));
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f, 2.0f));
			ImGui::PushStyleColor(ImGuiCol_FrameBg, ui::theme.Button);
			ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ui::theme.ButtonHover);
			ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ui::theme.Button);
			ImGui::PushStyleColor(ImGuiCol_SliderGrab, ui::theme.Accent);
			ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ui::theme.AccentLight);
			ImGui::SliderFloat("##brush", &g.cursor_radius, 1.0f, 100.0f, "");
			ImGui::PopStyleColor(5);
			ImGui::PopStyleVar();
			x += S(190.0f) + S(12.0f);

			char radius[16];
			std::snprintf(radius, sizeof(radius), "%d", static_cast<int>(g.cursor_radius));
			const float th = ImGui::GetTextLineHeight();
			DrawLabel(dl, ImVec2(org.x + x, org.y + CenterY(H, th)), radius, ui::theme.TextDim);
		}

		{
			FontScope f(m::FontLabel);
			float r = W - S(14.0f);
			const float btnH = S(35.0f);

			r -= S(138.0f);
			ImGui::SetCursorPos(ImVec2(r, CenterY(H, btnH)));
			if (ChromeButton("BENCHMARK", ImVec2(S(138.0f), btnH), g.show_bench_marks))
				g.show_bench_marks = !g.show_bench_marks;

			r -= S(8.0f) + S(172.0f);
			ImGui::SetCursorPos(ImVec2(r, CenterY(H, btnH)));
			if (ChromeButton("CHUNK OVERLAY", ImVec2(S(172.0f), btnH), g.show_active_chunks))
				g.show_active_chunks = !g.show_active_chunks;

		}

		EndChrome();
	}

	// -------------------------------------------------- overlays over the grid

	void Chip(ImDrawList* dl, float& x, float y, const char* text, const ImVec4& colour)
	{
		const float pad = S(7.0f);
		const float th = ImGui::GetTextLineHeight();
		const float w = TextW(text) + pad * 2.0f;
		const float h = th + S(6.0f);
		dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), U32(ui::theme.Overlay), 2.0f);
		dl->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), U32(ui::theme.OverlayEdge), 2.0f);
		dl->AddText(ImVec2(x + pad, y + S(3.0f)), U32(colour), text);
		x += w + S(6.0f);
	}

	void CanvasOverlays(Game& g)
	{
		// Background, not foreground: this still lands on top of the grid, which raylib drew
		// before ImGui started, but stays under the panels and under any popup. On the
		// foreground list the hint chip painted straight over the open theme dropdown.
		ImDrawList* dl = ImGui::GetBackgroundDrawList();
		const Rectangle& f = g.frame;

		FontScope fs(m::FontSmall);

		float x = f.x + S(10.0f);
		const float y = f.y + S(10.0f);

		Chip(dl, x, y, g.material_registry.GetName(g.SelectedMaterial).c_str(), ui::theme.Text);

		char brush[24];
		std::snprintf(brush, sizeof(brush), "r%d", static_cast<int>(g.cursor_radius));
		Chip(dl, x, y, brush, ui::theme.TextMute);

		Vector2 mouse = GetMousePosition();
		const int gx = static_cast<int>((mouse.x - g.Grid_offset.x) / g.TileSize);
		const int gy = static_cast<int>((mouse.y - g.Grid_offset.y) / g.TileSize);
		char cursor[32];
		if (g.grid.Is_in_bounds(gx, gy))
			std::snprintf(cursor, sizeof(cursor), "%d,%d", gx, gy);
		else
			std::snprintf(cursor, sizeof(cursor), "-,-");
		Chip(dl, x, y, cursor, ui::theme.TextMute);

		// Material under the cursor, only while it is over the grid. Labelled because it is
		// otherwise a second bare name sitting next to the selected one.
		if (g.grid.Is_in_bounds(gx, gy))
		{
			const Block& tile = g.grid.Get_at(static_cast<std::uint16_t>(gx), static_cast<std::uint16_t>(gy));
			if (tile.id != MaterialRegistry::AIR_ID)
			{
				char under[64];
				std::snprintf(under, sizeof(under), "under %s",
					g.material_registry.GetName(tile.id).c_str());
				Chip(dl, x, y, under, ui::theme.TextDim);
			}
		}

		{
			FontScope small(m::FontLabel);
			const char* hint = "L draw \xc2\xb7 R erase \xc2\xb7 wheel brush \xc2\xb7 space pause";
			const float th = ImGui::GetTextLineHeight();
			const float w = TextW(hint) + S(14.0f);
			const float h = th + S(6.0f);
			const float hx = f.x + f.width - S(10.0f) - w;
			const float hy = f.y + f.height - S(10.0f) - h;
			dl->AddRectFilled(ImVec2(hx, hy), ImVec2(hx + w, hy + h), U32(ui::theme.Overlay), 2.0f);
			dl->AddRect(ImVec2(hx, hy), ImVec2(hx + w, hy + h), U32(ui::theme.OverlayEdge), 2.0f);
			dl->AddText(ImVec2(hx + S(7.0f), hy + S(3.0f)), U32(ui::theme.TextMute), hint);
		}
	}

	// -------------------------------------------------------------- benchmark

	void BenchmarkPanel(Game& g)
	{
		if (!g.show_bench_marks) return;

		const float W = S(306.0f);
		const float H = S(207.0f);
		const float x = m::LeftRailW + S(18.0f);
		const float y = static_cast<float>(g.Window_size.y) - m::BottomBarH - H - S(18.0f);

		BeginChrome("##bench", ImVec2(x, y), ImVec2(W, H), ui::theme.PanelSunk);
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const ImVec2 org = ImGui::GetWindowPos();
		dl->AddRect(org, ImVec2(org.x + W, org.y + H), U32(ui::theme.OverlayEdge), S(3.0f));

		{
			FontScope f(m::FontLabel);
			DrawTracked(dl, ImVec2(org.x + S(12.0f), org.y + 11.0f), "BENCHMARK", m::TrackLabel, ui::theme.TextFaint);
		}

		struct Row { const char* key; float ms; };
		const Row rows[] = {
			{ "input",  g.input_time * S(1000.0f) },
			{ "update", g.update_time * S(1000.0f) },
			{ "render", g.render_time * S(1000.0f) },
			{ "ui",     g.UI_time * S(1000.0f) },
		};
		const float frame = std::max(0.001f, g.delta * S(1000.0f));

		// Scoped so the font is popped while this window is still current: ImGui checks the
		// stacks at End() and reports a mismatch if a PopFont lands after it.
		{
			FontScope f(m::FontSmall);
			const float th = ImGui::GetTextLineHeight();
			float ry = org.y + S(32.0f);

			for (const Row& row : rows)
			{
				DrawLabel(dl, ImVec2(org.x + S(12.0f), ry), row.key, ui::theme.TextMute);

				const float barX = org.x + S(79.0f);
				const float barW = S(110.0f);
				const float barY = ry + th * 0.5f - S(1.5f);
				dl->AddRectFilled(ImVec2(barX, barY), ImVec2(barX + barW, barY + S(3.0f)), U32(ui::theme.Border), 2.0f);
				const float fill = std::min(1.0f, row.ms / frame) * barW;
				dl->AddRectFilled(ImVec2(barX, barY), ImVec2(barX + fill, barY + S(3.0f)), U32(ui::theme.Accent), 2.0f);

				char value[24];
				std::snprintf(value, sizeof(value), "%.2f", row.ms);
				DrawLabel(dl, ImVec2(org.x + W - S(12.0f) - TextW(value), ry), value, ui::theme.TextDim);

				ry += th + S(5.0f);
			}

			char total[32];
			std::snprintf(total, sizeof(total), "%.2f ms", frame);
			DrawLabel(dl, ImVec2(org.x + S(12.0f), ry + S(4.0f)), "frame", ui::theme.TextMute);
			DrawLabel(dl, ImVec2(org.x + W - S(12.0f) - TextW(total), ry + S(4.0f)), total, ui::theme.Text);
		}

		EndChrome();
	}
}

// ---------------------------------------------------------------------- theme

scree::ui::Theme scree::ui::theme;

const scree::ui::ThemeOption scree::ui::ThemeOptions[scree::ui::ThemeCount] = {
	{ "Blue",   RGB(0x2e, 0x6c, 0xf0) },
	{ "Cyan",   RGB(0x20, 0xbe, 0xc8) },
	{ "Green",  RGB(0x56, 0xc8, 0x46) },
	{ "Amber",  RGB(0xff, 0x96, 0x28) },
	{ "Red",    RGB(0xe6, 0x46, 0x37) },
	{ "Violet", RGB(0x96, 0x64, 0xf0) },
	{ "Bone",   RGB(0x96, 0x8c, 0x82) },
};

int scree::ui::ActiveTheme = 0;

// The midpoint is the average look either way -- of the random spread for most
// materials, and of the fade for an interpolating one. Fire's maxColor on its own is
// pure yellow, which is indistinguishable from Sand at swatch size.
scree::RGB scree::ui::SwatchRGB(const MaterialData& data)
{
	return RGB::lerp(data.minColor, data.maxColor, 0.5f);
}

namespace
{
	// Re-hues a neutral: the base contributes its lightness, the material its hue and
	// saturation. Mixing towards the material colour instead would drag every dark surface
	// up towards the material's brightness and wash the panels out.
	//
	// `lift` exists because the base panels sit at a value of about 0.06, and a colour that
	// close to black has almost no room to carry a hue -- tinting alone moved them by only
	// a few points per channel. Raising the value of the large flat surfaces gives the hue
	// somewhere to show without making them stop reading as dark chrome.
	ImVec4 Retint(const ImVec4& base, float hue, float sat, float lift = 1.0f)
	{
		float h = 0.0f, s = 0.0f, v = 0.0f;
		ImGui::ColorConvertRGBtoHSV(base.x, base.y, base.z, h, s, v);

		float r = 0.0f, g = 0.0f, b = 0.0f;
		ImGui::ColorConvertHSVtoRGB(hue, sat, std::min(1.0f, v * lift), r, g, b);
		return ImVec4(r, g, b, base.w);
	}

	Color ToRaylib(const ImVec4& v, int alpha = 255)
	{
		return scree::ui::hex_rl(static_cast<int>(v.x * 255.0f),
			static_cast<int>(v.y * 255.0f),
			static_cast<int>(v.z * 255.0f), alpha);
	}
}

void scree::ui::UpdateTheme(const RGB& material_colour, float dt)
{
	// Eased in RGB rather than jumped, so switching material sweeps the chrome across
	// instead of flashing. Held between calls; seeded from the base accent in ApplyTheme.
	static ImVec4 current = col::Accent;

	const ImVec4 target = hex(material_colour.r, material_colour.g, material_colour.b);
	const float k = std::clamp(dt * 11.0f, 0.0f, 1.0f);
	current = Mix(current, target, k);

	float hue = 0.0f, sat = 0.0f, val = 0.0f;
	ImGui::ColorConvertRGBtoHSV(current.x, current.y, current.z, hue, sat, val);

	// Stone, ash, steam and air carry no usable hue, so they borrow the warm one the rest
	// of the design uses and the chrome comes out as the bone tint it started as.
	const bool neutral = sat < 0.16f;
	if (neutral) { hue = 0.06f; sat = 0.10f; }
	else         { sat = std::clamp(sat, 0.46f, 0.90f); }

	// How far each family of surfaces is allowed to take the hue. Backgrounds carry the
	// most because they have the most area at the least contrast; text carries almost
	// none, or it stops reading as text.
	const float surface = neutral ? 0.14f : 0.58f;
	const float edge = neutral ? 0.16f : 0.62f;
	const float label = neutral ? 0.10f : 0.26f;
	const float bright = neutral ? 0.05f : 0.12f;

	theme.Panel = Retint(col::Panel, hue, surface, 1.55f);
	theme.PanelSunk = Retint(col::PanelSunk, hue, surface, 1.55f);
	theme.Border = Retint(col::Border, hue, edge, 1.15f);
	theme.BorderSoft = Retint(col::BorderSoft, hue, edge, 1.15f);
	theme.Button = Retint(col::Button, hue, surface, 1.35f);
	theme.ButtonHover = Retint(col::ButtonHover, hue, surface, 1.35f);
	theme.ButtonBorder = Retint(col::ButtonBorder, hue, edge, 1.15f);
	theme.RowHover = Retint(col::RowHover, hue, surface, 1.35f);
	theme.Overlay = Retint(col::Overlay, hue, surface, 1.40f);
	theme.OverlayEdge = Retint(col::OverlayEdge, hue, edge, 1.15f);

	theme.Text = Retint(col::Text, hue, bright);
	theme.TextDim = Retint(col::TextDim, hue, bright);
	theme.TextMute = Retint(col::TextMute, hue, label);
	theme.TextFaint = Retint(col::TextFaint, hue, label);
	theme.TextGhost = Retint(col::TextGhost, hue, label);

	// The accent itself is the material at full strength, floored in brightness so oil and
	// dirty water still carry text.
	float r = 0.0f, g = 0.0f, b = 0.0f;
	const float lift = std::clamp(val, 0.80f, 1.0f);
	ImGui::ColorConvertHSVtoRGB(hue, sat, lift, r, g, b);
	theme.Accent = ImVec4(r, g, b, 1.0f);

	ImGui::ColorConvertHSVtoRGB(hue, sat * 0.86f, std::min(1.0f, lift * 1.07f), r, g, b);
	theme.AccentLight = ImVec4(r, g, b, 1.0f);

	ImGui::ColorConvertHSVtoRGB(hue, sat * 0.34f, 1.0f, r, g, b);
	theme.AccentText = ImVec4(r, g, b, 1.0f);

	theme.AccentBg = Mix(theme.Panel, theme.Accent, 0.16f);
	theme.AccentBorder = Mix(theme.Panel, theme.Accent, 0.38f);

	theme.WindowBg = ToRaylib(Retint(col::Bg, hue, surface, 1.55f));
	// The canvas is lifted least: it sits directly behind the simulation, and anything
	// brighter starts competing with the particles for attention.
	theme.CanvasBg = ToRaylib(Retint(col::CanvasBase, hue, surface, 1.25f));
	theme.Ring = ToRaylib(theme.AccentLight, 0xc0);

	// The style entries widgets read straight out of, rather than through a push.
	ImVec4* co = ImGui::GetStyle().Colors;
	co[ImGuiCol_WindowBg] = theme.Panel;
	co[ImGuiCol_PopupBg] = theme.Panel;
	co[ImGuiCol_Border] = theme.Border;
	co[ImGuiCol_Text] = theme.Text;
	co[ImGuiCol_TextDisabled] = theme.TextGhost;
	co[ImGuiCol_FrameBg] = theme.Button;
	co[ImGuiCol_FrameBgHovered] = theme.ButtonHover;
	co[ImGuiCol_FrameBgActive] = theme.Button;
	co[ImGuiCol_Button] = theme.Button;
	co[ImGuiCol_ButtonHovered] = theme.ButtonHover;
	co[ImGuiCol_ButtonActive] = theme.AccentBg;
	co[ImGuiCol_SliderGrab] = theme.Accent;
	co[ImGuiCol_SliderGrabActive] = theme.AccentLight;
	co[ImGuiCol_ScrollbarGrab] = theme.ButtonBorder;
	co[ImGuiCol_ScrollbarGrabActive] = theme.Accent;
	co[ImGuiCol_Separator] = theme.Border;
	co[ImGuiCol_CheckMark] = theme.Accent;
}

// ---------------------------------------------------------------------- theme

void scree::ui::ApplyTheme()
{
	ImGuiStyle& s = ImGui::GetStyle();

	s.WindowPadding = ImVec2(0.0f, 0.0f);
	s.WindowBorderSize = 0.0f;
	s.WindowRounding = 0.0f;
	s.ChildRounding = 0.0f;
	s.ChildBorderSize = 0.0f;
	s.FrameRounding = 3.0f;
	s.FrameBorderSize = 1.0f;
	s.FramePadding = ImVec2(9.0f, 5.0f);
	s.ItemSpacing = ImVec2(8.0f, 6.0f);
	s.GrabRounding = 3.0f;
	s.GrabMinSize = 11.0f;
	s.ScrollbarSize = 11.0f;
	s.ScrollbarRounding = 4.0f;
	s.PopupRounding = 2.0f;
	s.DisabledAlpha = 0.38f;

	ImVec4* co = s.Colors;
	co[ImGuiCol_WindowBg] = col::Panel;
	co[ImGuiCol_ChildBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
	co[ImGuiCol_PopupBg] = col::Panel;
	co[ImGuiCol_Border] = col::Border;
	co[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
	co[ImGuiCol_Text] = col::Text;
	co[ImGuiCol_TextDisabled] = col::TextGhost;
	co[ImGuiCol_FrameBg] = col::Button;
	co[ImGuiCol_FrameBgHovered] = col::ButtonHover;
	co[ImGuiCol_FrameBgActive] = col::Button;
	co[ImGuiCol_Button] = col::Button;
	co[ImGuiCol_ButtonHovered] = col::ButtonHover;
	co[ImGuiCol_ButtonActive] = col::AccentBg;
	co[ImGuiCol_SliderGrab] = col::Accent;
	co[ImGuiCol_SliderGrabActive] = col::AccentLight;
	co[ImGuiCol_ScrollbarBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
	co[ImGuiCol_ScrollbarGrab] = col::ButtonBorder;
	co[ImGuiCol_ScrollbarGrabHovered] = col::HoverBorder;
	co[ImGuiCol_ScrollbarGrabActive] = col::Accent;
	co[ImGuiCol_Separator] = col::Border;

	// Fill the theme in immediately: a dt of 1 snaps rather than eases, so the first frame
	// is already on the default theme instead of easing into it from the seed colour.
	UpdateTheme(ThemeOptions[ActiveTheme].seed, 1.0f);
}

void scree::ui::LoadFonts()
{
	// No font ships with the repo, so the host's own UI face is borrowed if one of these is
	// present -- whatever the rest of the desktop is set in. Failing that ImGui's built-in
	// font stands in: sizes still vary, they just render less crisply.
	static const char* candidates[] = {
		"C:/Windows/Fonts/segoeui.ttf",
		"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
		"/usr/share/fonts/TTF/DejaVuSans.ttf",
		"/System/Library/Fonts/SFNS.ttf",
		"/System/Library/Fonts/Helvetica.ttc",
	};

	ImGuiIO& io = ImGui::GetIO();
	for (const char* path : candidates)
	{
		if (!FileExists(path)) continue;
		Font = io.Fonts->AddFontFromFileTTF(path, metrics::FontBody);
		if (Font) break;
	}

	ImGui::GetStyle().FontSizeBase = metrics::FontBody;
}

// ----------------------------------------------------------------- settings

// Next to the executable, not the working directory, so it is found wherever the exe was
// launched from -- the same resolution the materials file uses.
static std::string SettingsPath()
{
	return std::string(GetApplicationDirectory()) + "scree.cfg";
}

void scree::ui::LoadSettings()
{
	// Missing file is the first-run case: nothing to read, defaults stand.
	std::ifstream file(SettingsPath());
	std::string key;
	while (file >> key)
	{
		if (key == "theme")
		{
			int value = 0;
			if (file >> value && value >= 0 && value < ThemeCount)
				ActiveTheme = value;
		}
		else
		{
			// Unknown key: skip its value so a later key still lines up. Keeps an old
			// binary from choking on a setting a newer one wrote.
			std::string ignored;
			file >> ignored;
		}
	}
}

void scree::ui::SaveSettings()
{
	std::ofstream file(SettingsPath());
	file << "theme " << ActiveTheme << "\n";
}

// ------------------------------------------------------------------- assembly

void scree::Game::UI()
{
	TopBar(*this);
	Banner(*this);
	LeftRail(*this);
	BottomBar(*this);
	CanvasOverlays(*this);
	BenchmarkPanel(*this);
}
