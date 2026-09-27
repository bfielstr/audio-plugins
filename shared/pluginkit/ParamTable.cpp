#include "ParamTable.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <deque>

namespace pk {

namespace {

std::string fmt (const char* f, double v)
{
    char buf[64];
    std::snprintf (buf, sizeof (buf), f, v);
    return buf;
}

std::string lower (std::string s)
{
    for (auto& c : s)
        c = (char)std::tolower ((unsigned char)c);
    return s;
}


// Ratio curve: min .. 1 over the first half (log), 1 .. max over the second half (linear in 1/r,
// so that equal knob steps sound like equal changes of the output slope).
double ratioToPlain (double n, double mn, double mx)
{
    if (n <= 0.5)
        return mn * std::pow (1.0 / mn, 2.0 * n);
    if (n >= 1.0)
        return mx; // exact, so "infinity" compares equal after a round trip
    return std::min (mx, 1.0 / std::max (1.0 / mx, 1.0 - (n - 0.5) * 2.0 * (1.0 - 1.0 / mx)));
}

double ratioToNormalized (double r, double mn, double mx)
{
    r = std::clamp (r, mn, mx);
    if (r <= 1.0)
        return 0.5 * std::log (r / mn) / std::log (1.0 / mn);
    return 0.5 + (1.0 - 1.0 / r) / (2.0 * (1.0 - 1.0 / mx));
}

} // namespace

ParamTable::ParamTable (std::vector<ParamInfo> p) : params (std::move (p)) {}

int ParamInfo::stepCount () const
{
    switch (type)
    {
        case PType::Bool: return 1;
        case PType::Choice: return (int)choices.size () - 1;
        case PType::Int: return (int)(max - min);
        default: return 0;
    }
}


const ParamInfo& ParamTable::info (uint32_t id) const
{
    return params[std::min<size_t> (id, params.size () - 1)];
}

double ParamTable::toPlain (uint32_t id, double n) const
{
    const auto& p = info (id);
    n = std::clamp (n, 0.0, 1.0);
    switch (p.type)
    {
        case PType::Bool: return n >= 0.5 ? 1.0 : 0.0;
        case PType::Choice:
        case PType::Int: return p.min + std::round (n * (p.max - p.min));
        case PType::Float: break;
    }
    switch (p.curve)
    {
        case Curve::Ratio: return ratioToPlain (n, p.min, p.max);
        case Curve::Log: return p.min * std::pow (p.max / p.min, n);
        case Curve::Power3: return p.min + (p.max - p.min) * n * n * n;
        case Curve::Linear: break;
    }
    return p.min + (p.max - p.min) * n;
}

double ParamTable::toNormalized (uint32_t id, double v) const
{
    const auto& p = info (id);
    v = std::clamp (v, p.min, p.max);
    if (p.max <= p.min)
        return 0.0;
    if (p.type != PType::Float)
        return (std::round (v) - p.min) / (p.max - p.min);
    switch (p.curve)
    {
        case Curve::Ratio: return ratioToNormalized (v, p.min, p.max);
        case Curve::Log: return std::log (v / p.min) / std::log (p.max / p.min);
        case Curve::Power3: return std::cbrt ((v - p.min) / (p.max - p.min));
        case Curve::Linear: break;
    }
    return (v - p.min) / (p.max - p.min);
}


std::string ParamTable::toText (uint32_t id, double v) const
{
    const auto& p = info (id);
    if (std::fabs (v) < 0.05 && p.type == PType::Float && p.disp != Disp::Percent && p.disp != Disp::Sustain)
        v = 0.0;
    switch (p.disp)
    {
        case Disp::Choice:
        {
            int i = std::clamp ((int)std::lround (v), 0, (int)p.choices.size () - 1);
            return p.choices[(size_t)i];
        }
        case Disp::OnOff: return v >= 0.5 ? "On" : "Off";
        case Disp::Percent: return fmt ("%.0f %%", v * 100.0);
        case Disp::Hz:
            if (v >= 1000.0)
                return fmt ("%.2f kHz", v / 1000.0);
            if (v < 1.0)
                return fmt ("%.2f Hz", v);
            return fmt (v < 100.0 ? "%.1f Hz" : "%.0f Hz", v);
        case Disp::Ms:
            if (v >= 1000.0)
                return fmt ("%.2f s", v / 1000.0);
            return fmt (v < 10.0 ? "%.1f ms" : "%.0f ms", v);
        case Disp::Db:
            return fmt ("%.1f dB", v);
        case Disp::DbGain:
            if (v <= p.min + 1e-6)
                return "-inf dB";
            return fmt ("%.1f dB", v);
        case Disp::Sustain:
            if (v <= 0.0001)
                return "-inf dB";
            return fmt ("%.1f dB", 20.0 * std::log10 (v));
        case Disp::Semis:
            if (p.type == PType::Int)
                return fmt ("%.0f st", v);
            return fmt ("%.1f st", v);
        case Disp::Cents: return fmt ("%.0f ct", v);
        case Disp::Ratio: // Live-style "1 : x"
            if (v >= p.max * 0.999)
                return "1 : inf";
            return fmt (v < 10.0 ? "1 : %.2f" : (v < 100.0 ? "1 : %.1f" : "1 : %.0f"), v);
        case Disp::Curve:
            if (std::fabs (v) < 0.005)
                return "Linear";
            return fmt ("%+.0f %%", v * 100.0);
        case Disp::Pan:
        {
            int a = (int)std::lround (std::fabs (v) * 50.0);
            if (a == 0)
                return "C";
            return std::to_string (a) + (v < 0 ? "L" : "R");
        }
        case Disp::Beats:
        {
            int beats = (int)std::lround (v);
            if (beats % 4 == 0)
                return std::to_string (beats / 4) + (beats == 4 ? " Bar" : " Bars");
            return std::to_string (beats) + (beats == 1 ? " Beat" : " Beats");
        }
        case Disp::Degrees: return fmt ("%.0f\xC2\xB0", v);
        case Disp::Bpm: return fmt ("%.2f BPM", v);
        case Disp::Plain:
            if (p.type == PType::Int)
                return fmt ("%.0f", v);
            return fmt ("%.0f", v);
        case Disp::Number: return fmt ("%.2f", v);
    }
    return fmt ("%.2f", v);
}

bool ParamTable::fromText (uint32_t id, const std::string& textIn, double& out) const
{
    const auto& p = info (id);
    std::string text = lower (textIn);
    if (p.type == PType::Choice)
    {
        for (size_t i = 0; i < p.choices.size (); ++i)
            if (lower (p.choices[i]) == text)
            {
                out = (double)i;
                return true;
            }
    }
    if (p.type == PType::Bool)
    {
        if (text == "on" || text == "1" || text == "true")
        {
            out = 1.0;
            return true;
        }
        if (text == "off" || text == "0" || text == "false")
        {
            out = 0.0;
            return true;
        }
        return false;
    }
    if (p.disp == Disp::Ratio && text.find (':') != std::string::npos)
        text = text.substr (text.find (':') + 1); // "1 : 4.17" -> "4.17"
    if (text.find ("inf") != std::string::npos)
    {
        out = p.disp == Disp::Ratio ? p.max : p.min;
        return true;
    }
    char* end = nullptr;
    double v = std::strtod (text.c_str (), &end);
    if (end == text.c_str ())
    {
        if ((p.disp == Disp::Pan && (text == "c" || text == "center")) ||
            (p.disp == Disp::Curve && text.find ("lin") != std::string::npos))
        {
            out = 0.0;
            return true;
        }
        return false;
    }
    std::string rest = end;
    switch (p.disp)
    {
        case Disp::Percent: v /= 100.0; break;
        case Disp::Hz:
            if (rest.find ('k') != std::string::npos)
                v *= 1000.0;
            break;
        case Disp::Ms:
            if (rest.find ("ms") == std::string::npos && rest.find ('s') != std::string::npos)
                v *= 1000.0;
            break;
        case Disp::Sustain: v = std::pow (10.0, v / 20.0); break;
        case Disp::Curve:
            if (text.find ("lin") != std::string::npos)
                v = 0.0;
            else
                v /= 100.0;
            break;
        case Disp::Pan:
            v /= 50.0;
            if (rest.find ('l') != std::string::npos)
                v = -std::fabs (v);
            break;
        case Disp::Beats:
            if (rest.find ("bar") != std::string::npos)
                v *= 4.0;
            break;
        case Disp::Choice:
            // allow typing a numeric label such as "16" for voice counts
            for (size_t i = 0; i < p.choices.size (); ++i)
                if (std::strtod (p.choices[i], nullptr) == v)
                {
                    out = (double)i;
                    return true;
                }
            return false;
        default: break;
    }
    out = std::clamp (v, p.min, p.max);
    return true;
}


namespace make {

ParamInfo choice (uint32_t id, const char* n, const char* sn, std::vector<const char*> c, int def)
{
    const double mx = double (c.size () - 1);
    return ParamInfo {id, n, sn, PType::Choice, 0.0, mx, double (def), Curve::Linear, Disp::Choice, std::move (c)};
}

ParamInfo toggle (uint32_t id, const char* n, const char* sn, bool def)
{
    return ParamInfo {id, n, sn, PType::Bool, 0.0, 1.0, def ? 1.0 : 0.0, Curve::Linear, Disp::OnOff, {}};
}

ParamInfo percent (uint32_t id, const char* n, const char* sn, double def)
{
    return ParamInfo {id, n, sn, PType::Float, 0.0, 1.0, def, Curve::Linear, Disp::Percent, {}};
}

ParamInfo real (uint32_t id, const char* n, const char* sn, double mn, double mx, double def, Curve c, Disp d)
{
    return ParamInfo {id, n, sn, PType::Float, mn, mx, def, c, d, {}};
}

ParamInfo integer (uint32_t id, const char* n, const char* sn, double mn, double mx, double def, Disp d)
{
    return ParamInfo {id, n, sn, PType::Int, mn, mx, def, Curve::Linear, d, {}};
}

const char* keep (std::string s)
{
    static std::deque<std::string> store;
    store.push_back (std::move (s));
    return store.back ().c_str ();
}

} // namespace make

} // namespace pk
