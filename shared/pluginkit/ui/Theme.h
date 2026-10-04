// Colours and fonts of the suite: "aged copper + cinnabar" (docs/THEME.md). A warm-black ground with
// hair-thin copper linework; one energy colour, cinnabar, in three states (idle, live, peak) that
// appears only where there is signal, power or interaction. Structure is copper or dim line, text is
// a warm off-white. Plain copper is for lines and large shapes only: small text uses kText,
// kCopperPale or kTextDim, which keep 4.5:1 on the ground. No other hues: where a view needs to tell
// things apart it uses line style (solid / dashed), brightness (idle / live / peak, copper / pale
// copper / text), position or a label.
#pragma once

#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/clinestyle.h"

#include <cstdint>

namespace pk::theme {

using VSTGUI::CColor;

// --- the palette's roles (docs/THEME.md, "Palette")
inline constexpr CColor kGround (7, 5, 4);            // window background: warm black
inline constexpr CColor kPanel (13, 10, 8);           // slightly lifted surfaces
inline constexpr CColor kWell (4, 3, 2);              // recessed areas: displays, meter beds, fields
inline constexpr CColor kLineDim (51, 41, 31);        // secondary structure, grids, inactive tracks
inline constexpr CColor kCopper (156, 106, 76);       // primary linework (lines only, never small text)
inline constexpr CColor kCopperPale (199, 154, 124);  // small labels and microtype
inline constexpr CColor kText (212, 205, 191);        // body text, values, headings
inline constexpr CColor kTextDim (143, 137, 124);     // secondary text, units, hints
inline constexpr CColor kEnergyIdle (122, 44, 28);    // dull cinnabar: unlit indicator, meter at rest
inline constexpr CColor kEnergyLive (220, 74, 42);    // cinnabar: active state, signal present
inline constexpr CColor kEnergyPeak (255, 176, 138);  // clip / overload / momentary strike (tiny areas)

// The colour `c` with alpha `a` (translucent region shading and fades over the ground).
constexpr CColor withAlpha (const CColor& c, uint8_t a) { return CColor (c.red, c.green, c.blue, a); }

// Grid lines in three weights, all within the dim-line / copper family: the fine grid, the major lines
// (decades, octaves, every 4th beat) and the reference line (0 dB, the centre).
inline constexpr CColor kGridMinor = withAlpha (kLineDim, 180);
inline constexpr CColor kGridMajor = kLineDim;
inline constexpr CColor kGridZero = withAlpha (kCopper, 120);

// The dashed line style of the suite: the second line of a pair that the old palette told apart by
// hue (a limit, a range, the other band, the other channel).
inline const VSTGUI::CLineStyle kDashed (VSTGUI::CLineStyle::kLineCapButt, VSTGUI::CLineStyle::kLineJoinMiter, 0.0, {3.0, 3.0});

// --- the names the code used before the theme, mapped as docs/THEME.md's "Values for code" suggests
inline constexpr CColor kBackground = kGround;
inline constexpr CColor kPanelEdge = kLineDim;
inline constexpr CColor kHeader = kWell;
inline constexpr CColor kTextBright = kText;
inline constexpr CColor kAccent = kEnergyLive;
inline constexpr CColor kAccentDim = kEnergyIdle;
inline constexpr CColor kKnobTrack = kLineDim;
inline constexpr CColor kControlBg = kPanel;
inline constexpr CColor kControlOn = kEnergyLive;
inline constexpr CColor kWaveBg = kWell;
inline constexpr CColor kWave = kCopper;
inline constexpr CColor kWaveOutside = kLineDim;
inline constexpr CColor kSliceAuto = kEnergyLive;
inline constexpr CColor kSliceManual = kText;
inline constexpr CColor kLoop = kEnergyLive;
inline constexpr CColor kPlayhead = kEnergyPeak;
inline constexpr CColor kGrid = kGridMinor;
inline constexpr CColor kCurve = kEnergyLive;

// The platform UI face until Archivo / JetBrains Mono are bundled (docs/THEME.md, "Type").
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
