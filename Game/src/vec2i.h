#pragma once

struct Vec2i
{
	int x = 0;
	int y = 0;

	constexpr Vec2i operator-(const Vec2i& other) const
	{
		return Vec2i{ x - other.x, y - other.y };
	}
	constexpr Vec2i operator+(const Vec2i& other) const
	{
		return Vec2i{ x + other.x, y + other.y };
	}
	constexpr Vec2i operator/(float scalar) const
	{
		return Vec2i{ static_cast<int>(x / scalar), static_cast<int>(y / scalar) };
	}
	constexpr Vec2i operator*(float scalar) const
	{
		return Vec2i{ static_cast<int>(x * scalar), static_cast<int>(y * scalar) };
	}
	constexpr bool operator==(const Vec2i& other) const
	{
		return x == other.x && y == other.y;
	}
};