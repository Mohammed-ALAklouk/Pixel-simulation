// Kept out of every raylib translation unit: windows.h collides with raylib over
// Rectangle, CloseWindow and ShowCursor, so nothing here may include raylib.h.
#include "FileDialog.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#if defined(_MSC_VER)
#pragma comment(lib, "comdlg32.lib")
#endif
#endif

std::string scree::OpenFileDialog(const char* filter, const char* title, const std::string& initialDir)
{
#if defined(_WIN32)
	char path[MAX_PATH] = {};

	// lpstrInitialDir is silently ignored when the separators are the wrong way round.
	std::string startDir = initialDir;
	for (char& ch : startDir) if (ch == '/') ch = '\\';
	while (!startDir.empty() && startDir.back() == '\\') startDir.pop_back();

	OPENFILENAMEA ofn = {};
	ofn.lStructSize = sizeof(ofn);
	ofn.lpstrFilter = filter;
	ofn.lpstrFile = path;
	ofn.nMaxFile = MAX_PATH;
	ofn.lpstrTitle = title;
	ofn.lpstrInitialDir = startDir.empty() ? nullptr : startDir.c_str();
	// NOCHANGEDIR because the dialog otherwise moves the process out of the directory
	// the asset paths are resolved against.
	ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

	if (!GetOpenFileNameA(&ofn)) return "";

	return path;
#else
	(void)filter;
	(void)title;
	(void)initialDir;
	return "";
#endif
}
