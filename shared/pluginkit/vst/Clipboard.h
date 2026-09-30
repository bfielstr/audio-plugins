// Text on the system clipboard (VSTGUI's, on macOS and Windows), for Copy / Paste Settings. VSTGUI has
// no clipboard on Linux: there the text is kept in the process, so copying and pasting between the
// plug-ins of one host still works.
#pragma once

#include <string>

namespace VSTGUI {
class CFrame;
class COptionMenu;
} // namespace VSTGUI

namespace pk {

void putClipboardText (VSTGUI::CFrame* frame, const std::string& text);
std::string clipboardText (VSTGUI::CFrame* frame);

// "Copy Settings" and "Paste Settings" at the end of a menu (after a separator); returns the index of
// the first, for the menu's callback to compare with.
int addSettingsMenuEntries (VSTGUI::COptionMenu* menu);

} // namespace pk
