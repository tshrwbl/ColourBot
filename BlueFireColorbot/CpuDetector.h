#pragma once

#include "DetectionCommon.h"

#include <cstdint>
#include <vector>

namespace colourbot {

class CpuDetector {
public:
	using PixelBuffer = const std::uint8_t*;

	void ResetSingleTarget() noexcept;

	[[nodiscard]] bool FirstColor(PixelBuffer data, int height, int width, int rowPitch, int trueX, int trueY, Vector2& targetOut) const;
	[[nodiscard]] bool CustomPriority(PixelBuffer data, int height, int width, int rowPitch, int trueX, int trueY, Vector2& targetOut) const;
	[[nodiscard]] bool SingleTargetPriority(
		PixelBuffer data,
		int height,
		int width,
		int rowPitch,
		int trueX,
		int trueY,
		int checkingRangeSingleTarget,
		float speed,
		Vector2& targetOut);

private:
	static void CollectPurpleCandidates(PixelBuffer data, int rowPitch, int halfWidth, int halfHeight, const ScanBounds& bounds, std::vector<Vector2>& out);
	static bool PickPriorityTarget(std::vector<Vector2>& candidates, int trueX, int trueY, Vector2& targetOut);

	bool notFirstRunSingleTarget_{ false };
	Vector2 saveLastLocation_{ 0, 0 };
};

} // namespace colourbot

