#include "effects/native/bessel8lvmixeqeffect.h"
#include "util/math.h"

// constant to calculate the group delay from the low pass corner
// mean value of a set of fid_calc_delay() calls for different corners
static const double kGroupDelay1Hz = 0.5067964223;
// kDelayOffset is required to match short delays.
static const double kDelayOffset = 0.2;
static const double kMaxCornerFreq = 14212;

// static
QString Bessel8LVMixEQEffect::getId() {
    return "org.mixxx.effects.bessel8lvmixeq";
}

// static
EffectManifest Bessel8LVMixEQEffect::getManifest() {
    EffectManifest manifest;
    manifest.setId(getId());
    manifest.setName(QObject::tr("Bessel8 LV-Mix EQ"));
    manifest.setAuthor("The Mixxx Team");
    manifest.setVersion("1.0");
    manifest.setDescription(QObject::tr(
        "A Bessel 8th order filter equalizer with Lipshitz and Vanderkooy mix (bit perfect unity, roll-off -48 db/Oct). "
        "To adjust frequency shelves see the Equalizer preferences."));

    EffectManifestParameter* low = manifest.addParameter();
    low->setId("low");
    low->setName(QObject::tr("Low"));
    low->setDescription(QObject::tr("Gain for Low Filter"));
    low->setControlHint(EffectManifestParameter::CONTROL_KNOB_LOGARITHMIC);
    low->setSemanticHint(EffectManifestParameter::SEMANTIC_UNKNOWN);
    low->setUnitsHint(EffectManifestParameter::UNITS_UNKNOWN);
    low->setNeutralPointOnScale(0.5);
    low->setDefault(1.0);
    low->setMinimum(0);
    low->setMaximum(4.0);

    EffectManifestParameter* mid = manifest.addParameter();
    mid->setId("mid");
    mid->setName(QObject::tr("Mid"));
    mid->setDescription(QObject::tr("Gain for Band Filter"));
    mid->setControlHint(EffectManifestParameter::CONTROL_KNOB_LOGARITHMIC);
    mid->setSemanticHint(EffectManifestParameter::SEMANTIC_UNKNOWN);
    mid->setUnitsHint(EffectManifestParameter::UNITS_UNKNOWN);
    mid->setNeutralPointOnScale(0.5);
    mid->setDefault(1.0);
    mid->setMinimum(0);
    mid->setMaximum(4.0);

    EffectManifestParameter* high = manifest.addParameter();
    high->setId("high");
    high->setName(QObject::tr("High"));
    high->setDescription(QObject::tr("Gain for High Filter"));
    high->setControlHint(EffectManifestParameter::CONTROL_KNOB_LOGARITHMIC);
    high->setSemanticHint(EffectManifestParameter::SEMANTIC_UNKNOWN);
    high->setUnitsHint(EffectManifestParameter::UNITS_UNKNOWN);
    high->setNeutralPointOnScale(0.5);
    high->setDefault(1.0);
    high->setMinimum(0);
    high->setMaximum(4.0);

    return manifest;
}

Bessel8LVMixEQEffect::Bessel8LVMixEQEffect(EngineEffect* pEffect,
                                           const EffectManifest& manifest)
        : m_pPotLow(pEffect->getParameterById("low")),
          m_pPotMid(pEffect->getParameterById("mid")),
          m_pPotHigh(pEffect->getParameterById("high")) {
    Q_UNUSED(manifest);
    m_pLoFreqCorner = new ControlObjectSlave("[Mixer Profile]", "LoEQFrequency");
    m_pHiFreqCorner = new ControlObjectSlave("[Mixer Profile]", "HiEQFrequency");
}

Bessel8LVMixEQEffect::~Bessel8LVMixEQEffect() {
    delete m_pLoFreqCorner;
    delete m_pHiFreqCorner;
}

void Bessel8LVMixEQEffect::processGroup(const QString& group,
                                        Bessel8LVMixEQEffectGroupState* pState,
                                        const CSAMPLE* pInput, CSAMPLE* pOutput,
                                        const unsigned int numSamples,
                                        const unsigned int sampleRate,
                                        const GroupFeatureState& groupFeatures) {
    Q_UNUSED(group);
    Q_UNUSED(groupFeatures);

    double fLow = m_pPotLow->value();
    double fMid = m_pPotMid->value();
    double fHigh = m_pPotHigh->value();

    if (pState->m_oldSampleRate != sampleRate ||
            (pState->m_loFreq != m_pLoFreqCorner->get()) ||
            (pState->m_hiFreq != m_pHiFreqCorner->get())) {
        pState->m_loFreq = m_pLoFreqCorner->get();
        pState->m_hiFreq = m_pHiFreqCorner->get();
        pState->m_oldSampleRate = sampleRate;
        pState->setFilters(sampleRate, pState->m_loFreq, pState->m_hiFreq);
    }

    // Since a Bessel Low pass Filter has a constant group delay in the pass band,
    // we can subtract or add the filtered signal to the dry signal if we compensate this delay
    // The dry signal represents the high gain
    // Then the higher low pass is added and at least the lower low pass result.
    fLow = fLow - fMid;
    fMid = fMid - fHigh;

    if (fHigh || pState->old_high) {
        pState->m_delay3->process(pInput, pState->m_pHighBuf, numSamples);
    } else {
        pState->m_delay3->pauseFilter();
    }

    if (fMid || pState->old_mid) {
        pState->m_delay2->process(pInput, pState->m_pBandBuf, numSamples);
        pState->m_low2->process(pState->m_pBandBuf, pState->m_pBandBuf, numSamples);
    } else {
        pState->m_delay2->pauseFilter();
        pState->m_low2->pauseFilter();
    }

    if (fLow || pState->old_low) {
        pState->m_low1->process(pInput, pState->m_pLowBuf, numSamples);
    } else {
        pState->m_low1->pauseFilter();
    }

    // Test code for comparing streams as two stereo channels
    //for (unsigned int i = 0; i < numSamples; i +=2) {
    //    pOutput[i] = pState->m_pLowBuf[i];
    //    pOutput[i + 1] = pState->m_pBandBuf[i];
    //}

    if (fLow != pState->old_low ||
            fMid != pState->old_mid ||
            fHigh != pState->old_high) {
        SampleUtil::copy3WithRampingGain(pOutput,
                pState->m_pLowBuf, pState->old_low, fLow,
                pState->m_pBandBuf, pState->old_mid, fMid,
                pState->m_pHighBuf, pState->old_high, fHigh,
                numSamples);
    } else {
        SampleUtil::copy3WithGain(pOutput,
                pState->m_pLowBuf, fLow,
                pState->m_pBandBuf, fMid,
                pState->m_pHighBuf, fHigh,
                numSamples);
    }

    pState->old_low = fLow;
    pState->old_mid = fMid;
    pState->old_high = fHigh;
}
