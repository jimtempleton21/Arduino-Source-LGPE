/*  LGPE Rare Candy
 *
 *  From: https://github.com/PokemonAutomation/
 *
 */

#include "CommonFramework/Notifications/ProgramNotifications.h"
#include "CommonTools/StartupChecks/VideoResolutionCheck.h"
#include "NintendoSwitch/Commands/NintendoSwitch_Commands_PushButtons.h"
#include "NintendoSwitch/Controllers/Joycon/NintendoSwitch_Joycon.h"
#include "Pokemon/Pokemon_Strings.h"
#include "PokemonLGPE_RareCandy.h"

namespace PokemonAutomation{
namespace NintendoSwitch{
namespace PokemonLGPE{

RareCandy_Descriptor::RareCandy_Descriptor()
    : SingleSwitchProgramDescriptor(
        "PokemonLGPE:RareCandy",
        Pokemon::STRING_POKEMON + " LGPE", "Rare Candy",
        "Programs/PokemonLGPE/RareCandy.html",
        "Spam the A button four times with a minor delay, then repeat X amount of times.",
        ProgramControllerClass::SpecializedController,
        FeedbackType::REQUIRED,
        AllowCommandsWhenRunning::DISABLE_COMMANDS
    )
{}

RareCandy::RareCandy()
    : REPEAT_COUNT(
        "<b>Number of Rare Candies:</b><br>How many rare candies to use.",
        LockMode::LOCK_WHILE_RUNNING,
        10, 1, 1000
    )
    , NOTIFICATION_STATUS_UPDATE("Status Update", true, false, std::chrono::seconds(3600))
    , NOTIFICATIONS({
        &NOTIFICATION_STATUS_UPDATE,
        &NOTIFICATION_PROGRAM_FINISH,
    })
{
    PA_ADD_OPTION(REPEAT_COUNT);
    PA_ADD_OPTION(NOTIFICATIONS);
}

void RareCandy::program(SingleSwitchProgramEnvironment& env, CancellableScope& scope){
    JoyconContext context(scope, env.console.controller<RightJoycon>());
    assert_16_9_720p_min(env.logger(), env.console);

    env.log("Starting Rare Candy program. Will use " + 
            std::to_string(REPEAT_COUNT) + " rare candies.");

    uint32_t total_iterations = REPEAT_COUNT * 2;
    for (uint32_t iteration = 0; iteration < total_iterations; iteration++){
        env.log("Iteration " + std::to_string(iteration + 1) + " of " + std::to_string(total_iterations) + 
                " (Candy " + std::to_string((iteration / 2) + 1) + " of " + std::to_string(REPEAT_COUNT) + ")");
        
        // Spam A button 4 times with minor delay between each press
        for (int i = 0; i < 3; i++){
            pbf_press_button(context, BUTTON_A, 300ms, 300ms);
        }
        
        // Small delay before next iteration
        pbf_wait(context, 300ms);
    }

    env.log("Rare Candy program completed.");
    send_program_finished_notification(env, NOTIFICATION_PROGRAM_FINISH);
}




}
}
}
