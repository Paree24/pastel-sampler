#pragma once
#include <juce_core/juce_core.h>
#include <utility>
#include <vector>
#include "Params.h"

// Factory presets: sound recipes played on the loaded sample. Applying one resets every
// parameter except the root note to its default, then sets the values below, so a recipe
// works on any sample (the sample and its root stay as they are).
namespace FactoryPresets
{
    struct Recipe { juce::String name; std::vector<std::pair<juce::String, float>> values; };

    inline const std::vector<Recipe>& all()
    {
        using namespace PP;
        static const std::vector<Recipe> r = {
            { "Init", {} },
            { "Soft Keys", { { E_A, 4 }, { E_D, 900 }, { E_S, 0.55f }, { E_R, 450 },
                             { F_ON, 1 }, { F_CUT, 4200 }, { F_RES, 0.15f }, { F_KEY, 0.5f },
                             { RVON, 1 }, { RVSIZE, 0.35f }, { RVMIX, 0.2f } } },
            { "Glass Partials", { { ENGINE, 1 }, { A_PARTIALS, 24 }, { A_TILT, -2.5f }, { E_A, 30 }, { E_R, 1200 },
                                  { CHON, 1 }, { CHMODE, 1 }, { RVON, 1 }, { RVSIZE, 0.6f }, { RVMIX, 0.3f } } },
            { "Frozen Air", { { ENGINE, 2 }, { P_FREEZE, 0.85f }, { P_BLUR, 0.55f }, { E_A, 900 }, { E_R, 2500 },
                              { RVON, 1 }, { RVSIZE, 0.85f }, { RVMIX, 0.45f },
                              { modSrcId(0), 1 }, { modDstId(0), 12 }, { modAmtId(0), 0.15f }, { L_RATE[0], 0.15f } } },
            { "Grain Cloud", { { ENGINE, 3 }, { G_SIZE, 140 }, { G_DENS, 45 }, { G_SPRAY, 120 }, { G_WIDTH, 1 },
                               { G_SCAN, 0.25f }, { G_JITTER, 12 }, { E_A, 400 }, { E_R, 1800 },
                               { RVON, 1 }, { RVSIZE, 0.7f }, { RVMIX, 0.35f } } },
            { "Scrub Pad", { { ENGINE, 3 }, { G_SCAN, 0 }, { G_POS, 0.35f }, { G_SIZE, 180 }, { G_DENS, 30 }, { G_SPRAY, 40 },
                             { E_A, 250 }, { E_R, 1500 },
                             { modSrcId(0), 1 }, { modDstId(0), 6 }, { modAmtId(0), 0.2f }, { L_RATE[0], 0.08f },
                             { modSrcId(1), 3 }, { modDstId(1), 7 }, { modAmtId(1), -0.5f } } },
            { "Wobble Bass", { { V_MODE, 1 }, { V_GLIDE, 40 }, { F_ON, 1 }, { F_CUT, 500 }, { F_RES, 0.45f }, { F_DRIVE, 0.3f },
                               { L_SYNC[0], 1 }, { L_DIV[0], 6 }, { L_WAVE[0], 1 },
                               { modSrcId(0), 1 }, { modDstId(0), 2 }, { modAmtId(0), 0.45f },
                               { modSrcId(1), 3 }, { modDstId(1), 3 }, { modAmtId(1), 0.3f } } },
            { "Shimmer Blend", { { ENGINE, 4 }, { S_LEVEL, -6 }, { A_LEVEL, -12 }, { P_LEVEL, -3 }, { G_LEVEL, -48 },
                                 { P_SHIFT, 12 }, { E_A, 120 }, { E_R, 2200 },
                                 { RVON, 1 }, { RVSIZE, 0.9f }, { RVMIX, 0.5f }, { DLON, 1 }, { DLSYNC, 1 }, { DLMIX, 0.2f } } },
            { "Arp Pluck", { { ARP_ON, 1 }, { ARP_DIV, 6 }, { ARP_OCT, 2 }, { ARP_GATE, 0.5f },
                             { E_A, 1 }, { E_D, 220 }, { E_S, 0 }, { E_R, 180 },
                             { F_ON, 1 }, { F_CUT, 2500 }, { modSrcId(0), 5 }, { modDstId(0), 2 }, { modAmtId(0), 0.4f },
                             { DLON, 1 }, { DLSYNC, 1 }, { DLDIV, 2 }, { DLMIX, 0.25f } } },
            { "Vibrato Lead", { { V_MODE, 2 }, { V_GLIDE, 70 }, { E_A, 15 }, { E_R, 250 },
                                { L_RATE[1], 5.5f }, { modSrcId(0), 2 }, { modDstId(0), 1 }, { modAmtId(0), 0.012f },
                                { modSrcId(1), 4 }, { modDstId(1), 2 }, { modAmtId(1), 0.35f },
                                { CHON, 1 }, { DLON, 1 }, { DLSYNC, 1 }, { DLMIX, 0.18f } } },
        };
        return r;
    }
}
