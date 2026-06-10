#ifndef ONI_RENDERERSELECT_MACOS_H
#define ONI_RENDERERSELECT_MACOS_H

#include "BFW.h"

#ifdef __cplusplus
extern "C" {
#endif

// If the Option/Alt modifier is held at call time, shows a native "Choose
// Renderer" dialog and returns the user's pick (UUcTrue = Metal). If Option is
// not held, returns inDefaultMetal unchanged. Runs before SDL init / window
// creation. macOS only.
UUtBool OniMac_ChooseRendererIfOptionHeld(UUtBool inDefaultMetal);

#ifdef __cplusplus
}
#endif

#endif
