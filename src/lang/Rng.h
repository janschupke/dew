#pragma once

#include <cstdint>
#include <string_view>

namespace dew::lang
{

/** Reproducible randomness, specified bit for bit.

    THE RULE, and the reason none of this uses the standard library: never
    std::uniform_int_distribution, std::uniform_real_distribution or
    std::shuffle for anything a score depends on. The standard specifies their
    STATISTICAL behaviour, not their algorithms - libstdc++ and libc++ produce
    different sequences from the same engine and the same seed. A score that
    renders one way on macOS and another on Linux CI is a bug that costs a week.
    juce::Random is out for a related reason: it is an LCG whose exact sequence
    would become part of the file format the moment a seeded score is rendered.

    So: splitmix64 to derive keys, PCG32 to draw from them, and Lemire's method
    for a bounded integer. All three are thirty lines and exact everywhere.
*/

constexpr std::uint64_t splitmix64 (std::uint64_t x) noexcept
{
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

constexpr std::uint64_t fnv1a64 (std::string_view text) noexcept
{
    std::uint64_t hash = 0xCBF29CE484222325ULL;

    for (const auto byte : text)
    {
        hash ^= (std::uint64_t) (unsigned char) byte;
        hash *= 0x100000001B3ULL;
    }

    return hash;
}

/** One step down the structural tree: song -> section -> instance -> channel ->
    site -> bar -> onset.

    Mixed rather than concatenated, so a key depends only on its ancestors and
    its own label - never on its siblings, and never on how many nodes precede
    it in the file. That is what makes editing one section leave every other
    section's notes untouched.

    The label must be STRUCTURAL - "section:verse", "melody/cadence" - and never
    a byte offset, or inserting a blank line reshuffles the song.
*/
constexpr std::uint64_t childKey (std::uint64_t parent, std::string_view label) noexcept
{
    return splitmix64 (parent + fnv1a64 (label));
}

/** A PCG32 stream. Cheap enough to build fresh at every decision point, which
    is exactly how it is used: draws are STATELESS across decisions.

    Advancing one shared stream would mean that adding a single `choose` at bar 3
    silently rewrites bars 4 onward - the classic procedural-generation
    regression, invisible until someone notices the song changed. Seeding a fresh
    Rng from each decision's own derived key costs one splitmix64 and removes the
    whole class of bug.
*/
class Rng
{
public:
    explicit constexpr Rng (std::uint64_t seed) noexcept
        : state (0), increment ((seed << 1u) | 1u)
    {
        nextBits();
        state += splitmix64 (seed);
        nextBits();
    }

    /** The next 32 bits. */
    constexpr std::uint32_t nextBits() noexcept
    {
        const auto previous = state;
        state = previous * 6364136223846793005ULL + increment;

        const auto xorshifted = (std::uint32_t) (((previous >> 18u) ^ previous) >> 27u);
        const auto rot = (std::uint32_t) (previous >> 59u);

        return (xorshifted >> rot) | (xorshifted << ((~rot + 1u) & 31u));
    }

    /** Uniform in [0, bound), by Lemire's method - unbiased, and identical on
        every platform because it is written out rather than delegated.
    */
    constexpr std::uint32_t below (std::uint32_t bound) noexcept
    {
        if (bound <= 1u)
            return 0;

        auto product = (std::uint64_t) nextBits() * (std::uint64_t) bound;
        auto low = (std::uint32_t) product;

        if (low < bound)
        {
            const auto threshold = (~bound + 1u) % bound;

            while (low < threshold)
            {
                product = (std::uint64_t) nextBits() * (std::uint64_t) bound;
                low = (std::uint32_t) product;
            }
        }

        return (std::uint32_t) (product >> 32u);
    }

    /** Uniform in [0, 1). 24 bits, which is every value a float can hold in
        that range without rounding surprises.
    */
    constexpr float unitFloat() noexcept
    {
        return (float) (nextBits() >> 8u) * (1.0f / 16777216.0f);
    }

    /** Uniform in [-amount, amount]. The `+-` form in the language. */
    constexpr float jitter (float amount) noexcept
    {
        return (unitFloat() * 2.0f - 1.0f) * amount;
    }

private:
    std::uint64_t state;
    std::uint64_t increment;
};

/** A structural path, built up as generation descends and turned into a key.

    Held as a key rather than a string so that descending is one mix rather than
    a concatenation and a hash of an ever-longer string.
*/
class SeedPath
{
public:
    explicit constexpr SeedPath (std::uint64_t songSeed) noexcept
        : key (splitmix64 (songSeed))
    {
    }

    constexpr SeedPath child (std::string_view label) const noexcept
    {
        return SeedPath { key, label };
    }

    /** An indexed step - "instance:2", "bar:5" - without building the string. */
    constexpr SeedPath child (std::string_view label, int ordinal) const noexcept
    {
        return SeedPath { splitmix64 (childKey (key, label) + (std::uint64_t) ordinal
                                      + 0x1000193ULL) };
    }

    constexpr std::uint64_t value() const noexcept { return key; }

    constexpr Rng rng() const noexcept { return Rng { key }; }

private:
    constexpr SeedPath (std::uint64_t parent, std::string_view label) noexcept
        : key (childKey (parent, label))
    {
    }

    std::uint64_t key;
};

} // namespace dew::lang
