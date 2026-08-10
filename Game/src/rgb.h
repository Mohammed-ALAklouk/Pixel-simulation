#pragma once

#include <raylib.h>
#include <cstdint>
#include <algorithm>

/*
	This is a wrapper to represent RGB colors
	I did this so the simulation logic is decupled from the graphics library and can be easily replaced in the future if needed
*/

namespace scree {
	struct RGB {
		std::uint8_t r = 0;
		std::uint8_t g = 0;
		std::uint8_t b = 0;

		RGB() = default;

		RGB(std::uint8_t red, std::uint8_t green, std::uint8_t blue)
			: r(red), g(green), b(blue) {}
		 Color toRaylibColor() const {
			return Color{
				static_cast<std::uint8_t>(r),
				static_cast<std::uint8_t>(g),
				static_cast<std::uint8_t>(b),
				255
			};
		}

		RGB operator-(const RGB& other) const {
			return RGB(sub(r, other.r), sub(g, other.g), sub(b, other.b));
		}

		RGB operator+(const RGB& other) const {
			return RGB(add(r, other.r), add(g, other.g), add(b, other.b));
		}

		RGB operator*(float scalar) const {
			return RGB(static_cast<std::uint8_t>(r * scalar),
				static_cast<std::uint8_t>(g * scalar),
				static_cast<std::uint8_t>(b * scalar));
		}

		// The sign of (max - min) carries the direction, so a channel where min > max
		// descends. RGB's own operators can't be used here -- they saturate at 0 and would
		// flatten such a channel instead. The result is clamped rather than left to RGB's
		// uint8_t constructor, which wraps modularly: a t above 1 would turn 310 into 54.
		static RGB lerp(const RGB& min, const RGB& max, float t)
		{
			t = std::clamp(t, 0.0f, 1.0f);
			int r = int(min.r) + static_cast<int>((int(max.r) - int(min.r)) * t);
			int g = int(min.g) + static_cast<int>((int(max.g) - int(min.g)) * t);
			int b = int(min.b) + static_cast<int>((int(max.b) - int(min.b)) * t);
			return RGB(static_cast<std::uint8_t>(std::clamp(r, 0, 255)),
				static_cast<std::uint8_t>(std::clamp(g, 0, 255)),
				static_cast<std::uint8_t>(std::clamp(b, 0, 255)));
		}

	private:
		static const std::uint8_t add(std::uint8_t a, std::uint8_t b) {
			return static_cast<std::uint8_t>(std::min(a + b, 255));
		}

		static const std::uint8_t sub(std::uint8_t a, std::uint8_t b) {
			return static_cast<std::uint8_t>(std::max(a - b, 0));
		}
	};
}