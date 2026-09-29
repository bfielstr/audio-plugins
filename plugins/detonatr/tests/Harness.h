// The little test harness Detonatr's test programs share: #include it, write TESTs, and end
// the file with DETONATR_TEST_MAIN.
#pragma once

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

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

#define DETONATR_TEST_MAIN                                                          \
int main (int argc, char** argv)                                                    \
{                                                                                   \
    const char* filter = argc > 1 ? argv[1] : nullptr;                              \
    int ran = 0;                                                                    \
    for (auto& t : tests ())                                                        \
    {                                                                               \
        if (filter && std::string (t.name).find (filter) == std::string::npos)      \
            continue;                                                               \
        const int before = gFailures;                                               \
        std::printf ("%s\n", t.name);                                               \
        t.fn ();                                                                    \
        std::printf ("  %s\n", gFailures == before ? "ok" : "FAILED");              \
        ++ran;                                                                      \
    }                                                                               \
    std::printf ("\n%d tests, %d checks, %d failures\n", ran, gChecks, gFailures);  \
    return gFailures == 0 ? 0 : 1;                                                  \
}
