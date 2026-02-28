#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdlib>

namespace colourbot {

constexpr int kPixelStride = 4;

struct Vector2 {
	int x;
	int y;
	constexpr Vector2(int xValue = 0, int yValue = 0) noexcept : x(xValue), y(yValue) {}
	[[nodiscard]] constexpr int LenSq() const noexcept {
		return (x * x) + (y * y);
	}
	[[nodiscard]] constexpr Vector2 operator+(const Vector2& rhs) const noexcept {
		return Vector2(x + rhs.x, y + rhs.y);
	}
};

struct ScanBounds {
	int minX;
	int maxX;
	int minY;
	int maxY;
};

[[nodiscard]] inline ScanBounds MakeBounds(int centerX, int centerY, int rangeX, int rangeY, int width, int height) {
	const int boundedRangeX = (std::max)(0, rangeX);
	const int boundedRangeY = (std::max)(0, rangeY);
	return {
		(std::max)(0, centerX - boundedRangeX),
		(std::min)(width, centerX + boundedRangeX),
		(std::max)(0, centerY - boundedRangeY),
		(std::min)(height, centerY + boundedRangeY)
	};
}

[[nodiscard]] inline bool IsPurpleColor(std::uint8_t red, std::uint8_t green, std::uint8_t blue) {
	if (green >= 170) {
		return false;
	}

	if (green >= 120) {
		return std::abs(int(red) - int(blue)) <= 8 &&
			red - green >= 50 &&
			blue - green >= 50 &&
			red >= 105 &&
			blue >= 105;
	}

	return std::abs(int(red) - int(blue)) <= 13 &&
		red - green >= 60 &&
		blue - green >= 60 &&
		red >= 110 &&
		blue >= 100;
}

} // namespace colourbot

