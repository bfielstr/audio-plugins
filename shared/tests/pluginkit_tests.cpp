// Headless tests for pluginkit's pure helpers (no SDK). Run: ./pluginkit_tests [filter]
#include "pluginkit/ui/HelpText.h"

#include <cstdio>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

using namespace pk::helptext;

static int gFailures = 0, gChecks = 0;
#define CHECK(cond, ...)                                                   \
    do                                                                     \
    {                                                                      \
        ++gChecks;                                                         \
        if (!(cond))                                                       \
        {                                                                  \
            ++gFailures;                                                   \
            std::printf ("    FAIL %s:%d: %s  ", __FILE__, __LINE__, #cond); \
            std::printf (__VA_ARGS__);                                     \
            std::printf ("\n");                                            \
        }                                                                  \
    } while (0)

struct TestCase
{
    const char* name;
    std::function<void ()> fn;
};
static std::vector<TestCase>& tests ()
{
    static std::vector<TestCase> t;
    return t;
}
struct Reg
{
    Reg (const char* n, std::function<void ()> f) { tests ().push_back ({n, std::move (f)}); }
};
#define TEST(name)                     \
    static void name ();               \
    static Reg reg_##name (#name, name); \
    static void name ()

static std::vector<std::string> lines (const std::string& s)
{
    std::vector<std::string> out;
    std::stringstream in (s);
    std::string l;
    while (std::getline (in, l))
        out.push_back (l);
    return out;
}

TEST (wrapBreaksLongHelpAtWords)
{
    const std::string help = "How loud the left and right voices are next to the dry centre. 0 %: bypass. 100 %: a clear left, "
                             "centre and right. 200 %: voices as loud as the centre.";
    const std::string w = wrap (help, 50);
    const auto ls = lines (w);
    CHECK (ls.size () >= 3, "%zu lines", ls.size ());
    std::string joined;
    for (const auto& l : ls)
    {
        CHECK (chars (l) <= 50, "line too long: '%s'", l.c_str ());
        CHECK (!l.empty () && l.front () != ' ' && l.back () != ' ', "spaces at a break: '%s'", l.c_str ());
        joined += (joined.empty () ? "" : " ") + l;
    }
    CHECK (joined == help, "words lost or changed: '%s'", joined.c_str ());
}

TEST (wrapKeepsShortTextAndBreaks)
{
    CHECK (wrap ("Overall output level.", 50) == "Overall output level.", "short text unchanged");
    CHECK (wrap ("first line\nsecond line", 50) == "first line\nsecond line", "existing breaks kept");
    CHECK (wrap ("", 50).empty (), "empty");
    // a word longer than the width stays whole on its own line
    const std::string w = wrap ("a supercalifragilisticexpialidocious b", 10);
    CHECK (w == "a\nsupercalifragilisticexpialidocious\nb", "long word: '%s'", w.c_str ());
}

TEST (wrapCountsUtf8Characters)
{
    // "\xC2\xB7" (a middle dot) is one character in two bytes: 10 of them and a space fit in 11
    const std::string dots = "\xC2\xB7\xC2\xB7\xC2\xB7\xC2\xB7\xC2\xB7 \xC2\xB7\xC2\xB7\xC2\xB7\xC2\xB7\xC2\xB7";
    CHECK (chars (dots) == 11, "chars %zu", chars (dots));
    CHECK (wrap (dots, 11) == dots, "fits on one line by characters, not bytes");
}

TEST (splitTitleTakesShortLeadIns)
{
    auto [t1, r1] = splitTitle ("Presets: pick one from your preset folder.");
    CHECK (t1 == "Presets" && r1 == "Pick one from your preset folder.", "'%s' / '%s'", t1.c_str (), r1.c_str ());
    auto [t2, r2] = splitTitle ("The stage from above: you at the bottom.");
    CHECK (t2.empty () && r2 == "The stage from above: you at the bottom.", "a sentence is not a title: '%s'", t2.c_str ());
    auto [t3, r3] = splitTitle ("Level of the input (to the bottom: -inf).");
    CHECK (t3.empty (), "a colon inside brackets: '%s'", t3.c_str ());
    auto [t4, r4] = splitTitle ("No colon here.");
    CHECK (t4.empty () && r4 == "No colon here.", "no title");
    auto [t5, r5] = splitTitle ("Gently's High band: where it starts to taper off.");
    CHECK (t5 == "Gently's High band" && r5 == "Where it starts to taper off.", "'%s'", t5.c_str ());
}

TEST (overlapsFindsIntersectingAndTouchingBoxes)
{
    std::vector<Box> b {{0, 0, 10, 10, "a"}, {10, 0, 20, 10, "b"}, {25, 0, 30, 10, "c"}, {5, 5, 8, 8, "d"}, {40, 0, 40, 10, "empty"}};
    const auto strict = overlaps (b, 0.0);
    CHECK (strict.size () == 1 && strict[0].first == 0 && strict[0].second == 3, "%zu strict overlaps", strict.size ());
    const auto touching = overlaps (b, 1.0);
    CHECK (touching.size () == 2, "%zu overlaps with touching", touching.size ()); // a-b touch, a-d overlap
    const auto spaced = overlaps (b, 6.0);
    CHECK (spaced.size () == 4, "%zu closer than 6 px", spaced.size ()); // also b-c and b-d (5 and 2 px apart)
}

int main (int argc, char** argv)
{
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int ran = 0;
    for (auto& t : tests ())
    {
        if (filter && std::string (t.name).find (filter) == std::string::npos)
            continue;
        const int before = gFailures;
        std::printf ("%s\n", t.name);
        t.fn ();
        std::printf ("  %s\n", gFailures == before ? "ok" : "FAILED");
        ++ran;
    }
    std::printf ("\n%d tests, %d checks, %d failures\n", ran, gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
