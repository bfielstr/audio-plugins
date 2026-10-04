// A debug check of an editor's layout: the visible controls, labels, value boxes and panel titles
// whose rectangles overlap or touch (docs/THEME.md, "Layout"), as lines of text. EditorBase writes it
// to the file named by the environment variable PK_LAYOUT_REPORT when an editor opens (the macOS host
// tests set it and print what it finds).
#pragma once

#include <string>
#include <typeinfo>
#include <vector>

namespace VSTGUI {
class CView;
}

namespace pk {

// One line per pair: "overlap" (a shared area) or "touch" (a shared edge), each view's kind, name and
// rectangle in window coordinates. Views are compared with every other visible one, whatever
// container holds them; containers count only through their children (and a Panel through its title).
// A view counts where it draws: a label where its text is, a knob where its dial and texts are, other
// controls with their rectangle grown to any text that reaches out of it (texts are measured in the
// theme's font). A control wholly inside another (a readout placed on a display) is by design and not
// reported, and a label merely touching something is how labels sit over their controls. Texts wider
// than the room their widget keeps for them are reported as "spill".
std::vector<std::string> layoutReport (VSTGUI::CView* root);

// A class's name without its namespaces ("pk::Knob" reads "Knob"), for the report.
std::string typeName (const std::type_info& t);

} // namespace pk
