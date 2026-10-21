#ifdef GROOVE_CORE_H_INCLUDED
 /* When you add this cpp file to your project, you mustn't include it in a file where you've
    already included any other headers - just put it inside a file on its own, possibly with your config
    flags preceding it, but don't include anything else. That also includes avoiding any automatic prefix
    header files that the compiler may be using.
 */
 #error "Incorrect use of JUCE cpp file"
#endif

#include "groove_core.h"

#include "processor/HostSync.cpp"
#include "processor/PresetManager.cpp"
#include "processor/OneKnobProcessor.cpp"

#include "ui/GrooveLookAndFeel.cpp"
#include "ui/BigKnob.cpp"
#include "ui/SmallKnob.cpp"
#include "ui/PresetBar.cpp"
#include "ui/ToggleChips.cpp"
#include "ui/ChoiceChips.cpp"
#include "ui/MascotComponent.cpp"
#include "ui/UpdateCheck.cpp"
#include "ui/OneKnobEditor.cpp"
