// The editors' info box (in the manner of Live's Info View): a fixed strip at the bottom of every
// editor that shows the name and the help text of whatever the mouse is over. The texts are the same
// as the floating tooltips' (setTooltipText, Help.h per plug-in), so a view's help is written once.
// EditorBase adds the strip and feeds it the view under the mouse; plug-ins only set help texts.
#pragma once

#include "vstgui/lib/cview.h"

#include <string>

namespace pk {

// The width, in characters, the floating tooltips wrap to (pk::helptext::wrap).
inline constexpr size_t kTooltipChars = 50;

// Sets `v`'s help: its floating tooltip and, for the info box, a title to show above the text (the
// displays and buttons have no parameter name to show). title may be null: the info box then takes a
// short "Title: ..." lead-in of the text, or shows the text alone.
void setHelp (VSTGUI::CView* v, const char* title, const char* text);

// Wraps `v`'s tooltip to kTooltipChars a line for the floating tooltip, keeping the unwrapped text
// for the info box (it wraps to its own width). Safe to call again: a tooltip already wrapped is left
// alone, one set again since (setTooltipText) is wrapped afresh. prepareTooltips does a whole tree.
void prepareTooltip (VSTGUI::CView* v);
void prepareTooltips (VSTGUI::CView* root);

// What the info box shows for `v`: the first view from `v` up through its parents that has a help
// text or is bound to a parameter. title: the parameter's full name, the title given to setHelp, or
// the text's short lead-in; text: the help, unwrapped. Both empty when nothing on the way has help.
struct HelpInfo
{
    std::string title, text;
};
HelpInfo helpFor (VSTGUI::CView* v);

class InfoBox : public VSTGUI::CView
{
public:
    // The strip EditorBase adds under an editor's content: its height, the box inside it inset by
    // 8 px at the left, right and bottom (the content above already ends 8 px short of it).
    static constexpr double kStripHeight = 60.0;
    // The width of the title column at the left of the box (the text runs to the right of it).
    static constexpr double kTitleWidth = 150.0;

    explicit InfoBox (const VSTGUI::CRect& r);
    // Shows the help of the view under the mouse (null: nothing, a hint instead).
    void showFor (VSTGUI::CView* v);
    const HelpInfo& shown () const { return info; }
    void draw (VSTGUI::CDrawContext* ctx) override;

private:
    HelpInfo info;
};

} // namespace pk
