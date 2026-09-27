// Table-driven parameter definitions shared by all plug-ins: normalized <-> plain mapping,
// display text and parsing. Each plug-in builds one ParamTable indexed by parameter ID.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace pk {

enum class PType { Float, Int, Choice, Bool };
// Ratio: min .. max with 1:1 at the centre (compressor / expander ratios, shown Live-style as
// "1 : x"; the maximum shows as "1 : inf").
enum class Curve { Linear, Log, Power3, Ratio };
enum class Disp { Percent, Hz, Ms, Db, DbGain, Semis, Cents, Pan, Plain, Beats, Degrees, Choice, OnOff, Sustain, Curve, Ratio };

struct ParamInfo
{
    uint32_t id;
    const char* name;
    const char* shortName;
    PType type;
    double min, max, def; // plain values (choices: index)
    Curve curve;
    Disp disp;
    std::vector<const char*> choices;

    int stepCount () const; // 0 = continuous
};

class ParamTable
{
public:
    explicit ParamTable (std::vector<ParamInfo> params);

    uint32_t size () const { return (uint32_t)params.size (); }
    bool valid (uint32_t id) const { return id < params.size (); }
    const ParamInfo& info (uint32_t id) const;

    double toPlain (uint32_t id, double normalized) const;
    double toNormalized (uint32_t id, double plain) const;
    double defaultNormalized (uint32_t id) const { return toNormalized (id, info (id).def); }
    std::string toText (uint32_t id, double plain) const;
    bool fromText (uint32_t id, const std::string& text, double& plainOut) const;

private:
    std::vector<ParamInfo> params;
};

// Helpers for building tables.
namespace make {
ParamInfo choice (uint32_t id, const char* name, const char* shortName, std::vector<const char*> choices, int def);
ParamInfo toggle (uint32_t id, const char* name, const char* shortName, bool def);
ParamInfo percent (uint32_t id, const char* name, const char* shortName, double def);
ParamInfo real (uint32_t id, const char* name, const char* shortName, double min, double max, double def, Curve c, Disp d);
ParamInfo integer (uint32_t id, const char* name, const char* shortName, double min, double max, double def, Disp d);
// Keeps generated strings alive for the lifetime of the program.
const char* keep (std::string s);
} // namespace make

} // namespace pk
