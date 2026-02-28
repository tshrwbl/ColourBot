#include "ConfigIO.h"

#include <charconv>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

namespace colourbot {

namespace {

std::string RemoveWhitespace(std::string_view source) {
	std::string result;
	result.reserve(source.size());
	for (const char ch : source) {
		if (!std::isspace(static_cast<unsigned char>(ch))) {
			result.push_back(ch);
		}
	}
	return result;
}

template <typename TInt>
bool ParseInteger(std::string_view text, TInt& output) {
	const auto begin = text.data();
	const auto end = begin + text.size();
	const auto [ptr, ec] = std::from_chars(begin, end, output);
	return ec == std::errc{} && ptr == end;
}

bool ParseFloat(std::string_view text, float& output) {
	std::string temp(text);
	char* parseEnd = nullptr;
	const float value = std::strtof(temp.c_str(), &parseEnd);
	if (parseEnd != (temp.c_str() + temp.size())) {
		return false;
	}
	output = value;
	return true;
}

bool ParseBool(std::string_view text, bool& output) {
	int numeric = 0;
	if (ParseInteger(text, numeric)) {
		output = numeric != 0;
		return true;
	}
	if (text == "true" || text == "TRUE") {
		output = true;
		return true;
	}
	if (text == "false" || text == "FALSE") {
		output = false;
		return true;
	}
	return false;
}

} // namespace

bool LoadConfigFile(std::string_view filePath, ConfigData& data) {
	std::ifstream configFile{ std::string(filePath) };
	if (!configFile.is_open()) {
		return false;
	}

	std::string line;
	while (std::getline(configFile, line)) {
		const std::string cleaned = RemoveWhitespace(line);
		if (cleaned.empty() || cleaned[0] == '#') {
			continue;
		}

		const size_t delimiterPos = cleaned.find('=');
		if (delimiterPos == std::string::npos || delimiterPos == 0 || delimiterPos + 1 >= cleaned.size()) {
			continue;
		}

		const std::string_view name(cleaned.data(), delimiterPos);
		const std::string_view value(cleaned.data() + delimiterPos + 1, cleaned.size() - delimiterPos - 1);

		if (name == "speed") {
			ParseFloat(value, data.speed);
		}
		else if (name == "maxX") {
			ParseInteger(value, data.maxX);
		}
		else if (name == "maxY") {
			ParseInteger(value, data.maxY);
		}
		else if (name == "offsetX") {
			ParseInteger(value, data.offset[0]);
		}
		else if (name == "offsetY") {
			ParseInteger(value, data.offset[1]);
		}
		else if (name == "flickAim") {
			ParseBool(value, data.flickAim);
		}
		else if (name == "flickAimTime") {
			ParseInteger(value, data.flickAimTime);
		}
		else if (name == "full360") {
			ParseInteger(value, data.full360);
		}
		else if (name == "sortingCounter") {
			ParseInteger(value, data.sortingCounter);
		}
		else if (name == "holdKey") {
			ParseInteger(value, data.holdKey);
		}
		else if (name == "isHold") {
			ParseBool(value, data.isHold);
		}
		else if (name == "invertHold") {
			ParseBool(value, data.invertHold);
		}
		else if (name == "recoilControl") {
			ParseBool(value, data.recoilControl);
		}
		else if (name == "overloadManualInputs") {
			ParseBool(value, data.overloadManualInputs);
		}
	}

	return true;
}

bool SaveConfigFile(std::string_view filePath, const ConfigData& data, int holdKeyIndex, const int* holdKeyCodes, int holdKeyCount) {
	std::ofstream configFile{ std::string(filePath), std::ios::trunc };
	if (!configFile.is_open()) {
		return false;
	}

	const auto writeSetting = [&configFile](std::string_view name, const auto value) {
		configFile << name << "=" << value << '\n';
	};

	writeSetting("speed", data.speed);
	writeSetting("maxX", data.maxX);
	writeSetting("maxY", data.maxY);
	writeSetting("offsetX", data.offset[0]);
	writeSetting("offsetY", data.offset[1]);
	writeSetting("flickAim", static_cast<int>(data.flickAim));
	writeSetting("flickAimTime", data.flickAimTime);
	writeSetting("full360", data.full360);
	writeSetting("sortingCounter", data.sortingCounter);
	writeSetting("recoilControl", static_cast<int>(data.recoilControl));
	writeSetting("overloadManualInputs", static_cast<int>(data.overloadManualInputs));

	configFile << "#All keycodes can be found at https://docs.microsoft.com/en-us/windows/win32/inputdev/virtual-key-codes\n";
	if (holdKeyIndex > 0 && holdKeyIndex < holdKeyCount && holdKeyCodes != nullptr) {
		writeSetting("holdKey", holdKeyCodes[holdKeyIndex]);
	}
	else {
		writeSetting("holdKey", data.holdKey);
	}
	writeSetting("isHold", static_cast<int>(data.isHold));
	writeSetting("invertHold", static_cast<int>(data.invertHold));

	return true;
}

} // namespace colourbot
