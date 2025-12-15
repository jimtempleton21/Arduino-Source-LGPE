/*  Overlay Text Tester
 *
 *  From: https://github.com/PokemonAutomation/
 *
 */

#include <iostream>
#include <algorithm>
#include <cctype>
#include "Common/Cpp/Color.h"
#include "CommonFramework/ImageTools/ImageBoxes.h"
#include "CommonFramework/ImageTools/ImageStats.h"
#include "CommonFramework/VideoPipeline/VideoFeed.h"
#include "CommonFramework/VideoPipeline/VideoOverlayScopes.h"
#include "CommonFramework/Language.h"
#include "CommonTools/OCR/OCR_RawOCR.h"
#include "NintendoSwitch/Commands/NintendoSwitch_Commands_PushButtons.h"
#include "Pokemon/Pokemon_Strings.h"
#include "NintendoSwitch/Controllers/Joycon/NintendoSwitch_Joycon.h"
#include "PokemonLGPE_OverlayTextTester.h"

namespace PokemonAutomation{
namespace NintendoSwitch{
namespace PokemonLGPE{
    using namespace Pokemon;


OverlayTextTester_Descriptor::OverlayTextTester_Descriptor()
    : SingleSwitchProgramDescriptor(
        "PokemonLGPE:OverlayTextTester",
        STRING_POKEMON + " LGPE", "Overlay Text Tester",
        "",
        "Test OCR-guided navigation on System -> Date and Time without changing the date.",
        ProgramControllerClass::SpecializedController,
        FeedbackType::REQUIRED,
        AllowCommandsWhenRunning::ENABLE_COMMANDS
    )
{}


OverlayTextTester::OverlayTextTester()
{}

// Helper: read and normalize text from a float box.
static std::string read_box_text(VideoSnapshot& snapshot, const ImageFloatBox& box){
    ImageViewRGB32 region = extract_box_reference(snapshot, box);
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

// Helper: retry logic - back out, verify System Update, navigate to Date and Time.
// Returns true if successfully navigated to Date and Time menu, false otherwise.
static bool retry_navigate_to_date_time(
    SingleSwitchProgramEnvironment& env,
    JoyconContext& context,
    VideoOverlaySet& overlays,
    Milliseconds unit
){
    env.log("Retry: Backing out to System menu...", COLOR_YELLOW);
    pbf_press_button(context, BUTTON_B, unit, 500ms);
    context.wait_for_all_requests();
    context.wait_for(500ms);

    VideoSnapshot snapshot = env.console.video().snapshot();
    if (!snapshot){
        env.log("No video during retry.", COLOR_RED);
        return false;
    }

    // Check for System Update at top
    ImageFloatBox system_update_box(0.37, 0.19, 0.16, 0.09);
    overlays.add(COLOR_BLUE, system_update_box);

    std::string top_lower = read_box_text(snapshot, system_update_box);
    env.log("Retry: Top entry OCR (lowercased): \"" + top_lower + "\"");

    bool has_system_word = top_lower.find("system") != std::string::npos;
    bool has_update_word = top_lower.find("update") != std::string::npos;

    if (!has_system_word || !has_update_word){
        env.log("Retry: Top entry does NOT look like 'System Update'. Sending 18 UP inputs.", COLOR_RED);
        for (int i = 0; i < 18; i++){
            pbf_move_joystick(context, 128, 0, unit, unit);
        }
        context.wait_for_all_requests();
        context.wait_for(500ms);
        snapshot = env.console.video().snapshot();
    }

    // Scroll down to Date and Time
    env.log("Retry: Scrolling down to find 'Date and Time'...", COLOR_BLUE);
    pbf_move_joystick(context, 128, 255, unit, unit);
    pbf_move_joystick(context, 128, 255, unit, unit);
    context.wait_for_all_requests();
    pbf_move_joystick(context, 128, 255, 525ms, unit);
    pbf_move_joystick(context, 128, 255, unit, unit);
    context.wait_for_all_requests();
    context.wait_for(500ms);

    snapshot = env.console.video().snapshot();
    if (!snapshot){
        env.log("No video after retry scroll.", COLOR_RED);
        return false;
    }

    ImageFloatBox date_time_box(0.37, 0.61, 0.15, 0.10);
    overlays.add(COLOR_RED, date_time_box);

    std::string dt_lower = read_box_text(snapshot, date_time_box);
    env.log("Retry: Date/Time candidate OCR (lowercased): \"" + dt_lower + "\"");

    bool has_date = dt_lower.find("date") != std::string::npos;
    bool has_time = dt_lower.find("time") != std::string::npos;

    if (has_date && has_time){
        env.log("Retry: OCR confirmed 'Date and Time'. Pressing A.", COLOR_BLUE);
        pbf_press_button(context, BUTTON_A, unit, 500ms);
        context.wait_for_all_requests();
        return true;
    }else{
        env.log("Retry: Failed to find 'Date and Time' after retry.", COLOR_RED);
        return false;
    }
}

void OverlayTextTester::program(SingleSwitchProgramEnvironment& env, CancellableScope& scope){
    JoyconContext context(scope, env.console.controller<JoyconController>());

    env.log(
        "Overlay Text Tester: assuming you are already in System Settings with "
        "the left nav somewhere near 'System'. This program will move within Settings only.",
        COLOR_BLUE
    );

    context.wait_for_all_requests();
    VideoSnapshot snapshot = env.console.video().snapshot();
    if (!snapshot){
        env.log("No video snapshot available.", COLOR_RED);
        return;
    }

    VideoOverlaySet overlays(env.console.overlay());

    // 1. Scroll left nav down to System, OCR check, then (optionally) press A.
    {
        env.log("Step 1: Scrolling left navigation towards 'System'...", COLOR_BLUE);

        Milliseconds tv = context->timing_variation();
        Milliseconds unit = 100ms + tv;

        // Scroll down a bit on the left nav to converge towards System.
        pbf_move_joystick(context, 128, 255, 2000ms, unit);
        context.wait_for_all_requests();
        context.wait_for(500ms);

        snapshot = env.console.video().snapshot();
        if (!snapshot){
            env.log("No video after scrolling to System.", COLOR_RED);
            return;
        }

        ImageFloatBox system_box(0.09, 0.74, 0.08, 0.08);
        overlays.add(COLOR_RED, system_box);

        std::string system_lower = read_box_text(snapshot, system_box);
        env.log("System candidate OCR (lowercased): \"" + system_lower + "\"");

        bool has_system = system_lower.find("system") != std::string::npos;
        if (has_system){
            env.log("OCR says this looks like 'System'. Pressing A to enter System settings.", COLOR_BLUE);
            pbf_press_button(context, BUTTON_A, unit, 500ms);
            context.wait_for_all_requests();

            // Immediately verify top-right entry text (expected \"System Update\").
            snapshot = env.console.video().snapshot();
            if (!snapshot){
                env.log("No video after entering System settings.", COLOR_RED);
                return;
            }

            ImageFloatBox system_update_box(0.37, 0.19, 0.16, 0.09);
            overlays.add(COLOR_BLUE, system_update_box);

            std::string top_lower = read_box_text(snapshot, system_update_box);
            env.log("Top entry candidate OCR (lowercased): \"" + top_lower + "\"");

            bool has_system_word2 = top_lower.find("system") != std::string::npos;
            bool has_update_word  = top_lower.find("update") != std::string::npos;

            if (!has_system_word2 || !has_update_word){
                env.log(
                    "Top entry does NOT look like 'System Update'. "
                    "Sending 18 rapid UP inputs to reset cursor to the top of the menu.",
                    COLOR_RED
                );

                for (int i = 0; i < 18; i++){
                    pbf_move_joystick(context, 128, 0, unit, unit);
                }
                context.wait_for_all_requests();
                context.wait_for(500ms);

                snapshot = env.console.video().snapshot();
                if (snapshot){
                    std::string after_reset = read_box_text(snapshot, system_update_box);
                    env.log("After reset, top entry OCR (lowercased): \"" + after_reset + "\"");
                }
            }else{
                env.log("Top entry OCR looks like 'System Update'. No reset needed.", COLOR_BLUE);
            }
        }else{
            env.log(
                "OCR does NOT look like 'System'. Skipping A-press on this entry to avoid mis-navigation.",
                COLOR_RED
            );
            return;
        }
    }

    // 2. Within System, scroll to "Date and Time" on the right panel and OCR-check it.
    {
        env.log("Step 2: Scrolling within System to find 'Date and Time'...", COLOR_BLUE);

        Milliseconds tv = context->timing_variation();
        Milliseconds unit = 100ms + tv;

        // These timings are adapted from the existing blind navigation.
        pbf_move_joystick(context, 128, 255, unit, unit);
        pbf_move_joystick(context, 128, 255, unit, unit);
        context.wait_for_all_requests();
        pbf_move_joystick(context, 128, 255, 525ms, unit);
        pbf_move_joystick(context, 128, 255, unit, unit);
        context.wait_for_all_requests();
        context.wait_for(500ms);

        snapshot = env.console.video().snapshot();
        if (!snapshot){
            env.log("No video after scrolling to Date and Time.", COLOR_RED);
            return;
        }

        // Same box we used in the OCR experiments.
        ImageFloatBox date_time_box(0.37, 0.61, 0.15, 0.10);
        overlays.add(COLOR_RED, date_time_box);

        std::string dt_lower = read_box_text(snapshot, date_time_box);
        env.log("Date/Time candidate OCR (lowercased): \"" + dt_lower + "\"");

        bool has_date = dt_lower.find("date") != std::string::npos;
        bool has_time = dt_lower.find("time") != std::string::npos;

        if (has_date && has_time){
            env.log(
                "OCR says this looks like 'Date and Time'. Pressing A to enter Date and Time menu.",
                COLOR_BLUE
            );
            pbf_press_button(context, BUTTON_A, unit, 500ms);
            context.wait_for_all_requests();
        }else{
            env.log(
                "OCR did NOT clearly find both 'date' and 'time'. "
                "In a real program we would NOT press A here.",
                COLOR_RED
            );
            return;
        }
    }

    // 3. Sanity checks within Date and Time menu
    {
        env.log("Step 3: Performing sanity checks in Date and Time menu...", COLOR_BLUE);

        Milliseconds tv = context->timing_variation();
        Milliseconds unit = 100ms + tv;

        // Check 1: Verify menu title is "Date and Time"
        {
            context.wait_for_all_requests();
            context.wait_for(500ms);
            snapshot = env.console.video().snapshot();
            if (!snapshot){
                env.log("No video for menu title check.", COLOR_RED);
                return;
            }

            ImageFloatBox menu_title_box(0.05, 0.03, 0.20, 0.10);
            overlays.add(COLOR_GREEN, menu_title_box);

            std::string title_lower = read_box_text(snapshot, menu_title_box);
            env.log("Menu title OCR (lowercased): \"" + title_lower + "\"");

            bool has_date_title = title_lower.find("date") != std::string::npos;
            bool has_time_title = title_lower.find("time") != std::string::npos;

            if (!has_date_title || !has_time_title){
                env.log("Menu title check FAILED. Retrying navigation...", COLOR_RED);
                if (!retry_navigate_to_date_time(env, context, overlays, unit)){
                    env.log("Retry failed. Aborting.", COLOR_RED);
                    return;
                }
                // Re-check after retry
                context.wait_for_all_requests();
                context.wait_for(500ms);
                snapshot = env.console.video().snapshot();
                if (!snapshot){
                    env.log("No video after retry.", COLOR_RED);
                    return;
                }
                title_lower = read_box_text(snapshot, menu_title_box);
                has_date_title = title_lower.find("date") != std::string::npos;
                has_time_title = title_lower.find("time") != std::string::npos;
                if (!has_date_title || !has_time_title){
                    env.log("Menu title still wrong after retry. Aborting.", COLOR_RED);
                    return;
                }
            }
            env.log("Menu title check PASSED.", COLOR_BLUE);
        }

        // Check 2: Verify "Sync Clock Using the Internet" text
        {
            snapshot = env.console.video().snapshot();
            if (!snapshot){
                env.log("No video for sync clock text check.", COLOR_RED);
                return;
            }

            ImageFloatBox sync_text_box(0.17, 0.19, 0.45, 0.10);
            overlays.add(COLOR_MAGENTA, sync_text_box);

            std::string sync_lower = read_box_text(snapshot, sync_text_box);
            env.log("Sync clock text OCR (lowercased): \"" + sync_lower + "\"");

            bool has_sync = sync_lower.find("sync") != std::string::npos;
            bool has_clock = sync_lower.find("clock") != std::string::npos;
            bool has_internet = sync_lower.find("internet") != std::string::npos;

            if (!has_sync || !has_clock || !has_internet){
                env.log("Sync clock text check FAILED. Retrying navigation...", COLOR_RED);
                if (!retry_navigate_to_date_time(env, context, overlays, unit)){
                    env.log("Retry failed. Aborting.", COLOR_RED);
                    return;
                }
                // Re-check after retry
                context.wait_for_all_requests();
                context.wait_for(500ms);
                snapshot = env.console.video().snapshot();
                if (!snapshot){
                    env.log("No video after retry.", COLOR_RED);
                    return;
                }
                sync_lower = read_box_text(snapshot, sync_text_box);
                has_sync = sync_lower.find("sync") != std::string::npos;
                has_clock = sync_lower.find("clock") != std::string::npos;
                has_internet = sync_lower.find("internet") != std::string::npos;
                if (!has_sync || !has_clock || !has_internet){
                    env.log("Sync clock text still wrong after retry. Aborting.", COLOR_RED);
                    return;
                }
            }
            env.log("Sync clock text check PASSED.", COLOR_BLUE);
        }

        // Check 3: Check toggle state and toggle OFF if needed
        {
            snapshot = env.console.video().snapshot();
            if (!snapshot){
                env.log("No video for toggle check.", COLOR_RED);
                return;
            }

            ImageFloatBox toggle_box(0.77, 0.20, 0.05, 0.05);
            overlays.add(COLOR_ORANGE, toggle_box);

            ImageStats toggle_stats = image_stats(extract_box_reference(snapshot, toggle_box));
            
            // Same logic as DateSpam.cpp: "On" is cyan/teal, "Off" is white
            bool is_cyan = (toggle_stats.average.g > toggle_stats.average.r + 5) &&
                          (toggle_stats.average.b >= toggle_stats.average.r);
            bool is_white = (std::abs(toggle_stats.average.r - toggle_stats.average.g) < 10) &&
                           (std::abs(toggle_stats.average.r - toggle_stats.average.b) < 10) &&
                           (std::abs(toggle_stats.average.g - toggle_stats.average.b) < 10);
            
            bool toggle_appears_on = is_cyan && !is_white;

            env.log(
                "Toggle RGB: [" + std::to_string((int)toggle_stats.average.r) + ", " +
                std::to_string((int)toggle_stats.average.g) + ", " +
                std::to_string((int)toggle_stats.average.b) + "]  cyan=" +
                std::string(is_cyan ? "YES" : "NO") + "  white=" +
                std::string(is_white ? "YES" : "NO") + "  appears_ON=" +
                std::string(toggle_appears_on ? "YES" : "NO")
            );

            if (toggle_appears_on){
                env.log("Toggle is ON (cyan detected, not white). Pressing A to toggle OFF...", COLOR_YELLOW);
                pbf_press_button(context, BUTTON_A, unit, 600ms);
                context.wait_for_all_requests();
                context.wait_for(500ms);
            }else{
                env.log("Toggle is already OFF (white or not cyan). No action needed.", COLOR_BLUE);
            }
        }

        // Check 4: Scroll down twice, press A, verify "Current Date and Time"
        {
            env.log("Scrolling down twice and entering date change menu...", COLOR_BLUE);
            pbf_move_joystick(context, 128, 255, unit, unit);
            context.wait_for_all_requests();
            pbf_move_joystick(context, 128, 255, unit, unit);
            context.wait_for_all_requests();
            pbf_press_button(context, BUTTON_A, unit, 500ms);
            context.wait_for_all_requests();
            context.wait_for(500ms);

            snapshot = env.console.video().snapshot();
            if (!snapshot){
                env.log("No video for final check.", COLOR_RED);
                return;
            }

            ImageFloatBox current_dt_box(0.01, 0.01, 0.32, 0.10);
            overlays.add(COLOR_PURPLE, current_dt_box);

            std::string current_dt_lower = read_box_text(snapshot, current_dt_box);
            env.log("Final check OCR (lowercased): \"" + current_dt_lower + "\"");

            bool has_current = current_dt_lower.find("current") != std::string::npos;
            bool has_date_final = current_dt_lower.find("date") != std::string::npos;
            bool has_time_final = current_dt_lower.find("time") != std::string::npos;
            bool has_zone = current_dt_lower.find("zone") != std::string::npos;

            if (!has_current || !has_date_final || !has_time_final || has_zone){
                if (has_zone){
                    env.log("Final check FAILED: Detected 'Time Zone' instead of 'Current Date and Time'. Retrying navigation...", COLOR_RED);
                }else{
                    env.log("Final check FAILED: Missing required words. Retrying navigation...", COLOR_RED);
                }
                env.log("Final check FAILED. Retrying navigation...", COLOR_RED);
                // Back out twice (from date change menu, then from Date and Time menu)
                pbf_press_button(context, BUTTON_B, unit, 500ms);
                context.wait_for_all_requests();
                if (!retry_navigate_to_date_time(env, context, overlays, unit)){
                    env.log("Retry failed. Aborting.", COLOR_RED);
                    return;
                }
                // Try the scroll + A again
                pbf_move_joystick(context, 128, 255, unit, unit);
                context.wait_for_all_requests();
                pbf_move_joystick(context, 128, 255, unit, unit);
                context.wait_for_all_requests();
                pbf_press_button(context, BUTTON_A, unit, 500ms);
                context.wait_for_all_requests();
                context.wait_for(500ms);

                snapshot = env.console.video().snapshot();
                if (snapshot){
                    current_dt_lower = read_box_text(snapshot, current_dt_box);
                    has_current = current_dt_lower.find("current") != std::string::npos;
                    has_date_final = current_dt_lower.find("date") != std::string::npos;
                    has_time_final = current_dt_lower.find("time") != std::string::npos;
                    has_zone = current_dt_lower.find("zone") != std::string::npos;
                    if (!has_current || !has_date_final || !has_time_final || has_zone){
                        if (has_zone){
                            env.log("Final check still failed after retry: Detected 'Time Zone'. Aborting.", COLOR_RED);
                        }else{
                            env.log("Final check still failed after retry: Missing required words. Aborting.", COLOR_RED);
                        }
                        return;
                    }
                }
            }
            env.log("SUCCESS: All checks passed! Ready for date change script.", COLOR_GREEN);
        }
    }

    env.log("Overlay Text Tester: finished all sanity checks successfully.", COLOR_BLUE);
    std::cout << "Overlay Text Tester finished." << std::endl;
}




}
}
}
