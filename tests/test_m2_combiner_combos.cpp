// A skin batch's shader id, for a model that stores combiners as an array.
//
// Global flag 0x08 says the batches' `shader` fields are offsets into the
// header's texture_combiner_combos, one op per layer, rather than shader ids.
// Read as ids they are small numbers that happen to name the wrong table
// rows: the Northrend login scene's light shafts say 0, which the table calls
// Opaque_Opaque, when the array says Mod_Mod2x - and that is the difference
// between a shaft whose falloff mask is honoured and one drawn as a hard
// quad. The oracle values below are the login scene's own array and the
// [1, 6] pair most of the client's helmets carry.
#include <catch_amalgamated.hpp>

#include "pipeline/m2_loader.hpp"

#include <vector>

using wowee::pipeline::m2ShaderFromCombinerCombos;
using wowee::pipeline::m2TexCombiner;

TEST_CASE("the login scene's light shafts resolve to Mod_Mod2x", "[m2]") {
    // UI_MainMenu_Northrend.m2: flag 0x08, combos [1, 4, 1, 1].
    const std::vector<uint16_t> combos{1, 4, 1, 1};
    // Batches 20 and 21 (ICECROWN_LIGHTRAY_01 over CLOUDSA02_MASK04) say 0.
    const uint16_t shafts = m2ShaderFromCombinerCombos(0, 2, combos);
    CHECK(shafts == 0x14);
    CHECK(m2TexCombiner(2, shafts, 4) == 6);   // Mod_Mod2x, whatever the blend
    CHECK(m2TexCombiner(2, shafts, 2) == 6);
}

TEST_CASE("the login scene's glow sheets resolve to Mod_Mod", "[m2]") {
    const std::vector<uint16_t> combos{1, 4, 1, 1};
    // Batches 16-19 and 40 (ICECROWN_GLOW0x over CLOUDSA02_MASK01) say 2.
    const uint16_t glow = m2ShaderFromCombinerCombos(2, 2, combos);
    CHECK(glow == 0x11);
    CHECK(m2TexCombiner(2, glow, 4) == 5);     // Mod_Mod
}

TEST_CASE("a helmet's specular sheet resolves to Mod_Mod2xNA", "[m2]") {
    const std::vector<uint16_t> combos{1, 6};
    const uint16_t spec = m2ShaderFromCombinerCombos(0, 2, combos);
    CHECK(spec == 0x16);
    CHECK(m2TexCombiner(2, spec, 0) == 8);     // Mod_Mod2xNA
}

TEST_CASE("a model without the array keeps its shader ids", "[m2]") {
    const std::vector<uint16_t> none;
    CHECK(m2ShaderFromCombinerCombos(0, 2, none) == 0);
    CHECK(m2ShaderFromCombinerCombos(0x14, 2, none) == 0x14);
    CHECK(m2ShaderFromCombinerCombos(0x8001, 2, none) == 0x8001);
}

TEST_CASE("one-layer batches and offsets past the array are left alone", "[m2]") {
    const std::vector<uint16_t> combos{1, 4, 1, 1};
    // One layer: m2TexCombiner draws it alone whatever the field says.
    CHECK(m2ShaderFromCombinerCombos(0, 1, combos) == 0);
    CHECK(m2ShaderFromCombinerCombos(3, 1, combos) == 3);
    // Two layers from an offset with no second entry: not a pair.
    CHECK(m2ShaderFromCombinerCombos(3, 2, combos) == 3);
    CHECK(m2ShaderFromCombinerCombos(4, 2, combos) == 4);
}

TEST_CASE("Opaque_Opaque takes the material's alpha only for the additive modes", "[m2]") {
    // A raw zero on a model without the array: alpha from the material when
    // the batch adds (modes 3 and 4), from the second layer when it blends or
    // covers. Modulate (5) and Modulate2x (6) sit above the additive pair in
    // the enumeration and are not additive; an open-ended ">= 3" swept them in.
    CHECK(m2TexCombiner(2, 0, 3) == 4);    // Opaque_Opaque
    CHECK(m2TexCombiner(2, 0, 4) == 4);
    CHECK(m2TexCombiner(2, 0, 0) == 1);    // Opaque_Mod
    CHECK(m2TexCombiner(2, 0, 2) == 1);
    CHECK(m2TexCombiner(2, 0, 5) == 1);
    CHECK(m2TexCombiner(2, 0, 6) == 1);
}
