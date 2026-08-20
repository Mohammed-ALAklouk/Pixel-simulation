#pragma once

#include "MaterialData.h"
#include "rgb.h"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <raylib.h>

// Palette and chrome metrics for the UI. Everything deciding what the window looks
// like lives here; UI.cpp only arranges it.
namespace scree::ui
{
	inline ImVec4 hex(int r, int g, int b, float a = 1.0f)
	{
		return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a);
	}

	inline Color hex_rl(int r, int g, int b, int a = 255)
	{
		return Color{ static_cast<unsigned char>(r), static_cast<unsigned char>(g),
					  static_cast<unsigned char>(b), static_cast<unsigned char>(a) };
	}

	namespace col
	{
		inline const ImVec4 Bg = hex(0x0a, 0x09, 0x08);
		inline const ImVec4 Panel = hex(0x10, 0x0e, 0x0c);
		inline const ImVec4 PanelSunk = hex(0x0d, 0x0b, 0x0a);
		inline const ImVec4 Border = hex(0x22, 0x1f, 0x1c);
		inline const ImVec4 BorderSoft = hex(0x1d, 0x1a, 0x17);

		inline const ImVec4 Button = hex(0x19, 0x15, 0x12);
		inline const ImVec4 ButtonBorder = hex(0x2c, 0x26, 0x22);
		inline const ImVec4 ButtonHover = hex(0x22, 0x1d, 0x19);
		inline const ImVec4 HoverBorder = hex(0x4a, 0x40, 0x38);
		inline const ImVec4 RowHover = hex(0x1b, 0x17, 0x14);

		inline const ImVec4 Text = hex(0xe7, 0xe2, 0xd9);
		inline const ImVec4 TextDim = hex(0xb7, 0xaf, 0xa4);
		inline const ImVec4 TextMute = hex(0x78, 0x70, 0x66);
		inline const ImVec4 TextFaint = hex(0x6a, 0x63, 0x5a);
		inline const ImVec4 TextGhost = hex(0x44, 0x3f, 0x39);

		inline const ImVec4 Accent = hex(0xff, 0x7a, 0x2f);
		inline const ImVec4 AccentLight = hex(0xff, 0x8a, 0x45);
		inline const ImVec4 AccentText = hex(0xff, 0xd9, 0xbd);
		inline const ImVec4 AccentBg = hex(0x22, 0x1c, 0x17);
		inline const ImVec4 AccentBorder = hex(0x5a, 0x3d, 0x22);

		inline const ImVec4 Good = hex(0x7d, 0xd8, 0x7d);
		inline const ImVec4 Warn = hex(0xe0, 0xb6, 0x4a);
		inline const ImVec4 Bad = hex(0xe0, 0x5a, 0x4a);

		inline const ImVec4 Overlay = hex(0x0a, 0x09, 0x08, 0.78f);
		inline const ImVec4 OverlayEdge = hex(0x2b, 0x27, 0x23);

		// The area behind the grid, re-tinted into Theme::CanvasBg like every other surface.
		inline const ImVec4 CanvasBase = hex(0x07, 0x06, 0x06);

		// Deliberately outside the theme: the grid hairline stays neutral so the frame doesn't
		// read as part of the sim, and the chunk overlay keeps a fixed hue so it stays legible over any material.
		inline const Color GridEdge = hex_rl(0x2b, 0x27, 0x23);

		// The grid's own backdrop. In the prototype empty space is not flat black: Air is a
		// two-step dither between these two, which is what gives the canvas its texture.
		inline const Color GridBgLow = hex_rl(0x08, 0x08, 0x0a);
		inline const Color GridBgHigh = hex_rl(0x0a, 0x0a, 0x0c);
		inline const Color ChunkEdge = hex_rl(0xff, 0x7a, 0x2f, 0x8c);
	}

	namespace metrics
	{
		// Multiplies the geometric literals (paddings, margins, control sizes) via S(). Left at
		// 1.0: only the fonts needed to grow. The knob for a genuine whole-interface resize.
		constexpr float Scale = 1.0f;

		// Multiplies the type only. Above 1.0 because raylib gives raw pixels with no desktop
		// scale factor, so base sizes render small on a scaled display. The browser scales itself, so web is 1.0.
#if defined(PLATFORM_WEB)
		constexpr float TextScale = 1.0f;
#else
		constexpr float TextScale = 1.5f;
#endif

		// Runtime device-pixel ratio. On web both geometry (S) and type multiply by this to hold
		// physical size; Game sets it from devicePixelRatio. Desktop leaves it 1.0.
		inline float UiScale = 1.0f;

		// Bars and rows are sized off the type they have to hold, so these are the few
		// measurements that track TextScale rather than Scale.
		constexpr float TopBarH = 66.0f;
		constexpr float LeftRailW = 300.0f;
		constexpr float BottomBarH = 60.0f;
		constexpr float RowH = 38.0f;
		constexpr float BannerLineH = 29.0f;
		constexpr float BannerPad = 24.0f;
		constexpr int   BannerMaxLines = 4;
		// Where the message starts (clearing the tier label) and the room the DISMISS button needs.
		// compute_layout sizes the banner off these same numbers, so its line count matches the drawing.
		constexpr float BannerTextInset = 205.0f;
		constexpr float BannerTextRight = 125.0f;
		// Deliberately wider than the real advance at FontSmall, so the estimated line
		// count errs high and the banner is never shorter than its text.
		constexpr float BannerGlyphW = 12.0f;
		// Breathing room between the grid and the chrome. Wide enough to seat the canvas chips
		// drawn in this margin; below ~800px window height it, not the 1:1 clamp, sets the grid's size.
		constexpr float CanvasPad = 44.0f;

		constexpr float FontLabel = 13.0f * TextScale;   // section headers
		constexpr float FontSmall = 15.0f * TextScale;   // readouts, chips, buttons
		constexpr float FontBody = 17.0f * TextScale;   // material names
		constexpr float FontTitle = 21.0f * TextScale;   // wordmark

		// Letter-spacing, only on the section headers and the wordmark. Scales with the
		// type it is applied to.
		constexpr float TrackLabel = 0.8f * TextScale;
		constexpr float TrackTitle = 2.0f * TextScale;
	}

	// The interface font: the host's standard proportional UI face. Null when no candidate
	// was found, which leaves ImGui's built-in font in place -- sizes still vary, they just
	// render less crisply.
	extern ImFont* Font;

	// The whole chrome is themed off one chosen colour: every surface, border and label is the
	// matching col:: neutral re-tinted to that hue at its own lightness. Status colours
	// (Good/Warn/Bad) stay out -- a red that means "failed" must not drift with the theme.
	struct Theme
	{
		ImVec4 Panel, PanelSunk, Border, BorderSoft;
		ImVec4 Button, ButtonBorder, ButtonHover, RowHover;
		ImVec4 Text, TextDim, TextMute, TextFaint, TextGhost;
		ImVec4 Accent, AccentLight, AccentText, AccentBg, AccentBorder;
		ImVec4 Overlay, OverlayEdge;
		Color  WindowBg, CanvasBg, Ring;
	};

	extern Theme theme;

	// The picker's contents. Seeds are read only for hue and saturation (Retint takes lightness
	// from the neutral). Bone is under the saturation floor on purpose and comes out warm grey.
	struct ThemeOption
	{
		const char* name;
		RGB seed;
	};

	inline constexpr int ThemeCount = 7;
	extern const ThemeOption ThemeOptions[ThemeCount];

	// Index into ThemeOptions. Blue is entry 0.
	extern int ActiveTheme;

	// Eases towards the theme colour over a few frames rather than snapping, and writes
	// the themed entries of the ImGui style. Safe to call outside a frame; Game calls it
	// once per render before anything is drawn.
	void UpdateTheme(const RGB& theme_colour, float dt);

	// The colour a material is represented by in the UI: the midpoint of its range, which
	// is its average appearance whether it interpolates over its lifespan or not.
	RGB SwatchRGB(const MaterialData& data);

	void ApplyTheme();
	void LoadFonts();

	// Persist the choices that outlive a run (currently just the theme) in a text file next to
	// the executable. LoadSettings runs at startup before the first theme; SaveSettings on each change.
	void LoadSettings();
	void SaveSettings();
}
