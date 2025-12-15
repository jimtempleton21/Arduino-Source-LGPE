/*  Overlay Text Tester
 *
 *  From: https://github.com/PokemonAutomation/
 *
 */

#ifndef PokemonAutomation_PokemonLGPE_OverlayTextTester_H
#define PokemonAutomation_PokemonLGPE_OverlayTextTester_H

#include "NintendoSwitch/NintendoSwitch_SingleSwitchProgram.h"

namespace PokemonAutomation{
namespace NintendoSwitch{
namespace PokemonLGPE{


class OverlayTextTester_Descriptor : public SingleSwitchProgramDescriptor{
public:
    OverlayTextTester_Descriptor();
};


class OverlayTextTester : public SingleSwitchProgramInstance{
public:
    OverlayTextTester();
    virtual void program(SingleSwitchProgramEnvironment& env, CancellableScope& scope) override;

    virtual void start_program_border_check(
        VideoStream& stream,
        FeedbackType feedback_type
    ) override{}
};




}
}
}
#endif
