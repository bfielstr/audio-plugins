// Colours and fonts, loosely following Live's dark theme.
#pragma once

#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cfont.h"

namespace pk::theme {

using VSTGUI::CColor;

inline const CColor kBackground (30, 30, 30);
inline const CColor kPanel (42, 42, 42);
inline const CColor kPanelEdge (58, 58, 58);
inline const CColor kHeader (24, 24, 24);
inline const CColor kText (205, 205, 205);
inline const CColor kTextDim (130, 130, 130);
inline const CColor kTextBright (240, 240, 240);
inline const CColor kAccent (255, 164, 40);       // orange
inline const CColor kAccentDim (140, 95, 35);
inline const CColor kKnobTrack (70, 70, 70);
inline const CColor kControlBg (56, 56, 56);
inline const CColor kControlOn (255, 164, 40);
inline const CColor kWaveBg (22, 22, 22);
inline const CColor kWave (190, 196, 204);
inline const CColor kWaveOutside (80, 82, 86);
inline const CColor kSliceAuto (70, 150, 255);
inline const CColor kSliceManual (240, 240, 240);
inline const CColor kLoop (120, 200, 120);
inline const CColor kPlayhead (255, 230, 120);
inline const CColor kGrid (48, 48, 52);
inline const CColor kCurve (255, 164, 40);

inline VSTGUI::SharedPointer<VSTGUI::CFontDesc> font (double size, bool bold = false)
{
#if defined(_WIN32)
    const char* face = "Segoe UI";
#elif defined(__APPLE__)
    const char* face = "Helvetica Neue";
#else
    const char* face = "DejaVu Sans";
#endif
    return VSTGUI::makeOwned<VSTGUI::CFontDesc> (face, size, bold ? VSTGUI::kBoldFace : 0);
}

} // namespace pk::theme
