#pragma once

#include <array>
#include <string_view>

namespace colourbot {

struct ConfigData {
	float speed{};
	int maxX{};
	int maxY{};
	std::array<int, 2> offset{};
	bool flickAim{};
	int flickAimTime{};
	int full360{};
	int sortingCounter{};
	int holdKey{};
	bool isHold{};
	bool invertHold{};
	bool recoilControl{};
	bool overloadManualInputs{};
};

bool LoadConfigFile(std::string_view filePath, ConfigData& data);
bool SaveConfigFile(std::string_view filePath, const ConfigData& data, int holdKeyIndex, const int* holdKeyCodes, int holdKeyCount);

} // namespace colourbot

