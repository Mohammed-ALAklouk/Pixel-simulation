#pragma once

#include <raylib.h>
#include <cstdint>
#include <algorithm>

// RGB wrapper that keeps the simulation logic decoupled from the graphics library.

namespace scree {
	struct RGB {
		std::uint8_t r = 0;
		std::uint8_t g = 0;
		std::uint8_t b = 0;

		RGB() = default;

		RGB(std::uint8_t red, std::uint8_t green, std::uint8_t blue)
			: r(red), g(green), b(blue) {}
		 Color ToRaylibColor() const {
			return Color{
				static_cast<std::uint8_t>(r),
				static_cast<std::uint8_t>(g),
				static_cast<std::uint8_t>(b),
				255
			};
		}

		RGB operator-(const RGB& other) const {
			return RGB(Sub(r, other.r), Sub(g, other.g), Sub(b, other.b));
		}

		RGB operator+(const RGB& other) const {
			return RGB(Add(r, other.r), Add(g, other.g), Add(b, other.b));
		}

		RGB operator*(float scalar) const {
			return RGB(static_cast<std::uint8_t>(r * scalar),
				static_cast<std::uint8_t>(g * scalar),
				static_cast<std::uint8_t>(b * scalar));
		}

		// Done in signed int and clamped, not via RGB's operators: those saturate at 0
		// (flattening a descending channel where min > max) and wrap on overflow.
		static RGB Lerp(const RGB& min, const RGB& max, float t)
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
		static const std::uint8_t Add(std::uint8_t a, std::uint8_t b) {
			return static_cast<std::uint8_t>(std::min(a + b, 255));
		}

		static const std::uint8_t Sub(std::uint8_t a, std::uint8_t b) {
			return static_cast<std::uint8_t>(std::max(a - b, 0));
		}
	};
}