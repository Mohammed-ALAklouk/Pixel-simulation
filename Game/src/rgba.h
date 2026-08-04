#include <raylib.h>
#include <cstdint>
#include <algorithm>

/*
	This is a wrapper to represent RGBA colors
	I did this so the simulation logic is decupled from the graphics library and can be easily replaced in the future if needed
*/

namespace PS {
	struct RGBA {
		std::uint8_t r = 0;
		std::uint8_t g = 0;
		std::uint8_t b = 0;
		std::uint8_t a = 255;

		RGBA() = default;

		RGBA(std::uint8_t red, std::uint8_t green, std::uint8_t blue, std::uint8_t alpha = 255)
			: r(red), g(green), b(blue), a(alpha) {}
		 Color toRaylibColor() const {
			return Color{static_cast<std::uint8_t>(r),
				static_cast<std::uint8_t>(g),
				static_cast<std::uint8_t>(b),
				static_cast<std::uint8_t>(a)};
		}

		RGBA operator-(const RGBA& other) const {
			return RGBA(sub(r, other.r), sub(g, other.g), sub(b, other.b), sub(a, other.a));
		}

		RGBA operator+(const RGBA& other) const {
			return RGBA(add(r, other.r), add(g, other.g), add(b, other.b), add(a, other.a));
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