#include "CpuDetector.h"

#include <algorithm>
#include <cmath>

namespace colourbot {

void CpuDetector::ResetSingleTarget() noexcept {
	notFirstRunSingleTarget_ = false;
	saveLastLocation_ = Vector2(0, 0);
}

bool CpuDetector::FirstColor(PixelBuffer data, int height, int width, int rowPitch, int trueX, int trueY, Vector2& targetOut) const {
	const int halfWidth = width / 2;
	const int halfHeight = height / 2;
	const auto bounds = MakeBounds(halfWidth, halfHeight, trueX, trueY, width, height);

	for (int y = bounds.minY; y < bounds.maxY; ++y) {
		const auto* pixel = data + (static_cast<size_t>(y) * static_cast<size_t>(rowPitch)) + (static_cast<size_t>(bounds.minX) * kPixelStride);
		for (int x = bounds.minX; x < bounds.maxX; ++x) {
			if (IsPurpleColor(pixel[2], pixel[1], pixel[0])) {
				targetOut = Vector2(x - halfWidth, y - halfHeight);
				return true;
			}
			pixel += kPixelStride;
		}
	}
	return false;
}

bool CpuDetector::CustomPriority(PixelBuffer data, int height, int width, int rowPitch, int trueX, int trueY, Vector2& targetOut) const {
	thread_local std::vector<Vector2> candidates;
	if (candidates.capacity() < 4096) {
		candidates.reserve(4096);
	}
	candidates.clear();

	const int halfWidth = width / 2;
	const int halfHeight = height / 2;
	const auto bounds = MakeBounds(halfWidth, halfHeight, trueX, trueY, width, height);
	CollectPurpleCandidates(data, rowPitch, halfWidth, halfHeight, bounds, candidates);
	return PickPriorityTarget(candidates, trueX, trueY, targetOut);
}

bool CpuDetector::SingleTargetPriority(
	PixelBuffer data,
	int height,
	int width,
	int rowPitch,
	int trueX,
	int trueY,
	int checkingRangeSingleTarget,
	float speed,
	Vector2& targetOut) {
	thread_local std::vector<Vector2> candidates;
	if (candidates.capacity() < 4096) {
		candidates.reserve(4096);
	}
	candidates.clear();

	const int halfWidth = width / 2;
	const int halfHeight = height / 2;

	if (!notFirstRunSingleTarget_) {
		const auto bounds = MakeBounds(halfWidth, halfHeight, trueX, trueY, width, height);
		CollectPurpleCandidates(data, rowPitch, halfWidth, halfHeight, bounds, candidates);
	}
	else {
		const int lastX = static_cast<int>((saveLastLocation_.x * (1.0f - speed)) + halfWidth);
		const int lastY = static_cast<int>((saveLastLocation_.y * (1.0f - speed)) + halfHeight);
		const auto bounds = MakeBounds(lastX, lastY, checkingRangeSingleTarget, checkingRangeSingleTarget, width, height);
		CollectPurpleCandidates(data, rowPitch, halfWidth, halfHeight, bounds, candidates);
	}

	if (!PickPriorityTarget(candidates, trueX, trueY, targetOut)) {
		notFirstRunSingleTarget_ = false;
		return false;
	}

	saveLastLocation_ = targetOut;
	notFirstRunSingleTarget_ = true;
	return true;
}

void CpuDetector::CollectPurpleCandidates(PixelBuffer data, int rowPitch, int halfWidth, int halfHeight, const ScanBounds& bounds, std::vector<Vector2>& out) {
	for (int y = bounds.minY; y < bounds.maxY; ++y) {
		const auto* pixel = data + (static_cast<size_t>(y) * static_cast<size_t>(rowPitch)) + (static_cast<size_t>(bounds.minX) * kPixelStride);
		for (int x = bounds.minX; x < bounds.maxX; ++x) {
			if (IsPurpleColor(pixel[2], pixel[1], pixel[0])) {
				out.emplace_back(x - halfWidth, y - halfHeight);
			}
			pixel += kPixelStride;
		}
	}
}

bool CpuDetector::PickPriorityTarget(std::vector<Vector2>& candidates, int trueX, int trueY, Vector2& targetOut) {
	constexpr int maxCount = 5;
	constexpr int forbiddenDistance = 100;
	constexpr int forbiddenDistanceSq = forbiddenDistance * forbiddenDistance;

	if (candidates.empty()) {
		return false;
	}

	std::sort(candidates.begin(), candidates.end(), [](const Vector2& lhs, const Vector2& rhs) {
		return lhs.y < rhs.y;
	});

	std::vector<Vector2> forbidden;
	forbidden.reserve(maxCount + 1);
	for (const auto& current : candidates) {
		if (std::abs(current.x) > trueX || std::abs(current.y) > trueY) {
			continue;
		}

		bool canUpdate = true;
		for (const auto& existing : forbidden) {
			const auto merged = current + existing;
			if (merged.LenSq() < forbiddenDistanceSq || std::abs(current.x + existing.x) < forbiddenDistance) {
				canUpdate = false;
				break;
			}
		}

		if (canUpdate) {
			forbidden.push_back(current);
			if (static_cast<int>(forbidden.size()) > maxCount) {
				break;
			}
		}
	}

	if (forbidden.empty()) {
		return false;
	}

	const auto best = std::min_element(forbidden.begin(), forbidden.end(), [](const Vector2& lhs, const Vector2& rhs) {
		return lhs.LenSq() < rhs.LenSq();
	});
	targetOut = *best;
	return true;
}

} // namespace colourbot

