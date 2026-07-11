// BFW_LI_Gamepad_SDL.h — SDL_GameController device layer (#73).
#ifndef BFW_LI_GAMEPAD_SDL_H
#define BFW_LI_GAMEPAD_SDL_H

#include "BFW.h"
#include "BFW_LocalInput.h"
#include <SDL2/SDL.h>

UUtError LIrGamepad_Initialize(void);      // after SDL_Init; ONI_GAMEPAD=0 disables
void     LIrGamepad_Terminate(void);
UUtBool  LIrGamepad_Present(void);
void     LIrGamepad_HandleSDLEvent(const SDL_Event *inEvent);  // hot-plug
void     LIrGamepad_GetData(LItAction *outAction);             // game-mode poll
void     LIrGamepad_PumpMenuEvents(void);                      // normal-mode cursor
void     LIrGamepad_BindDefaults(void);                        // default pad bindings
void     LIrGamepad_Rumble(float inStrength01, UUtUns32 inDurationMS);

#endif
