#include "sg/messages.h"
#include "sg/rng.h"
#include "sg/version.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

int failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__,   \
                         __LINE__, #cond);                                \
            ++failures;                                                   \
        }                                                                 \
    } while (0)

void rngMatchesJavaScript() {
    // Golden values from the JS mulberry32 the parity harness installs as
    // Math.random (printed with toPrecision(17) under Node).
    struct Case { std::uint32_t seed; double v[5]; };
    const Case cases[] = {
        {0u, {0.26642920868471265, 0.00032974570058286190, 0.22327202744781971,
              0.14620214793831110, 0.46732782293111086}},
        {1u, {0.62707394058816135, 0.0027357211802154779, 0.52744703995995224,
              0.98105096747167408, 0.96837789821438491}},
        {12345u, {0.97972826776094735, 0.30675226449966431, 0.48420542152598500,
                  0.81793441250920296, 0.50942836934700608}},
        {0xdeadbeefu, {0.94136961409822106, 0.26719574979506433, 0.77203335752710700,
                       0.35816076025366783, 0.47554167779162526}},
    };
    for (const auto& c : cases) {
        sg::Rng rng(c.seed);
        for (double expected : c.v) CHECK(rng.next() == expected);  // bit-exact
    }
}

void modSourcesMatchReference() {
    CHECK(sg::kNumModSources == 24);
    CHECK(sg::modSourceIndex("env1") == sg::kSrcEnv0);
    CHECK(sg::modSourceIndex("lfo1") == sg::kSrcLfo0);
    CHECK(sg::modSourceIndex("velocity") == sg::kSrcVelocity);
    CHECK(sg::modSourceIndex("keytrack") == sg::kSrcKeytrack);
    CHECK(sg::modSourceIndex("random") == sg::kSrcRandom);
    CHECK(sg::modSourceIndex("macro1") == sg::kSrcMacro0);
    CHECK(sg::modSourceIndex("modwheel") == sg::kSrcModwheel);
    CHECK(sg::modSourceIndex("pitchwheel") == sg::kSrcPitchwheel);
    CHECK(sg::modSourceIndex("aftertouch") == sg::kSrcAftertouch);
    CHECK(sg::modSourceIndex("nope") == -1);
    CHECK(sg::kModSources[sg::kSrcKeytrack].bipolar);
    CHECK(sg::kModSources[sg::kSrcPitchwheel].bipolar);
    CHECK(!sg::kModSources[sg::kSrcMacro0].perVoice);
    CHECK(sg::kFxIds.size() == 8 && sg::kFxIds[4] == "reverb");
    CHECK(sg::kBlockSize == 128);
}

void versionIsSet() {
    CHECK(std::strlen(sg::versionString()) > 0);
}

} // namespace

int main() {
    rngMatchesJavaScript();
    modSourcesMatchReference();
    versionIsSet();
    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("core tests passed\n");
    return 0;
}
