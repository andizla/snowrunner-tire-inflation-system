// vanilla_balance.h: the vanilla balance factor of a tire. The tire's grip on ground, asphalt and mud comes down by
// the smallest factor that lets some vanilla tire match or beat it in all three at once; 1 when a vanilla tire
// already does, which is every vanilla tire (vanilla_tires.h, from tools\vanilla_tires.ps1). A tire that is only
// strong in one direction loses little; one that grips better than any vanilla tire everywhere ends up level with
// the best vanilla all-round tire.
#pragma once
#include "vanilla_tires.h"

inline float VanillaBalanceFactor(float ground, float asphalt, float mud)
{
    auto ratio = [](float vanilla, float v) { return v > 0.0f ? vanilla / v : 1.0e9f; };
    float best = 0.0f;
    for (const auto &t : kVanillaEnvelope)
    {
        float r = ratio(t[0], ground);
        const float a = ratio(t[1], asphalt), m = ratio(t[2], mud);
        if (a < r) r = a;
        if (m < r) r = m;
        if (r > best) best = r;
    }
    return best < 1.0f ? best : 1.0f;
}

// With the ini's VanillaBalanceStrength: 0 = off, 1 = all the way to the vanilla envelope, and above 1 past it: the
// tire's ratio to the envelope is applied again, (strength - 1) times more, so at 2 a tire with three times the
// vanilla grip ends at a third of it. A vanilla tire (factor 1) stays as it is at every strength.
inline float VanillaBalance(float ground, float asphalt, float mud, float strength)
{
    const float f = VanillaBalanceFactor(ground, asphalt, mud);
    if (strength <= 1.0f) return 1.0f - strength * (1.0f - f);
    return f * (1.0f - (strength - 1.0f) * (1.0f - f));
}
