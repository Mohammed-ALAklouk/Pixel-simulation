#pragma once

#include <string>

namespace scree {
	// Empty when the user cancels or the platform has no dialog.
	std::string OpenFileDialog(const char* filter, const char* title, const std::string& initialDir = "");
}
