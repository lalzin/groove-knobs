/*******************************************************************************
 BEGIN_JUCE_MODULE_DECLARATION

  ID:                 groove_core
  vendor:             LALZIN
  version:            0.1.0
  name:               Groove Knobs core
  description:        Shared processor, DSP and UI building blocks for the Groove Knobs one-knob plugin suite.
  license:            Proprietary
  minimumCppStandard: 20

  dependencies:       juce_audio_utils juce_dsp juce_gui_extra

 END_JUCE_MODULE_DECLARATION
*******************************************************************************/

#pragma once
#define GROOVE_CORE_H_INCLUDED

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include <array>
#include <atomic>
#include <cmath>
#include <complex>
#include <functional>
#include <memory>
#include <optional>

#include "dsp/DspMath.h"
#include "dsp/ParamSmoother.h"
#include "dsp/TptSvf.h"
#include "dsp/Saturator.h"
#include "dsp/DcBlocker.h"
#include "dsp/AllpassPhaser.h"
#include "dsp/Grit.h"
#include "dsp/Lfo.h"
#include "dsp/DelayLine.h"
#include "dsp/SafetyClipper.h"
#include "dsp/KWeighting.h"
#include "dsp/TestSignals.h"
#include "dsp/ModDelay.h"
#include "dsp/PartitionedConvolver.h"

#include "processor/HostSync.h"
#include "processor/MacroMapper.h"
#include "processor/PresetManager.h"
#include "processor/OneKnobEngine.h"
#include "processor/AudioFeatures.h"
#include "processor/OneKnobProcessor.h"
#include "processor/OfflineRenderer.h"

#include "ui/Palette.h"
#include "ui/GrooveFonts.h"
#include "ui/GrooveLookAndFeel.h"
#include "ui/BigKnob.h"
#include "ui/SmallKnob.h"
#include "ui/PresetBar.h"
#include "ui/PresetTags.h"
#include "ui/ToggleChips.h"
#include "ui/ChoiceChips.h"
#include "ui/MascotComponent.h"
#include "ui/OneKnobEditor.h"
