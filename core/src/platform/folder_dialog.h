#pragma once
// The system's "choose a folder" window.
//   Windows: the standard folder picker (IFileOpenDialog with FOS_PICKFOLDERS)
//   macOS:   the Finder's "choose folder" (osascript)
//   Linux:   zenity or kdialog, when one of them is installed
#include <string>

namespace dm {

// Returns true and the chosen folder; false if the user cancelled or no picker is available on this system
// (available() tells which).
bool chooseFolder(const std::string& title, const std::string& startFolder, std::string& chosen);
bool folderDialogAvailable();

} // namespace dm
