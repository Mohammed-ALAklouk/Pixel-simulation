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

#if defined(_WIN32)
namespace {
	// lpstrInitialDir is silently ignored when the separators are the wrong way round.
	std::string NormalizeDir(const std::string& dir)
	{
		std::string out = dir;
		for (char& ch : out) if (ch == '/') ch = '\\';
		while (!out.empty() && out.back() == '\\') out.pop_back();

		return out;
	}
}
#endif

std::string scree::OpenFileDialog(const char* filter, const char* title, const std::string& initialDir)
{
#if defined(_WIN32)
	char path[MAX_PATH] = {};
	const std::string startDir = NormalizeDir(initialDir);

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

std::string scree::SaveFileDialog(const char* filter, const char* title, const std::string& initialDir,
	const std::string& suggestedName)
{
#if defined(_WIN32)
	char path[MAX_PATH] = {};
	suggestedName.copy(path, sizeof(path) - 1);
	const std::string startDir = NormalizeDir(initialDir);

	OPENFILENAMEA ofn = {};
	ofn.lStructSize = sizeof(ofn);
	ofn.lpstrFilter = filter;
	ofn.lpstrFile = path;
	ofn.nMaxFile = MAX_PATH;
	ofn.lpstrTitle = title;
	ofn.lpstrInitialDir = startDir.empty() ? nullptr : startDir.c_str();
	ofn.lpstrDefExt = "json";
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;

	if (!GetSaveFileNameA(&ofn)) return "";

	return path;
#else
	(void)filter;
	(void)title;
	(void)initialDir;
	(void)suggestedName;
	return "";
#endif
}
