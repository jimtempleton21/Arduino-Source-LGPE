/*  LGPE Daily Item Farmer
 *
 *  From: https://github.com/PokemonAutomation/
 *
 */

#include <algorithm>
#include <cctype>
#include <map>
#include <string>
#include "Common/Cpp/Color.h"
#include "CommonFramework/Exceptions/OperationFailedException.h"
#include "CommonFramework/ImageTools/ImageBoxes.h"
#include "CommonFramework/ImageTypes/ImageViewRGB32.h"
#include "CommonFramework/Language.h"
#include "CommonFramework/Notifications/ProgramNotifications.h"
#include "CommonFramework/ProgramStats/StatsTracking.h"
#include "CommonFramework/VideoPipeline/VideoFeed.h"
#include "CommonFramework/VideoPipeline/VideoOverlayScopes.h"
//#include "CommonTools/Async/InferenceRoutines.h"
#include "CommonTools/OCR/OCR_RawOCR.h"
#include "CommonTools/StartupChecks/VideoResolutionCheck.h"
#include "NintendoSwitch/NintendoSwitch_Settings.h"
#include "NintendoSwitch/Commands/NintendoSwitch_Commands_PushButtons.h"
#include "NintendoSwitch/Controllers/Joycon/NintendoSwitch_Joycon.h"
#include "NintendoSwitch/Programs/NintendoSwitch_GameEntry.h"
#include "NintendoSwitch/Programs/DateSpam/NintendoSwitch_HomeToDateTime.h"
#include "Pokemon/Pokemon_Strings.h"
//#include "CommonTools/VisualDetectors/BlackScreenDetector.h"
#include "PokemonLGPE/Commands/PokemonLGPE_DateSpam.h"
//#include "PokemonLGPE/Inference/PokemonLGPE_ShinySymbolDetector.h"
//#include "PokemonLGPE/Programs/PokemonLGPE_GameEntry.h"
#include "PokemonSwSh/Commands/PokemonSwSh_Commands_DateSpam.h"
#include "PokemonLGPE_DailyItemFarmer.h"

namespace PokemonAutomation{
namespace NintendoSwitch{
namespace PokemonLGPE{

DailyItemFarmer_Descriptor::DailyItemFarmer_Descriptor()
    : SingleSwitchProgramDescriptor(
        "PokemonLGPE:DailyItemFarmer",
        Pokemon::STRING_POKEMON + " LGPE", "Daily Item Farmer",
        "Programs/PokemonLGPE/DailyItemFarmer.html",
        "Farm daily item respawns (ex. fossils) by date-skipping.",
        ProgramControllerClass::SpecializedController,
        FeedbackType::REQUIRED,
        AllowCommandsWhenRunning::DISABLE_COMMANDS
    )
{}

struct DailyItemFarmer_Descriptor::Stats : public StatsTracker{
    Stats()
        : skips(m_stats["Skips"])
    {
        m_display_order.emplace_back("Skips");
    }
    std::atomic<uint64_t>& skips;
};

// Helper: read and normalize text from a float box (non-blocking OCR)
static std::string read_item_text_ocr(VideoSnapshot& snapshot, const ImageFloatBox& box){
    if (!snapshot){
        return "";
    }
    
    ImageViewRGB32 image = snapshot;
    ImageViewRGB32 region = extract_box_reference(image, box);
    std::string text = OCR::ocr_read(Language::English, region);

    std::string cleaned;
    for (char ch : text){
        if (ch != '\r' && ch != '\n'){
            cleaned += ch;
        }
    }
    std::string lowered = cleaned;
    std::transform(
        lowered.begin(), lowered.end(), lowered.begin(),
        [](unsigned char c){ return (char)std::tolower(c); }
    );

    return lowered;
}

// Helper: extract item name from OCR text (e.g., "You found a Rare Candy!" -> "rare candy")
static std::string extract_item_name(const std::string& ocr_text){
    // Common patterns: "You found a [Item]!", "You got a [Item]!", "Found [Item]!", etc.
    std::string lower = ocr_text;
    
    // Remove common prefixes
    size_t found_pos = std::string::npos;
    std::vector<std::string> prefixes = {
        "you found a ", "you found ", "you got a ", "you got ", 
        "found a ", "found ", "got a ", "got ", "received a ", "received "
    };
    
    for (const auto& prefix : prefixes){
        found_pos = lower.find(prefix);
        if (found_pos != std::string::npos){
            lower = lower.substr(found_pos + prefix.length());
            break;
        }
    }
    
    // Remove trailing punctuation and whitespace
    while (!lower.empty() && (lower.back() == '!' || lower.back() == '.' || 
                              lower.back() == '?' || lower.back() == ' ' || lower.back() == '\t')){
        lower.pop_back();
    }
    
    return lower;
}
std::unique_ptr<StatsTracker> DailyItemFarmer_Descriptor::make_stats() const{
    return std::unique_ptr<StatsTracker>(new Stats());
}

DailyItemFarmer::DailyItemFarmer()
    : ATTEMPTS(
        "<b>Number of attempts:</b>",
        LockMode::LOCK_WHILE_RUNNING,
        30, 1
    )
    , LINK_CODE(
        "<b>Link Code:</b><br>The link code used when matching for a trade/battle. This only needs to be changed when running multiple LGPE date-skip programs at the same time.",
        {   //Combinations of 3 different symbols is possible but 10 choices seems like enough.
            {LinkCode::Pikachu,     "pikachu",      "Pikachu"},
            {LinkCode::Eevee,       "eevee",        "Eevee"},
            {LinkCode::Bulbasaur,   "bulbasaur",    "Bulbasaur"},
            {LinkCode::Charmander,  "charmander",   "Charmander"},
            {LinkCode::Squirtle,    "squirtle",     "Squirtle"},
            {LinkCode::Pidgey,      "pidgey",       "Pidgey"},
            {LinkCode::Caterpie,    "caterpie",     "Caterpie"},
            {LinkCode::Rattata,     "rattata",      "Rattata"},
            {LinkCode::Jigglypuff,  "jigglypuff",   "Jigglypuff"},
            {LinkCode::Diglett,     "diglett",      "Diglett"},
        },
        LockMode::LOCK_WHILE_RUNNING,
        LinkCode::Pikachu
    )
    , FIX_TIME_WHEN_DONE(
        "<b>Fix Time when Done:</b><br>Fix the time after the program finishes.",
        LockMode::UNLOCK_WHILE_RUNNING, false
    )
    , GO_HOME_WHEN_DONE(false)
    , NOTIFICATION_STATUS_UPDATE("Status Update", true, false, std::chrono::seconds(3600))
    , NOTIFICATIONS({
        &NOTIFICATION_STATUS_UPDATE,
        &NOTIFICATION_PROGRAM_FINISH,
    })
{
    PA_ADD_OPTION(ATTEMPTS);
    PA_ADD_OPTION(LINK_CODE);
    PA_ADD_OPTION(FIX_TIME_WHEN_DONE);
    PA_ADD_OPTION(GO_HOME_WHEN_DONE);
    PA_ADD_OPTION(NOTIFICATIONS);
}

void DailyItemFarmer::start_local_trade(SingleSwitchProgramEnvironment& env, JoyconContext& context){
    env.log("Starting local trade.");
    //Open Menu -> Communication -> Nearby player -> Local Trade
    pbf_press_button(context, BUTTON_X, 200ms, 1200ms);
    pbf_move_joystick(context, 255, 128, 100ms, 600ms);
    pbf_press_button(context, BUTTON_A, 200ms, 1500ms);
    pbf_press_button(context, BUTTON_A, 200ms, 2500ms); //  Black screen
    pbf_press_button(context, BUTTON_A, 200ms, 1500ms);
    pbf_press_button(context, BUTTON_A, 200ms, 1500ms);

    //Enter link code
    switch(LINK_CODE) {
    case LinkCode::Pikachu:
        break;
    case LinkCode::Eevee:
        pbf_move_joystick(context, 255, 128, 100ms, 100ms);
        break;
    case LinkCode::Bulbasaur:
        pbf_move_joystick(context, 255, 128, 100ms, 100ms);
        pbf_move_joystick(context, 255, 128, 100ms, 100ms);
        break;
    case LinkCode::Charmander:
        pbf_move_joystick(context, 0, 128, 100ms, 100ms);
        pbf_move_joystick(context, 0, 128, 100ms, 100ms);
        break;
    case LinkCode::Squirtle:
        pbf_move_joystick(context, 0, 128, 100ms, 100ms);
        break;
    case LinkCode::Pidgey:
        pbf_move_joystick(context, 128, 255, 100ms, 100ms);
        break;
    case LinkCode::Caterpie:
        pbf_move_joystick(context, 128, 255, 100ms, 100ms);
        pbf_move_joystick(context, 255, 128, 100ms, 100ms);
        break;
    case LinkCode::Rattata:
        pbf_move_joystick(context, 128, 255, 100ms, 100ms);
        pbf_move_joystick(context, 0, 128, 100ms, 100ms);
        pbf_move_joystick(context, 0, 128, 100ms, 100ms);
        break;
    case LinkCode::Jigglypuff:
        pbf_move_joystick(context, 128, 255, 100ms, 100ms);
        pbf_move_joystick(context, 0, 128, 100ms, 100ms);
        pbf_move_joystick(context, 0, 128, 100ms, 100ms);
        break;
    case LinkCode::Diglett:
        pbf_move_joystick(context, 128, 255, 100ms, 100ms);
        pbf_move_joystick(context, 0, 128, 100ms, 100ms);
        break;
    default:
        env.log("Invalid link code selection. Defaulting to Pikachu.");
        break;
    }
    //Select symbol three times, then enter link search
    pbf_press_button(context, BUTTON_A, 200ms, 300ms);
    pbf_press_button(context, BUTTON_A, 200ms, 300ms);
    pbf_press_button(context, BUTTON_A, 200ms, 300ms);
    pbf_wait(context, 1500ms); //let search start
    context.wait_for_all_requests();
}

void DailyItemFarmer::program(SingleSwitchProgramEnvironment& env, CancellableScope& scope){
    JoyconContext context(scope, env.console.controller<RightJoycon>());
    assert_16_9_720p_min(env.logger(), env.console);
    DailyItemFarmer_Descriptor::Stats& stats = env.current_stats<DailyItemFarmer_Descriptor::Stats>();

    /* Stand in front of the fossil spawn near Mewtwo.
    *  Use a repel to keep wild encounters away.
    *  Start program in-game.
    *  100% daily spawn. Only works near Mewtwo.
    *  Other cave item spawns are tied to steps taken.
    *  Should work for other hidden daily items, game corner, mt moon moonstones, etc.
    */

    uint8_t year = MAX_YEAR;

    //Roll the date back before doing anything else.
    start_local_trade(env, context);

    go_home(env.console, context);

    //  Initiating a local connection tends to mess up the wireless schedule.
    //  Thus we wait a bit for the connection to clear up.
    pbf_press_button(context, BUTTON_ZL, 160ms, 1200ms);
    pbf_press_button(context, BUTTON_ZL, 160ms, 1200ms);
    pbf_press_button(context, BUTTON_ZL, 160ms, 1200ms);
    context.wait_for_all_requests();
    context.wait_for(1500ms);

    // Navigate from home to System Settings only (stops at System Settings menu)
    home_to_settings_only(env.console, context);
    
    env.log("About to call navigate_to_date_change_with_ocr...", COLOR_BLUE);
    
    // OCR-guided navigation from System Settings to date change menu with full sanity checks
    if (!navigate_to_date_change_with_ocr(env.console, context)){
        throw OperationFailedException(
            ErrorReport::SEND_ERROR_REPORT,
            "Failed to navigate to date change menu using OCR. Aborting to prevent date manipulation errors.",
            env.console
        );
    }

    env.log("Rolling date back.");
    roll_date_backward_N(context, MAX_YEAR);
    year = 0;
    pbf_press_button(context, BUTTON_HOME, 160ms, ConsoleSettings::instance().SETTINGS_TO_HOME_DELAY0);
    pbf_press_button(context, BUTTON_HOME, 160ms, ConsoleSettings::instance().SETTINGS_TO_HOME_DELAY0);

    //  Start with one B press while the poll rate changes.
    //  If we mash here we silent disconnect the ESP32.
    pbf_press_button(context, BUTTON_B, 200ms, 1800ms);
    pbf_mash_button(context, BUTTON_B, 5000ms);
    context.wait_for_all_requests();

    env.log("Starting pickup loop.");
    
    // Track item counts for this run
    std::map<std::string, uint32_t> item_counts;
    
    for (uint32_t count = 0; count < ATTEMPTS; count++) {
        env.log("Pick up item.");

        pbf_mash_button(context, BUTTON_A, 5000ms);
        context.wait_for_all_requests();
        
        // Non-blocking OCR: Read item pickup text
        {
            context.wait_for(500ms);  // Brief wait for text to appear
            VideoSnapshot snapshot = env.console.video().snapshot();
            
            if (snapshot){
                ImageFloatBox item_text_box(0.19, 0.77, 0.62, 0.20);
                VideoOverlaySet overlays(env.console.overlay());
                overlays.add(COLOR_CYAN, item_text_box);
                
                std::string ocr_text = read_item_text_ocr(snapshot, item_text_box);
                env.log("Item pickup OCR text: \"" + ocr_text + "\"", COLOR_BLUE);
                
                if (!ocr_text.empty()){
                    std::string item_name = extract_item_name(ocr_text);
                    
                    if (!item_name.empty()){
                        item_counts[item_name]++;
                        uint32_t current_count = item_counts[item_name];
                        
                        env.log(
                            "Item detected: \"" + item_name + "\" - " + 
                            std::to_string(current_count) + " found so far (loop " + 
                            std::to_string(count + 1) + " of " + std::to_string(ATTEMPTS) + ")",
                            COLOR_GREEN
                        );
                    }else{
                        env.log("Could not extract item name from OCR text.", COLOR_YELLOW);
                    }
                }else{
                    env.log("OCR returned empty text.", COLOR_YELLOW);
                }
            }else{
                env.log("No video snapshot available for OCR.", COLOR_YELLOW);
            }
        }

        start_local_trade(env, context);

        //  Dateskip
        go_home(env.console, context);

#if 1
        //  Initiating a local connection tends to mess up the wireless schedule.
        //  Thus we wait a bit for the connection to clear up.
        pbf_press_button(context, BUTTON_ZL, 160ms, 1200ms);
        pbf_press_button(context, BUTTON_ZL, 160ms, 1200ms);
        pbf_press_button(context, BUTTON_ZL, 160ms, 1200ms);
        context.wait_for_all_requests();
        context.wait_for(1500ms);
#endif

        // Navigate from home to System Settings only (stops at System Settings menu)
        home_to_settings_only(env.console, context);
        
        env.log("About to call navigate_to_date_change_with_ocr (loop iteration)...", COLOR_BLUE);
        
        // OCR-guided navigation from System Settings to date change menu with full sanity checks
        if (!navigate_to_date_change_with_ocr(env.console, context)){
            env.log("Failed to navigate to date change menu using OCR. Skipping this iteration.", COLOR_RED);
            continue;  // Skip this iteration and try again
        }

        if (year >= MAX_YEAR){
            env.log("Rolling date back.");
            roll_date_backward_N(context, MAX_YEAR);
            year = 0;
        }else{
            env.log("Rolling date forward.");
            roll_date_forward_1(context);
            year++;
        }
        pbf_press_button(context, BUTTON_HOME, 160ms, ConsoleSettings::instance().SETTINGS_TO_HOME_DELAY0);

        //  Re-enter game and close out link menu
        pbf_press_button(context, BUTTON_HOME, 160ms, ConsoleSettings::instance().SETTINGS_TO_HOME_DELAY0);

        //  Start with one B press while the poll rate changes.
        //  If we mash here we silent disconnect the ESP32.
        pbf_press_button(context, BUTTON_B, 200ms, 2500ms);
        pbf_mash_button(context, BUTTON_B, 7000ms);
        context.wait_for_all_requests();

        stats.skips++;
        env.update_stats();
    }

    if (FIX_TIME_WHEN_DONE){
        go_home(env.console, context);
        home_to_date_time(env.console, context, false);
        pbf_press_button(context, BUTTON_A, 50ms, 500ms);
        pbf_press_button(context, BUTTON_A, 50ms, 500ms);
        pbf_wait(context, 100ms);
        context.wait_for_all_requests();
        pbf_press_button(context, BUTTON_HOME, 160ms, ConsoleSettings::instance().SETTINGS_TO_HOME_DELAY0);
        resume_game_from_home(env.console, context);
    }

    if (GO_HOME_WHEN_DONE) {
        pbf_press_button(context, BUTTON_HOME, 200ms, 1000ms);
    }
    send_program_finished_notification(env, NOTIFICATION_PROGRAM_FINISH);
}


}
}
}
