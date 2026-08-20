#include "Game.h"
#include "UI.h"
#include "FileDialog.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

using namespace scree;
namespace c = scree::ui::col;
namespace m = scree::ui::metrics;

ImFont* scree::ui::Font = nullptr;

#if defined(_WIN32)
// Declared by hand rather than including <windows.h>, which collides with raylib. raylib's
// OpenURL is unusable here: it runs system("explorer ..."), which spawns a console this exe
// lacks and rejects the forward slashes GetApplicationDirectory returns.
extern "C" __declspec(dllimport) void* __stdcall ShellExecuteA(
	void* hwnd, const char* op, const char* file, const char* params,
	const char* dir, int showCmd);
#if defined(_MSC_VER)
#pragma comment(lib, "shell32.lib")
#endif
#endif

namespace
{
	static const char* const lifeSpanBaseStr[] = { "Self", "Reactor", "Initial" };

	ImU32 U32(const ImVec4& v) { return ImGui::GetColorU32(v); }

	// Design units to pixels: everything below is written at 1.0 and passed through here.
	inline float S(float v) { return v * m::Scale * m::UiScale; }

	// PushFont keeps the current face and changes only the size. Size tracks UiScale so text
	// rasterises at device resolution on a high-DPI canvas.
	struct FontScope
	{
		explicit FontScope(float size) { ImGui::PushFont(scree::ui::Font, size * m::UiScale); }
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

	// ---------------------------------------------- material editor building blocks

	ImVec4 SwatchColour(const MaterialData& data);

	// Where every control in the editor starts, measured from the left edge of the card it
	// sits in. Names sit to the left of it and widgets to the right, at any nesting depth.
	constexpr float FormLabelW = 168.0f;

	// How far the current nesting level is held off the body's right edge. Cards stack, each
	// adding its inset and padding and restoring the previous value. Widths measure off this
	// because ImGui resolves a negative item width against the window edge, not the card.
	float RightMargin = 0.0f;

	// Width from the cursor to the right edge of the card the cursor is in.
	float AvailW() { return ImGui::GetContentRegionAvail().x - RightMargin; }

	// Depth is carried by the fill: levels alternate recessed/raised, so nested cards read at a glance.
	enum class CardTone { Sunk, Raised };

	// A filled, bordered box behind a run of widgets. Its height is only known once they have
	// been laid out, so the fill goes on its own draw channel and is merged back in after.
	class Card
	{
	public:
		explicit Card(CardTone tone, float inset = 0.0f, float pad = S(11.0f))
			: m_pad(pad), m_inset(inset), m_outerMargin(RightMargin)
		{
			m_fill = (tone == CardTone::Sunk) ? ui::theme.PanelSunk : ui::theme.Button;
			m_edge = (tone == CardTone::Sunk) ? ui::theme.BorderSoft : ui::theme.ButtonBorder;

			if (m_inset > 0.0f) ImGui::Indent(m_inset);
			m_dl = ImGui::GetWindowDrawList();
			m_min = ImGui::GetCursorScreenPos();
			m_w = ImGui::GetContentRegionAvail().x - m_outerMargin - m_inset;

			m_split.Split(m_dl, 2);
			m_split.SetCurrentChannel(m_dl, 1);

			RightMargin = m_outerMargin + m_inset + m_pad;
			ImGui::Indent(m_pad);
			ImGui::PushItemWidth(-RightMargin);
			ImGui::Dummy(ImVec2(0.0f, S(2.0f)));
		}

		~Card()
		{
			ImGui::Dummy(ImVec2(0.0f, S(2.0f)));
			ImGui::PopItemWidth();
			ImGui::Unindent(m_pad);
			RightMargin = m_outerMargin;

			const ImVec2 max(m_min.x + m_w, ImGui::GetCursorScreenPos().y);
			m_split.SetCurrentChannel(m_dl, 0);
			m_dl->AddRectFilled(m_min, max, U32(m_fill), S(4.0f));
			m_dl->AddRect(m_min, max, U32(m_edge), S(4.0f));
			m_split.Merge(m_dl);

			if (m_inset > 0.0f) ImGui::Unindent(m_inset);
		}

		Card(const Card&) = delete;
		Card& operator=(const Card&) = delete;

	private:
		ImDrawList* m_dl = nullptr;
		ImDrawListSplitter m_split;
		ImVec2 m_min{};
		float m_w = 0.0f, m_pad, m_inset, m_outerMargin;
		ImVec4 m_fill{}, m_edge{};
	};

	// A tracked caps caption on a row of its own -- the same treatment the bars and the rail
	// give their section names.
	void Caption(const char* text, const ImVec4& colour)
	{
		FontScope f(m::FontLabel);
		const ImVec2 p = ImGui::GetCursorScreenPos();
		const float th = ImGui::GetTextLineHeight();
		DrawTracked(ImGui::GetWindowDrawList(), ImVec2(p.x, p.y + S(2.0f)), text, m::TrackLabel, colour);
		ImGui::Dummy(ImVec2(TrackedW(text, m::TrackLabel), th + S(4.0f)));
	}

	// Caption plus a remove button hard against the card's right edge. True when it was hit.
	bool CardHeader(const char* text, int index)
	{
		char title[64];
		if (index >= 0) std::snprintf(title, sizeof(title), "%s %d", text, index + 1);
		else            std::snprintf(title, sizeof(title), "%s", text);

		const float btn = S(25.0f);
		const ImVec2 p = ImGui::GetCursorScreenPos();
		const float w = AvailW();

		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + w - btn);
		const bool remove = ChromeButton("X", ImVec2(btn, btn));

		FontScope f(m::FontLabel);
		const float th = ImGui::GetTextLineHeight();
		DrawTracked(ImGui::GetWindowDrawList(), ImVec2(p.x, p.y + (btn - th) * 0.5f), title,
			m::TrackLabel, ui::theme.TextFaint);
		return remove;
	}

	// A folding section band, styled like a selected row in the rail. The open state lives in
	// ImGui's own per-window storage, so no member has to be added for each section.
	bool Section(const char* label, bool defaultOpen = false)
	{
		ImGui::Dummy(ImVec2(0.0f, S(3.0f)));

		ImGuiStorage* store = ImGui::GetStateStorage();
		const ImGuiID key = ImGui::GetID(label);
		bool open = store->GetBool(key, defaultOpen);

		const float h = S(34.0f);
		const ImVec2 p = ImGui::GetCursorScreenPos();
		const float w = AvailW();

		if (ImGui::InvisibleButton(label, ImVec2(w, h)))
		{
			open = !open;
			store->SetBool(key, open);
		}
		const bool hovered = ImGui::IsItemHovered();

		ImDrawList* dl = ImGui::GetWindowDrawList();
		if (open || hovered)
			dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h),
				U32(open ? ui::theme.AccentBg : ui::theme.RowHover), S(3.0f));
		if (open)
			dl->AddRectFilled(p, ImVec2(p.x + S(2.0f), p.y + h), U32(ui::theme.Accent));

		const float cx = p.x + S(15.0f);
		const float cy = p.y + h * 0.5f;
		const ImU32 caret = U32(open ? ui::theme.Accent : ui::theme.TextMute);
		if (open)
			dl->AddTriangleFilled(ImVec2(cx - S(5.0f), cy - S(3.0f)), ImVec2(cx + S(5.0f), cy - S(3.0f)),
				ImVec2(cx, cy + S(4.0f)), caret);
		else
			dl->AddTriangleFilled(ImVec2(cx - S(3.0f), cy - S(5.0f)), ImVec2(cx - S(3.0f), cy + S(5.0f)),
				ImVec2(cx + S(4.0f), cy), caret);

		FontScope f(m::FontLabel);
		const float th = ImGui::GetTextLineHeight();
		DrawTracked(dl, ImVec2(p.x + S(31.0f), p.y + (h - th) * 0.5f), label, m::TrackLabel,
			open ? ui::theme.AccentText : ui::theme.TextFaint);
		return open;
	}

	// Cards carry their own border, so the global item spacing is not enough between two of
	// them -- stacked that close they read as one box with a line drawn through it.
	void Gap(float h = S(4.0f)) { ImGui::Dummy(ImVec2(0.0f, h)); }

	// What a list says when it is empty, rather than leaving a blank card.
	void Hint(const char* text)
	{
		ImGui::PushStyleColor(ImGuiCol_Text, ui::theme.TextGhost);
		ImGui::TextUnformatted(text);
		ImGui::PopStyleColor();
	}

	// The left half of a control row. Leaves the cursor on the control column; the widget that
	// follows inherits the card's item width, so every field ends on the same right edge too.
	void FormLabel(const char* text)
	{
		const float x = ImGui::GetCursorPosX();
		ImGui::AlignTextToFramePadding();
		ImGui::PushStyleColor(ImGuiCol_Text, ui::theme.TextMute);
		ImGui::TextUnformatted(text);
		ImGui::PopStyleColor();
		ImGui::SameLine(0.0f, 0.0f);
		ImGui::SetCursorPosX(std::max(x + S(FormLabelW), ImGui::GetCursorPosX() + S(10.0f)));
	}

	bool ChromeCheckbox(const char* label, bool* v)
	{
		ImGui::PushStyleColor(ImGuiCol_Text, *v ? ui::theme.Text : ui::theme.TextDim);
		const bool changed = ImGui::Checkbox(label, v);
		ImGui::PopStyleColor();
		return changed;
	}

	// A checkbox carries its own label, so it takes the control column and leaves the label
	// column empty -- that keeps its box in line with the sliders and combos above it.
	bool FormCheck(const char* label, bool* v)
	{
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + S(FormLabelW));
		return ChromeCheckbox(label, v);
	}

	// Combos are the one widget that dresses two windows: the box in the panel and the popup
	// over it, which is why the colours are pushed around the whole call rather than one item.
	void PushComboStyle()
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
	}

	void PopComboStyle() { ImGui::PopStyleColor(9); }

	bool EnumCombo(const char* label, const char* id, int& value, const char* const* names, int count)
	{
		FormLabel(label);
		bool changed = false;

		PushComboStyle();
		if (ImGui::BeginCombo(id, names[value]))
		{
			for (int i = 0; i < count; i++)
			{
				const bool selected = (i == value);
				if (ImGui::Selectable(names[i], selected))
				{
					value = i;
					changed = true;
				}
				if (selected) ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
		PopComboStyle();
		return changed;
	}

	// Every row carries the material's swatch, the same one the rail lists it by, so a target
	// is picked by the colour it will actually paint rather than by name alone.
	bool MaterialCombo(const char* label, const char* id, MaterialID& value, Game& g)
	{
		FormLabel(label);

		// Captured before the combo opens: while the popup is up it is the current window, and
		// the swatch has to land on the box back in the panel.
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const ImVec2 box = ImGui::GetCursorScreenPos();
		const float boxH = ImGui::GetFrameHeight();
		const float sw = S(15.0f);

		// Leading spaces reserve the room the swatch is drawn into; a combo preview is a
		// string and nothing else.
		char preview[80];
		std::snprintf(preview, sizeof(preview), "     %s", g.materialRegistry.GetName(value).c_str());

		bool changed = false;
		PushComboStyle();
		if (ImGui::BeginCombo(id, preview))
		{
			ImDrawList* pdl = ImGui::GetWindowDrawList();
			for (int i = 0; i < g.materialRegistry.GetMaterialsCount(); i++)
			{
				const MaterialID rowID = static_cast<MaterialID>(i);
				const bool selected = (rowID == value);

				ImGui::PushID(i);
				if (ImGui::Selectable("##opt", selected, 0, ImVec2(0.0f, ImGui::GetTextLineHeight() + S(6.0f))))
				{
					value = rowID;
					changed = true;
				}
				if (selected) ImGui::SetItemDefaultFocus();

				const ImVec2 p = ImGui::GetItemRectMin();
				const float rowH = ImGui::GetItemRectSize().y;
				const float sy = std::floor(p.y + (rowH - sw) * 0.5f);
				pdl->AddRectFilled(ImVec2(p.x + S(7.0f), sy), ImVec2(p.x + S(7.0f) + sw, sy + sw),
					U32(SwatchColour(g.materialRegistry.Get(rowID))), S(2.0f));
				pdl->AddText(ImVec2(p.x + S(29.0f), p.y + (rowH - ImGui::GetTextLineHeight()) * 0.5f),
					U32(selected ? ui::theme.AccentText : ui::theme.TextDim),
					g.materialRegistry.GetName(rowID).c_str());
				ImGui::PopID();
			}
			ImGui::EndCombo();
		}
		PopComboStyle();

		dl->AddRectFilled(ImVec2(box.x + S(9.0f), box.y + (boxH - sw) * 0.5f),
			ImVec2(box.x + S(9.0f) + sw, box.y + (boxH + sw) * 0.5f),
			U32(SwatchColour(g.materialRegistry.Get(value))), S(2.0f));
		return changed;
	}

	bool TagCombo(const char* label, const char* id, MaterialID& value, Game& g)
	{
		std::vector<std::pair<std::string, MaterialID>> tags(
			g.materialRegistry.GetTags().begin(), g.materialRegistry.GetTags().end());
		std::sort(tags.begin(), tags.end(),
			[](const auto& a, const auto& b) { return a.second < b.second; });

		const char* preview = "";
		for (const auto& p : tags)
			if (p.second == value) preview = p.first.c_str();

		FormLabel(label);
		bool changed = false;

		PushComboStyle();
		if (ImGui::BeginCombo(id, preview))
		{
			for (const auto& p : tags)
			{
				const bool selected = (p.second == value);
				if (ImGui::Selectable(p.first.c_str(), selected))
				{
					value = p.second;
					changed = true;
				}
				if (selected) ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
		PopComboStyle();
		return changed;
	}

	// A swatch that opens the picker, with the value beside it: the three drag boxes ImGui
	// puts there by default do not fit the column and read as three unrelated numbers.
	bool ColorEditRGB(const char* label, scree::RGB& c)
	{
		FormLabel(label);

		char id[64];
		std::snprintf(id, sizeof(id), "##%s", label);

		float col[3] = { c.r / 255.0f, c.g / 255.0f, c.b / 255.0f };
		const bool changed = ImGui::ColorEdit3(id, col,
			ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
		if (changed)
		{
			c.r = static_cast<std::uint8_t>(std::clamp(col[0], 0.0f, 1.0f) * 255.0f + 0.5f);
			c.g = static_cast<std::uint8_t>(std::clamp(col[1], 0.0f, 1.0f) * 255.0f + 0.5f);
			c.b = static_cast<std::uint8_t>(std::clamp(col[2], 0.0f, 1.0f) * 255.0f + 0.5f);
		}

		char hex[16];
		std::snprintf(hex, sizeof(hex), "%02X %02X %02X", c.r, c.g, c.b);
		ImGui::SameLine(0.0f, S(10.0f));
		ImGui::AlignTextToFramePadding();
		ImGui::PushStyleColor(ImGuiCol_Text, ui::theme.TextDim);
		ImGui::TextUnformatted(hex);
		ImGui::PopStyleColor();
		return changed;
	}

	bool Uint8Edit(const char* label, std::uint8_t& v, int lo, int hi)
	{
		FormLabel(label);

		char id[64];
		std::snprintf(id, sizeof(id), "##%s", label);

		int n = v;
		if (ImGui::SliderInt(id, &n, lo, hi, "%d", ImGuiSliderFlags_NoInput))
		{
			v = static_cast<std::uint8_t>(std::clamp(n, 0, 255));
			return true;
		}
		return false;
	}

	// One weighted outcome. `initialLifespanOnly` is the on_death case, where the loader
	// accepts nothing but Initial and there is no choice to offer.
	bool TransitionEdit(Transition& transition, int index, Game& g, bool initialLifespanOnly)
	{
		Card card(CardTone::Raised, S(6.0f));
		const bool remove = CardHeader("OUTCOME", index);

		// Weight applies either way: an entry that changes nothing still competes with the
		// others for the roll.
		Uint8Edit("Weight", transition.weight, 1, 255);
		FormCheck("Leave the block unchanged", &transition.noTransition);

		if (!transition.noTransition)
		{
			MaterialCombo("Turns into", "##nextid", transition.nextID, g);

			if (!initialLifespanOnly)
			{
				int base = static_cast<int>(transition.lifespanBase);
				if (EnumCombo("Lifespan base", "##lifespanbase", base, lifeSpanBaseStr, 3))
					transition.lifespanBase = static_cast<Transition::LifeSpanBase>(base);
			}
		}

		return remove;
	}

	// A weighted outcome list in a card of its own, captioned with the field it writes.
	void TransitionList(const char* caption, std::vector<Transition>& list, Game& g,
		bool initialLifespanOnly)
	{
		Card card(CardTone::Sunk, S(6.0f));
		Caption(caption, ui::theme.TextFaint);

		if (list.empty())
			Hint("nothing happens");

		for (size_t i = 0; i < list.size();)
		{
			ImGui::PushID(static_cast<int>(i));
			const bool remove = TransitionEdit(list[i], static_cast<int>(i), g, initialLifespanOnly);
			ImGui::PopID();
			Gap();

			if (remove) list.erase(list.begin() + i);
			else        ++i;
		}

		FontScope f(m::FontLabel);
		if (ChromeButton("+ OUTCOME", ImVec2(S(150.0f), S(29.0f))))
			list.push_back(Transition());
	}

	bool ReactionEdit(EditReaction& er, int index, Game& g)
	{
		static const char* const targetTypeStr[] = { "Material", "Tag" };
		static const char* const sampleStr[] = { "All neighbours", "First to react" };

		Card card(CardTone::Raised, S(6.0f));
		const bool remove = CardHeader("REACTION", index);

		Reaction& r = er.reaction;

		int type = static_cast<int>(r.targetType);
		if (EnumCombo("Target type", "##targettype", type, targetTypeStr, 2))
		{
			r.targetType = static_cast<Reaction::TargetType>(type);
			r.targetID = 0;   // the id means a different thing in each mode
		}

		if (r.targetType == Reaction::TargetType::Material)
		{
			MaterialCombo("Target", "##target", r.targetID, g);
			Uint8Edit("Chance", r.chance, 0, 100);
		}
		else
		{
			TagCombo("Target tag", "##targettag", r.targetID, g);
		}

		int sample = static_cast<int>(r.sample);
		if (EnumCombo("Scan sample", "##sample", sample, sampleStr, 2))
			r.sample = static_cast<Reaction::Sample>(sample);

		FormCheck("Halt update", &r.haltUpdate);
		Gap();

		ImGui::PushID("target");
		TransitionList("TARGET BECOMES", er.targetTransitions, g, false);
		ImGui::PopID();
		Gap();

		ImGui::PushID("self");
		TransitionList("SELF BECOMES", er.selfTransitions, g, false);
		ImGui::PopID();

		return remove;
	}


	const char* MaterialClass(const MaterialData& data, MaterialID id)
	{
		if (id == MaterialRegistry::AIR_ID)      return "void";
		if (data.movement.yDirection < 0)       return "gas";
		if (data.movement.isLiquid)             return "liquid";
		if (data.movement.canCascade)           return "powder";
		if (data.movement.canFall)              return "grain";
		return "solid";
	}

	ImVec4 SwatchColour(const MaterialData& data)
	{
		const RGB mid = scree::ui::SwatchRGB(data);
		return scree::ui::hex(mid.r, mid.g, mid.b);
	}

	std::string FileName(const std::string& path)
	{
		const std::size_t cut = path.find_last_of("/\\");
		return cut == std::string::npos ? path : path.substr(cut + 1);
	}

	// Hands the file to whatever the desktop has registered for it.
	void OpenInEditor(std::string path)
	{
#if defined(_WIN32)
		for (char& ch : path) if (ch == '/') ch = '\\';
		// A return of 32 or less is a failure, not a handle -- usually SE_ERR_NOASSOC when nothing
		// is registered for .json. "openas" then shows the Open With picker, so the click always leads somewhere.
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
		const float W = static_cast<float>(g.windowSize.x);
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
			std::snprintf(fps, sizeof(fps), "%.0f fps", g.fpsDisplay);
			const float fpsW = S(82.0f);
			r -= fpsW;
			DrawLabel(dl, ImVec2(org.x + r + fpsW - TextW(fps), org.y + CenterY(H, th)), fps,
				g.fpsDisplay >= 55.0f ? c::Good : c::Warn);
			r -= S(14.0f);

#if !defined(PLATFORM_WEB)
			// The materials file chip is desktop-only: on web the file is baked into the
			// build and its reload/import/save/open-in-editor actions do not apply.
			const char* name = "materials.json";
			const std::string customName = FileName(g.customFilePath);
			const bool hasCustom = !customName.empty();

			const float reloadW = S(94.0f);
			const float importW = S(88.0f);
			const float saveW = S(74.0f);
			const float clearW = S(28.0f);
			const float nameW = TextW(name);
			const float customW = hasCustom ? TextW(customName.c_str()) : 0.0f;
			const float chipH = S(39.0f);

			float chipW = S(14.0f) + nameW + S(10.0f) + reloadW + S(6.0f) + importW
				+ S(6.0f) + saveW + S(8.0f);
			if (hasCustom) chipW += S(10.0f) + customW + S(6.0f) + clearW;
			r -= chipW;

			const ImVec2 chipMin(org.x + r, org.y + CenterY(H, chipH));
			const ImVec2 chipMax(chipMin.x + chipW, chipMin.y + chipH);
			dl->AddRectFilled(chipMin, chipMax, U32(ui::theme.Button), S(3.0f));
			dl->AddRect(chipMin, chipMax, U32(ui::theme.ButtonBorder), S(3.0f));

			const float ty = chipMin.y + (chipH - th) * 0.5f;
			const float buttonY = CenterY(H, S(31.0f));
			float cx = r + S(14.0f);

			// The file name opens the file in whatever the desktop has registered for it.
			ImGui::SetCursorPos(ImVec2(cx, ty - org.y));
			if (ImGui::InvisibleButton("##openjson", ImVec2(nameW, th)))
				OpenInEditor(g.MaterialsPath());

			bool hot = ImGui::IsItemHovered();
			if (hot) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
			DrawLabel(dl, ImVec2(org.x + cx, ty), name, hot ? ui::theme.AccentLight : ui::theme.TextDim);
			if (hot)
				dl->AddLine(ImVec2(org.x + cx, ty + th - S(2.0f)),
					ImVec2(org.x + cx + nameW, ty + th - S(2.0f)), U32(ui::theme.AccentLight));
			cx += nameW + S(10.0f);

			ImGui::SetCursorPos(ImVec2(cx, buttonY));
			ImGui::PushStyleColor(ImGuiCol_Text, ui::theme.AccentLight);
			if (ChromeButton("RELOAD", ImVec2(reloadW, S(31.0f))))
				g.LoadMaterials();
			ImGui::PopStyleColor();
			cx += reloadW + S(6.0f);

			ImGui::SetCursorPos(ImVec2(cx, buttonY));
			if (ChromeButton("IMPORT", ImVec2(importW, S(31.0f)), hasCustom))
				g.ImportMaterials();
			cx += importW + S(6.0f);

			ImGui::SetCursorPos(ImVec2(cx, buttonY));
			ImGui::BeginDisabled(g.materialRegistry.GetMaterialsCount() <= g.materialRegistry.GetCoreMaterialsCount());
			if (ChromeButton("SAVE", ImVec2(saveW, S(31.0f))))
				g.ExportMaterials();
			ImGui::EndDisabled();
			cx += saveW;

			if (hasCustom) {
				cx += S(10.0f);
				ImGui::SetCursorPos(ImVec2(cx, ty - org.y));
				if (ImGui::InvisibleButton("##opencustom", ImVec2(customW, th)))
					OpenInEditor(g.customFilePath);

				hot = ImGui::IsItemHovered();
				if (hot) {
					ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
					ImGui::SetTooltip("%s", g.customFilePath.c_str());
				}
				DrawLabel(dl, ImVec2(org.x + cx, ty), customName.c_str(),
					hot ? ui::theme.AccentLight : ui::theme.TextDim);
				if (hot)
					dl->AddLine(ImVec2(org.x + cx, ty + th - S(2.0f)),
						ImVec2(org.x + cx + customW, ty + th - S(2.0f)), U32(ui::theme.AccentLight));
				cx += customW + S(6.0f);

				ImGui::SetCursorPos(ImVec2(cx, buttonY));
				if (ChromeButton("X", ImVec2(clearW, S(31.0f))))
					g.ClearCustomMaterials();
			}
#endif
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
				g.stepOnce = true;
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
			if (ChromeButton("-##stepsdown", ImVec2(stepBtn, stepBtn)) && g.updatesPerFrame > 1)
				g.updatesPerFrame--;

			char steps[16];
			std::snprintf(steps, sizeof(steps), "%d", g.updatesPerFrame);
			const float th = ImGui::GetTextLineHeight();
			DrawLabel(dl, ImVec2(org.x + cx + stepBtn + (stepVal - TextW(steps)) * 0.5f,
				org.y + CenterY(H, th)), steps, ui::theme.Text);

			ImGui::SetCursorPos(ImVec2(cx + stepBtn + stepVal, CenterY(H, stepBtn)));
			if (ChromeButton("+##stepsup", ImVec2(stepBtn, stepBtn)) && g.updatesPerFrame < 10)
				g.updatesPerFrame++;
		}

		EndChrome();
	}

	// ----------------------------------------------------------------- banner

	void Banner(Game& g)
	{
		if (g.layoutBannerH <= 0.0f) return;

		const float W = static_cast<float>(g.windowSize.x);
		const float H = g.layoutBannerH;
		const ImVec4& edge = g.materialLoadFailed ? c::Bad : c::Warn;

		BeginChrome("##banner", ImVec2(0, m::TopBarH), ImVec2(W, H), ui::theme.PanelSunk, true);
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const ImVec2 org = ImGui::GetWindowPos();
		dl->AddRectFilled(ImVec2(org.x, org.y), ImVec2(org.x + S(3.0f), org.y + H), U32(edge));
		dl->AddLine(ImVec2(org.x, org.y + H - 0.5f), ImVec2(org.x + W, org.y + H - 0.5f), U32(ui::theme.Border));

		{
			FontScope f(m::FontLabel);
			const char* tier = g.materialLoadFailed ? "LOAD FAILED" : "LOADER";
			DrawTracked(dl, ImVec2(org.x + S(16.0f), org.y + S(10.0f)), tier, m::TrackLabel, edge);
		}

		{
			FontScope f(m::FontSmall);
			ImGui::SetCursorPos(ImVec2(m::BannerTextInset, S(8.0f)));
			ImGui::PushStyleColor(ImGuiCol_Text, ui::theme.TextDim);
			ImGui::PushTextWrapPos(W - m::BannerTextRight);
			ImGui::TextUnformatted(g.materialLoadErrorMessage.c_str());
			ImGui::PopTextWrapPos();
			ImGui::PopStyleColor();
		}

		{
			FontScope f(m::FontLabel);
			ImGui::SetCursorPos(ImVec2(W - S(103.0f), S(11.0f)));
			if (ChromeButton("DISMISS", ImVec2(S(87.0f), S(31.0f))))
				g.materialLoadErrorMessage.clear();
		}

		EndChrome();
	}

	// -------------------------------------------------------------- left rail

	void LeftRail(Game& g)
	{
		const float top = m::TopBarH + g.layoutBannerH;
		const float H = static_cast<float>(g.windowSize.y) - top;
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

			for (int i = 0; i < g.materialRegistry.GetMaterialsCount(); i++)
			{
				const MaterialID id = static_cast<MaterialID>(i);
				const MaterialData& data = g.materialRegistry.Get(id);
				const bool selected = (id == g.selectedMaterial);

				ImGui::PushID(i);
				const ImVec2 p = ImGui::GetCursorScreenPos();
				const float rowW = ImGui::GetContentRegionAvail().x;
				if (ImGui::InvisibleButton("row", ImVec2(rowW, rowH)))
					g.selectedMaterial = id;
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

				// Snapped to whole pixels: a half-pixel centred Y let the fill's AA corners spill
				// past the stroke. The border is a larger filled rect behind the fill, not a
				// stroke, so it can't bleed.
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
						g.materialRegistry.GetName(id).c_str(), selected ? ui::theme.AccentText : ui::theme.TextDim);
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

			ImGui::SetCursorPosX(S(6.0f));
			const float rowW = ImGui::GetContentRegionAvail().x;
			if (ChromeButton("+ NEW MATERIAL", ImVec2(rowW, m::RowH))) {
				g.BeginNewMaterial();
				g.newMaterialPanelOpen = true;
			}

			ImGui::SetCursorPosX(S(6.0f));
			ImGui::BeginDisabled(g.selectedMaterial == MaterialRegistry::AIR_ID);
			if (ChromeButton("EDIT SELECTED", ImVec2(rowW, m::RowH))) {
				g.BeginEditMaterial(g.selectedMaterial);
				g.newMaterialPanelOpen = true;
			}

			ImGui::SetCursorPosX(S(6.0f));
			if (ChromeButton("DELETE SELECTED", ImVec2(rowW, m::RowH))) {
				g.pendingDeleteId = g.selectedMaterial;
				g.confirmDeleteOpen = true;
			}
			ImGui::EndDisabled();

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
				g.materialRegistry.GetMaterialsCount() - 1, g.materialRegistry.GetTagsCount());
			char particles[32];
			std::snprintf(particles, sizeof(particles), "%d", g.particleCount);
			char chunks[32];
			std::snprintf(chunks, sizeof(chunks), "%d/%d", g.awakeChunkCount,
				g.grid.GetWidthChunks() * g.grid.GetHeightChunks());

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
		PushComboStyle();

		ImGui::SetCursorPos(ImVec2(x, y));
		ImGui::SetNextItemWidth(w);

		// Preview is blank -- swatch and name are drawn manually below; a spacer string would
		// clear the swatch by the font's space width. Window padding is zero so the bars sit
		// flush, but the popup needs its own back. Box rect captured before BeginCombo, which
		// otherwise reports the popup window instead of the button.
		const ImVec2 boxMin = ImGui::GetCursorScreenPos();
		const float boxCenterY = boxMin.y + ImGui::GetFrameHeight() * 0.5f;

		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, S(4.0f)));
		const bool open = ImGui::BeginCombo("##theme", "", ImGuiComboFlags_HeightLargest);
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

				// Swatch and name centred in the row's real rect, matching the closed box,
				// so they hold at any UI scale instead of drifting from fixed offsets.
				const ImVec2 rmin = ImGui::GetItemRectMin();
				const ImVec2 rmax = ImGui::GetItemRectMax();
				ImDrawList* pdl = ImGui::GetWindowDrawList();
				const RGB& seed = ui::ThemeOptions[i].seed;
				const float sw = S(15.0f);
				const float sTop = (rmin.y + rmax.y) * 0.5f - sw * 0.5f;
				pdl->AddRectFilled(ImVec2(rmin.x + S(8.0f), sTop), ImVec2(rmin.x + S(8.0f) + sw, sTop + sw),
					U32(scree::ui::hex(seed.r, seed.g, seed.b)), 2.0f);
				const float rowFh = ImGui::GetFontSize();
				pdl->AddText(ImVec2(rmin.x + S(29.0f), (rmin.y + rmax.y) * 0.5f - rowFh * 0.5f),
					U32(selected ? ui::theme.AccentText : ui::theme.TextDim), ui::ThemeOptions[i].name);
				ImGui::PopID();
			}
			ImGui::EndCombo();
		}

		// Back in the owning window now, so this lands on the box rather than the popup.
		// Centred in the combo's real rect so it holds at any UI scale.
		const RGB& active = ui::ThemeOptions[ui::ActiveTheme].seed;
		const float swatch = S(15.0f);
		const float swatchTop = boxCenterY - swatch * 0.5f;
		ImGui::GetWindowDrawList()->AddRectFilled(
			ImVec2(boxMin.x + S(8.0f), swatchTop),
			ImVec2(boxMin.x + S(8.0f) + swatch, swatchTop + swatch),
			U32(scree::ui::hex(active.r, active.g, active.b)), 2.0f);

		// The name, past the swatch and centred in the box like the popup rows.
		const float fh = ImGui::GetFontSize();
		ImGui::GetWindowDrawList()->AddText(
			ImVec2(boxMin.x + S(29.0f), boxCenterY - fh * 0.5f),
			U32(ui::theme.TextDim), ui::ThemeOptions[ui::ActiveTheme].name);

		PopComboStyle();
	}

	// ------------------------------------------------------------- bottom bar

	void BottomBar(Game& g)
	{
		const float W = static_cast<float>(g.windowSize.x) - m::LeftRailW;
		const float H = m::BottomBarH;
		const float y = static_cast<float>(g.windowSize.y) - H;

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
			ImGui::SliderFloat("##brush", &g.cursorRadius, 1.0f, 100.0f, "");
			ImGui::PopStyleColor(5);
			ImGui::PopStyleVar();
			x += S(190.0f) + S(12.0f);

			char radius[16];
			std::snprintf(radius, sizeof(radius), "%d", static_cast<int>(g.cursorRadius));
			const float th = ImGui::GetTextLineHeight();
			DrawLabel(dl, ImVec2(org.x + x, org.y + CenterY(H, th)), radius, ui::theme.TextDim);
		}

		{
			FontScope f(m::FontLabel);
			float r = W - S(14.0f);
			const float btnH = S(35.0f);

			r -= S(138.0f);
			ImGui::SetCursorPos(ImVec2(r, CenterY(H, btnH)));
			if (ChromeButton("BENCHMARK", ImVec2(S(138.0f), btnH), g.showBenchMarks))
				g.showBenchMarks = !g.showBenchMarks;

			r -= S(8.0f) + S(172.0f);
			ImGui::SetCursorPos(ImVec2(r, CenterY(H, btnH)));
			if (ChromeButton("CHUNK OVERLAY", ImVec2(S(172.0f), btnH), g.showActiveChunks))
				g.showActiveChunks = !g.showActiveChunks;

			r -= S(8.0f) + S(92.0f);
			ImGui::SetCursorPos(ImVec2(r, CenterY(H, btnH)));
			if (ChromeButton("BLOOM", ImVec2(S(92.0f), btnH), g.bloomEnabled))
				g.bloomEnabled = !g.bloomEnabled;

#if !defined(PLATFORM_WEB)
			const float canvasW = S(126.0f);
			r -= S(20.0f) + canvasW;
			ImGui::SetCursorPos(ImVec2(r, CenterY(H, btnH)));
			if (ChromeButton("LOAD CANVAS", ImVec2(canvasW, btnH))) {
				const std::string picked = OpenFileDialog("Canvas files\0*.json\0All files\0*.*\0",
					"Load canvas", g.CanvasesPath());
				if (!picked.empty()) g.ImportCanvas(picked);
			}

			r -= S(8.0f) + canvasW;
			ImGui::SetCursorPos(ImVec2(r, CenterY(H, btnH)));
			if (ChromeButton("SAVE CANVAS", ImVec2(canvasW, btnH))) {
				const std::string picked = SaveFileDialog("Canvas files\0*.json\0All files\0*.*\0",
					"Save canvas", g.CanvasesPath(), "canvas.json");
				if (!picked.empty()) g.ExportCanvas(picked);
			}
#endif
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
		// Background, not foreground: still lands on top of the grid but stays under the panels
		// and popups. On the foreground list the hint chip painted over the open theme dropdown.
		ImDrawList* dl = ImGui::GetBackgroundDrawList();
		// Anchored to the region between the panels, not the grid, so these sit in the
		// letterboxing rather than over the simulation's corners. The grid fits the shorter
		// side, so the other axis leaves a wide band free.
		const Rectangle& f = g.canvasRegion;

		FontScope fs(m::FontSmall);

		float x = f.x + S(10.0f);
		const float y = f.y + S(10.0f);

		Chip(dl, x, y, g.materialRegistry.GetName(g.selectedMaterial).c_str(), ui::theme.Text);

		char brush[24];
		std::snprintf(brush, sizeof(brush), "r%d", static_cast<int>(g.cursorRadius));
		Chip(dl, x, y, brush, ui::theme.TextMute);

		Vector2 mouse = GetMousePosition();
		const int gx = static_cast<int>((mouse.x - g.gridOffset.x) / g.tileSize);
		const int gy = static_cast<int>((mouse.y - g.gridOffset.y) / g.tileSize);
		char cursor[32];
		if (g.grid.IsInBounds(gx, gy))
			std::snprintf(cursor, sizeof(cursor), "%d,%d", gx, gy);
		else
			std::snprintf(cursor, sizeof(cursor), "-,-");
		Chip(dl, x, y, cursor, ui::theme.TextMute);

		// Material under the cursor, only while it is over the grid. Labelled because it is
		// otherwise a second bare name sitting next to the selected one.
		if (g.grid.IsInBounds(gx, gy))
		{
			const Block& tile = g.grid.GetAt(static_cast<std::uint16_t>(gx), static_cast<std::uint16_t>(gy));
			if (tile.id != MaterialRegistry::AIR_ID)
			{
				char under[64];
				std::snprintf(under, sizeof(under), "under %s",
					g.materialRegistry.GetName(tile.id).c_str());
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
		if (!g.showBenchMarks) return;

		const float W = S(306.0f);
		const float H = S(207.0f);
		const float x = m::LeftRailW + S(18.0f);
		const float y = static_cast<float>(g.windowSize.y) - m::BottomBarH - H - S(18.0f);

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
			{ "input",  g.inputTime * S(1000.0f) },
			{ "update", g.updateTime * S(1000.0f) },
			{ "render", g.renderTime * S(1000.0f) },
			{ "ui",     g.uiTime * S(1000.0f) },
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

	// -------------------------------------------------------------- new material panel

	// The body of the editor: everything between the fixed title strip and the fixed footer,
	// laid out inside its own scrolling child so those two never move.
	void MaterialForm(Game& g)
	{
		MaterialData& material = *g.editedMaterial;

		{
			Card card(CardTone::Raised);
			FormLabel("Name");
			ImGui::InputText("##name", &g.newMaterialName);
		}

		if (Section("APPEARANCE", true))
		{
			Card card(CardTone::Sunk, S(8.0f));

			// The spread the particles are actually picked from, which neither of the two
			// swatches shows on its own.
			FormLabel("Range");
			{
				const ImVec2 p = ImGui::GetCursorScreenPos();
				const float w = ImGui::CalcItemWidth();
				const float h = ImGui::GetFrameHeight();
				ImGui::Dummy(ImVec2(w, h));

				ImDrawList* dl = ImGui::GetWindowDrawList();
				const ImU32 lo = U32(ui::hex(material.minColor.r, material.minColor.g, material.minColor.b));
				const ImU32 hi = U32(ui::hex(material.maxColor.r, material.maxColor.g, material.maxColor.b));
				dl->AddRectFilledMultiColor(p, ImVec2(p.x + w, p.y + h), lo, hi, hi, lo);
				dl->AddRect(p, ImVec2(p.x + w, p.y + h), U32(ui::theme.ButtonBorder));
			}

			ColorEditRGB("Min colour", material.minColor);
			ColorEditRGB("Max colour", material.maxColor);
			FormCheck("Interpolate over lifespan", &material.interpolateColor);
			if (!material.interpolateColor)
				Uint8Edit("Colour steps", material.numberOfSteps, 1, 10);

			Uint8Edit("Emission", material.emission, 0, 255);
		}

		if (Section("TAGS"))
		{
			Card card(CardTone::Sunk, S(8.0f));

			std::vector<std::pair<std::string, MaterialID>> tags(
				g.materialRegistry.GetTags().begin(), g.materialRegistry.GetTags().end());
			std::sort(tags.begin(), tags.end(),
				[](const auto& a, const auto& b) { return a.second < b.second; });

			if (tags.empty())
				Hint("no tags are defined in the material file");

			for (const auto& pair : tags)
			{
				ImGui::PushID(pair.second);
				const std::uint8_t tagBit = 1u << pair.second;

				FormLabel(pair.first.c_str());

				// The anchor box is parked at the right end of the row, so the intensity
				// slider stops short of the card edge rather than filling it.
				const float anchorW = S(112.0f);
				ImGui::SetNextItemWidth(-(RightMargin + anchorW));
				int intensity = material.tagIntensity[pair.second];
				if (ImGui::SliderInt("##intensity", &intensity, 0, 100, "%d", ImGuiSliderFlags_NoInput))
				{
					material.tagIntensity[pair.second] = static_cast<std::uint8_t>(intensity);
					if (intensity) material.tagBitmask |= tagBit;
					else           material.tagBitmask &= ~tagBit;
				}

				ImGui::SameLine(0.0f, S(12.0f));
				bool anchor = (material.anchorTagBitmask & tagBit) != 0;
				if (ChromeCheckbox("Anchor", &anchor))
				{
					if (anchor) material.anchorTagBitmask |= tagBit;
					else        material.anchorTagBitmask &= ~tagBit;
				}
				ImGui::PopID();
			}
		}

		if (Section("LIFESPAN"))
		{
			Card card(CardTone::Sunk, S(8.0f));

			Uint8Edit("Initial", material.lifespanData.initial, 0, 255);
			Uint8Edit("Tick", material.lifespanData.tick, 0, 255);

			if (material.lifespanData.tick)
			{
				Uint8Edit("Tick chance", material.lifespanData.chance, 0, 100);
				Gap();
				TransitionList("ON DEATH", g.newMaterialTransitions, g, true);
			}
			else
			{
				Hint("tick is 0, so this material never dies");
			}
		}

		if (Section("REACTIONS"))
		{
			Card card(CardTone::Sunk, S(8.0f));

			if (g.newMaterialReactions.empty())
				Hint("this material reacts with nothing");

			for (size_t i = 0; i < g.newMaterialReactions.size();)
			{
				ImGui::PushID(static_cast<int>(i));
				const bool remove = ReactionEdit(g.newMaterialReactions[i], static_cast<int>(i), g);
				ImGui::PopID();
				Gap();

				if (remove) g.newMaterialReactions.erase(g.newMaterialReactions.begin() + i);
				else        ++i;
			}

			FontScope f(m::FontLabel);
			if (ChromeButton("+ REACTION", ImVec2(S(150.0f), S(29.0f))))
				g.newMaterialReactions.push_back(EditReaction());
		}

		if (Section("MOVEMENT"))
		{
			Card card(CardTone::Sunk, S(8.0f));

			bool up = (material.movement.yDirection < 0);
			if (FormCheck("Rises instead of falling", &up))
				material.movement.yDirection = up ? -1 : 1;

			Uint8Edit("Density", material.movement.density, 0, 255);
			Uint8Edit("Scatter chance", material.movement.scatterChance, 0, 255);

			FormCheck("Can fall", &material.movement.canFall);
			FormCheck("Can cascade", &material.movement.canCascade);
			FormCheck("Is fluid", &material.movement.isFluid);
			FormCheck("Is liquid", &material.movement.isLiquid);
		}

		ImGui::Dummy(ImVec2(0.0f, S(6.0f)));
	}

	void NewMaterialPanel(Game& g)
	{
		const ImGuiViewport* vp = ImGui::GetMainViewport();
		const ImVec2 c = vp->GetCenter();
		const ImVec2 size(std::max(vp->Size.x * 0.52f, S(640.0f)), vp->Size.y * 0.84f);
		const ImVec2 pos(c.x - size.x * 0.5f, c.y - size.y * 0.5f);

		const float headH = S(52.0f);
		const float footH = S(58.0f);

		BeginChrome("##newmat", pos, size, ui::theme.Panel);
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const ImVec2 org = ImGui::GetWindowPos();
		const ImVec2 end(org.x + size.x, org.y + size.y);

		// The editor is the only thing to look at while it is up, so the rest of the window is
		// dimmed behind it. The window's own background has already gone down by this point,
		// which is why it is painted back over the scrim.
		dl->PushClipRectFullScreen();
		dl->AddRectFilled(vp->Pos, ImVec2(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y),
			U32(ImVec4(0.0f, 0.0f, 0.0f, 0.55f)));
		dl->PopClipRect();
		dl->AddRectFilled(org, end, U32(ui::theme.Panel), S(3.0f));
		dl->AddRect(org, end, U32(ui::theme.Border), S(3.0f));

		// --- title strip
		{
			dl->AddLine(ImVec2(org.x, org.y + headH - 0.5f), ImVec2(end.x, org.y + headH - 0.5f),
				U32(ui::theme.Border));

			const float ss = S(20.0f);
			const float sx = std::floor(org.x + S(16.0f));
			const float sy = std::floor(org.y + (headH - ss) * 0.5f);
			dl->AddRectFilled(ImVec2(sx, sy), ImVec2(sx + ss, sy + ss), U32(ui::theme.ButtonBorder), S(3.0f));
			dl->AddRectFilled(ImVec2(sx + 1.0f, sy + 1.0f), ImVec2(sx + ss - 1.0f, sy + ss - 1.0f),
				U32(SwatchColour(*g.editedMaterial)), S(2.0f));

			FontScope f(m::FontSmall);
			const float th = ImGui::GetTextLineHeight();
			const char* title = g.Editing() ? "EDIT MATERIAL" : "NEW MATERIAL";
			DrawTracked(dl, ImVec2(sx + ss + S(12.0f), org.y + CenterY(headH, th)), title,
				m::TrackLabel, ui::theme.Text);

			ImGui::SetCursorPos(ImVec2(size.x - S(16.0f) - S(28.0f), CenterY(headH, S(28.0f))));
			if (ChromeButton("X", ImVec2(S(28.0f), S(28.0f))))
			{
				g.newMaterialPanelOpen = false;
				g.BeginNewMaterial();
			}
		}

		// --- form
		ImGui::SetCursorPos(ImVec2(S(16.0f), headH + S(10.0f)));
		ImGui::BeginChild("##form", ImVec2(size.x - S(32.0f), size.y - headH - footH - S(10.0f)), 0,
			ImGuiWindowFlags_NoSavedSettings);
		{
			FontScope f(m::FontSmall);
			RightMargin = 0.0f;
			MaterialForm(g);
		}
		ImGui::EndChild();

		// --- footer
		{
			const float fy = size.y - footH;
			dl->AddLine(ImVec2(org.x, org.y + fy + 0.5f), ImVec2(end.x, org.y + fy + 0.5f),
				U32(ui::theme.Border));

			const bool nameTaken = !g.Editing() && g.materialRegistry.HasMaterial(g.newMaterialName);
			const bool canCommit = !g.newMaterialName.empty() && !nameTaken;
			const float btnH = S(34.0f);
			const float commitW = S(176.0f);

			FontScope f(m::FontLabel);

			ImGui::SetCursorPos(ImVec2(S(16.0f), fy + CenterY(footH, btnH)));
			ImGui::BeginDisabled(!canCommit);
			if (ChromeButton(g.Editing() ? "APPLY CHANGES" : "ADD MATERIAL", ImVec2(commitW, btnH)))
				g.CommitMaterial();
			ImGui::EndDisabled();

			if (!canCommit)
			{
				FontScope s(m::FontSmall);
				const float th = ImGui::GetTextLineHeight();
				DrawLabel(dl, ImVec2(org.x + S(16.0f) + commitW + S(14.0f), org.y + fy + CenterY(footH, th)),
					g.newMaterialName.empty() ? "a name is required" : "that name is taken", c::Warn);
			}

			if (g.Editing())
			{
				const float delW = S(104.0f);
				ImGui::SetCursorPos(ImVec2(size.x - S(16.0f) - delW, fy + CenterY(footH, btnH)));
				if (ChromeButton("DELETE", ImVec2(delW, btnH)))
				{
					g.pendingDeleteId = g.editedMaterialID;
					g.confirmDeleteOpen = true;
				}
			}
		}

		EndChrome();
	}

	// ------------------------------------------------------------ delete confirmation
	void ConfirmDeletePanel(Game& g)
	{
		const ImVec2 c = ImGui::GetMainViewport()->GetCenter();
		const ImVec2 size(S(420.0f), S(150.0f));
		const ImVec2 pos(c.x - size.x * 0.5f, c.y - size.y * 0.5f);

		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(16.0f), S(16.0f)));
		BeginChrome("##confirmdelete", pos, size, ui::theme.PanelSunk);

		ImGui::PushStyleColor(ImGuiCol_Text, ui::theme.Text);
		ImGui::TextUnformatted("Delete material");
		ImGui::PopStyleColor();

		ImGui::PushStyleColor(ImGuiCol_Text, ui::theme.TextDim);
		ImGui::PushTextWrapPos(size.x - S(16.0f));
		ImGui::Text("Every %s block on the grid is cleared and this cannot be undone.",
			g.materialRegistry.GetName(g.pendingDeleteId).c_str());
		ImGui::PopTextWrapPos();
		ImGui::PopStyleColor();

		ImGui::SetCursorPosY(size.y - S(47.0f));
		if (ChromeButton("DELETE", ImVec2(S(110.0f), S(31.0f)))) {
			g.DeleteMaterial(g.pendingDeleteId);
			g.confirmDeleteOpen = false;
			g.newMaterialPanelOpen = false;
		}
		ImGui::SameLine();
		if (ChromeButton("CANCEL", ImVec2(S(110.0f), S(31.0f))))
			g.confirmDeleteOpen = false;

		EndChrome();
		ImGui::PopStyleVar();
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
	return RGB::Lerp(data.minColor, data.maxColor, 0.5f);
}

namespace
{
	// Re-hues a neutral: the base gives lightness, the material hue and saturation. Mixing
	// towards the material colour instead would wash the dark panels out.
	//
	// `lift` raises the value of near-black surfaces (~0.06), which otherwise have no room to
	// carry a hue, so the tint shows without them ceasing to read as dark chrome.
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

	// The sizes above are written at 1.0; scale them to match the S()-geometry and fonts,
	// so control padding tracks the device-pixel canvas instead of drifting from it.
	s.ScaleAllSizes(metrics::UiScale);

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
#if defined(PLATFORM_WEB)
	// No host fonts exist in the browser filesystem, so ship one; without it ImGui's
	// bitmap font stands in and looks soft at any size.
	{
		const std::string inter = std::string(GetApplicationDirectory()) + "assets/Inter-Regular.ttf";
		if (FileExists(inter.c_str()))
			Font = io.Fonts->AddFontFromFileTTF(inter.c_str(), metrics::FontBody * metrics::UiScale);
	}
#endif
	for (const char* path : candidates)
	{
		if (Font) break;
		if (!FileExists(path)) continue;
		Font = io.Fonts->AddFontFromFileTTF(path, metrics::FontBody * metrics::UiScale);
	}

	ImGui::GetStyle().FontSizeBase = metrics::FontBody * metrics::UiScale;
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

	if (newMaterialPanelOpen)
		NewMaterialPanel(*this);

	if (confirmDeleteOpen)
		ConfirmDeletePanel(*this);
}
