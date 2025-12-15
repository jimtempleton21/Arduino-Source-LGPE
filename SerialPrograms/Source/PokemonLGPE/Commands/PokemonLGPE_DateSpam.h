/*  Date Spamming Routines
 *
 *  From: https://github.com/PokemonAutomation/
 *
 */

#ifndef PokemonAutomation_PokemonLGPE_Commands_DateSpam_H
#define PokemonAutomation_PokemonLGPE_Commands_DateSpam_H

#include "CommonFramework/Tools/VideoStream.h"
#include "NintendoSwitch/Controllers/Joycon/NintendoSwitch_Joycon.h"
#include "NintendoSwitch/NintendoSwitch_ConsoleHandle.h"

namespace PokemonAutomation{
namespace NintendoSwitch{
namespace PokemonLGPE{

// Verify that "Date and Time" menu item is selected (not "Time Zone") before rolling date
// If wrong menu item is selected, navigate to correct it
void verify_date_time_menu_selected(ConsoleHandle& console, JoyconContext& context);

// Navigate from home screen to System Settings and enter it (stops at System Settings menu)
// This is a minimal version that only does the home->settings navigation
void home_to_settings_only(ConsoleHandle& console, JoyconContext& context);

// OCR-guided navigation from System Settings to the date change menu with full sanity checks
// Assumes we're already in System Settings (left nav visible)
// Returns true if successfully navigated to date change menu, false otherwise
bool navigate_to_date_change_with_ocr(ConsoleHandle& console, JoyconContext& context);

void roll_date_forward_1                    (JoyconContext& context);
void roll_date_backward_N                   (JoyconContext& context, uint8_t skips);

}

}
}
#endif
