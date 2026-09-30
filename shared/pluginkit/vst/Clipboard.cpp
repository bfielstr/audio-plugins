#include "Clipboard.h"

#include "vstgui/lib/cdropsource.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/controls/coptionmenu.h"

#include <mutex>

namespace pk {

using namespace VSTGUI;

namespace {
std::mutex gLock;
std::string gText; // the last text copied in this process (the clipboard where VSTGUI has none)
} // namespace

void putClipboardText (CFrame* frame, const std::string& text)
{
    {
        std::lock_guard<std::mutex> l (gLock);
        gText = text;
    }
    if (frame)
        frame->setClipboard (CDropSource::create (text.data (), (uint32_t)text.size (), IDataPackage::kText));
}

std::string clipboardText (CFrame* frame)
{
    if (frame)
        if (auto data = frame->getClipboard ())
            for (uint32_t i = 0; i < data->getCount (); ++i)
            {
                const void* buf = nullptr;
                IDataPackage::Type type;
                const uint32_t size = data->getData (i, buf, type);
                if (type == IDataPackage::kText && buf && size > 0)
                {
                    std::string s (static_cast<const char*> (buf), size);
                    while (!s.empty () && s.back () == '\0')
                        s.pop_back ();
                    return s;
                }
            }
    std::lock_guard<std::mutex> l (gLock);
    return gText;
}

int addSettingsMenuEntries (COptionMenu* menu)
{
    menu->addSeparator ();
    const int first = menu->getNbEntries ();
    menu->addEntry ("Copy Settings");
    menu->addEntry ("Paste Settings");
    return first;
}

} // namespace pk
