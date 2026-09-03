#include <catch2/catch_test_macros.hpp>

#include <string>

#include "lang/Diagnostics.h"

using namespace dew::lang;

namespace
{

constexpr const char* kSource = "song {\n"
                                "  tempo 96\n"
                                "  meter 4/4\n"
                                "}\n";

/** The byte range of the first occurrence of `text`. */
SourceRange rangeOf (std::string_view source, std::string_view text)
{
    const auto at = (std::uint32_t) source.find (text);
    return { at, at + (std::uint32_t) text.size() };
}

} // namespace

TEST_CASE ("a rendered diagnostic points its carets at the token", "[score][diagnostic]")
{
    const std::string_view source { kSource };
    DiagnosticBag bag { source };

    auto& d = bag.error ("E201", "unknown key `tempo`", rangeOf (source, "tempo"),
                         "not a key of `song`");
    d.notes.push_back ("`song` accepts title, tempo, meter, grid, key and seed");
    d.helps.push_back ("did you mean `title`?");

    const auto text = render (d, source, "amber.score", bag.lines());

    INFO (text);

    REQUIRE (text.find ("amber.score:2:3: error[E201]: unknown key `tempo`") != std::string::npos);
    REQUIRE (text.find (" 2 |   tempo 96") != std::string::npos);

    // Five carets under a five-character token, indented to column 3.
    REQUIRE (text.find ("   |   ^^^^^ not a key of `song`") != std::string::npos);

    REQUIRE (text.find ("= note: `song` accepts") != std::string::npos);
    REQUIRE (text.find ("= help: did you mean `title`?") != std::string::npos);
}

TEST_CASE ("carets line up under a token that follows a multi-byte character",
           "[score][diagnostic]")
{
    // The whole reason columns count characters. With a byte-counted column the
    // caret row would be two columns to the right of the token it points at,
    // and only on lines containing non-ASCII - which no ASCII fixture catches.
    const std::string source = "title \"caf\xc3\xa9\" 96\n";
    DiagnosticBag bag { source };

    const auto at = (std::uint32_t) source.find ("96");
    auto& d = bag.error ("E210", "a title takes no number", { at, at + 2 });

    const auto text = render (d, source, "x.score", bag.lines());
    INFO (text);

    // "title "café" " is 13 characters, so 96 starts at column 14.
    REQUIRE (text.find ("x.score:1:14:") != std::string::npos);

    const auto caretLine = text.find ("^^");
    REQUIRE (caretLine != std::string::npos);

    // 13 spaces after the "   | " gutter, then the carets.
    REQUIRE (text.find ("   | " + std::string (13, ' ') + "^^") != std::string::npos);
}

TEST_CASE ("only the first error on a line is reported", "[score][diagnostic]")
{
    // A parser resynchronising after a bad token routinely produces several
    // errors on one line, all of them the first one's echo.
    const std::string_view source { kSource };
    DiagnosticBag bag { source };

    bag.error ("E101", "unexpected token", rangeOf (source, "tempo"));
    bag.error ("E102", "expected a value", rangeOf (source, "96"));

    REQUIRE (bag.size() == 1);
    REQUIRE (bag.all().front().code == "E101");

    // A different line is a different fact.
    bag.error ("E103", "also wrong", rangeOf (source, "meter"));
    REQUIRE (bag.size() == 2);
}

TEST_CASE ("warnings are not silenced by an error on the same line", "[score][diagnostic]")
{
    // Two rules relaxing at the same bar are two facts, not one repeated - so
    // the one-per-line rule is for errors only.
    const std::string_view source { kSource };
    DiagnosticBag bag { source };

    bag.error ("E101", "unexpected token", rangeOf (source, "tempo"));
    bag.warning ("W601", "a warning", rangeOf (source, "96"));
    bag.warning ("W602", "another", rangeOf (source, "tempo"));

    REQUIRE (bag.size() == 3);
    REQUIRE (bag.hasErrors());
}

TEST_CASE ("an exact repeat is said once", "[score][diagnostic]")
{
    const std::string_view source { kSource };
    DiagnosticBag bag { source };

    const auto range = rangeOf (source, "96");

    bag.warning ("W601", "same thing", range);
    bag.warning ("W601", "same thing", range);
    bag.warning ("W601", "same thing", range);

    REQUIRE (bag.size() == 1);

    // A different code at the same place is a different fact.
    bag.warning ("W602", "something else", range);
    REQUIRE (bag.size() == 2);
}

TEST_CASE ("a hundred errors is where it stops", "[score][diagnostic]")
{
    // Past a point the output is noise, and a file this broken has one cause.
    std::string source;

    for (int i = 0; i < 400; ++i)
        source += "line" + std::to_string (i) + "\n";

    DiagnosticBag bag { source };

    for (int i = 0; i < 400; ++i)
    {
        const auto at = (std::uint32_t) source.find ("line" + std::to_string (i));
        bag.error ("E101", "wrong", { at, at + 4 });
    }

    REQUIRE (bag.size() == DiagnosticBag::maxDiagnostics + 1);
    REQUIRE (bag.all().back().code == "E999");

    // Reported at the end of the file, because it is a fact about the file
    // rather than about the hundredth line - and never PAST the end, which
    // would make the editor's overlay draw off the document.
    REQUIRE (bag.all().back().primary.begin == (std::uint32_t) source.size());
    REQUIRE (bag.all().back().primary.isEmpty());
}

TEST_CASE ("a bag with no errors says so even when it holds warnings", "[score][diagnostic]")
{
    const std::string_view source { kSource };
    DiagnosticBag bag { source };

    REQUIRE_FALSE (bag.hasErrors());

    bag.warning ("W601", "a warning", rangeOf (source, "96"));

    REQUIRE_FALSE (bag.hasErrors());
    REQUIRE (bag.size() == 1);

    bag.error ("E101", "an error", rangeOf (source, "meter"));
    REQUIRE (bag.hasErrors());
}

TEST_CASE ("a related range names the second witness", "[score][diagnostic]")
{
    // The shape the grid errors need: the conflict is between two places, and a
    // message naming only one of them cannot be acted on.
    const std::string source = "rhythm a { 1/32 }\nrhythm b { 1/8t }\n";
    DiagnosticBag bag { source };

    auto& d = bag.error ("E402",
                         "this score needs a grid of 24 steps per beat, "
                         "but dew stores at most 16",
                         rangeOf (source, "1/32"), "requires 8");
    d.related.push_back ({ rangeOf (source, "1/8t"), "3 required by `1/8t`" });
    d.notes.push_back ("24 = lcm(8, 3)");

    const auto text = render (d, source, "x.score", bag.lines());
    INFO (text);

    REQUIRE (text.find ("1 | rhythm a { 1/32 }") != std::string::npos);
    REQUIRE (text.find ("= note: 3 required by `1/8t` here:") != std::string::npos);
    REQUIRE (text.find ("2 | rhythm b { 1/8t }") != std::string::npos);
}

TEST_CASE ("rendering the whole bag renders every diagnostic", "[score][diagnostic]")
{
    const std::string_view source { kSource };
    DiagnosticBag bag { source };

    bag.error ("E101", "first", rangeOf (source, "tempo"));
    bag.error ("E102", "second", rangeOf (source, "meter"));

    const auto text = renderAll (bag, source, "amber.score");

    REQUIRE (text.find ("E101") != std::string::npos);
    REQUIRE (text.find ("E102") != std::string::npos);
}
