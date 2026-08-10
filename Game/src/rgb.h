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

	private:
		static const std::uint8_t add(std::uint8_t a, std::uint8_t b) {
			return static_cast<std::uint8_t>(std::min(a + b, 255));
		}

		static const std::uint8_t sub(std::uint8_t a, std::uint8_t b) {
			return static_cast<std::uint8_t>(std::max(a - b, 0));
		}
	};
}