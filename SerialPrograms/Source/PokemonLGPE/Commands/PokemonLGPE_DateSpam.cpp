/*  Auto Host Routines
 * 
 *  From: https://github.com/PokemonAutomation/
 * 
 */

#include <algorithm>
#include <cctype>

#include "Common/Cpp/Color.h"
#include "Common/Cpp/Time.h"
#include "CommonFramework/Exceptions/OperationFailedException.h"
#include "CommonFramework/ImageTools/ImageBoxes.h"
#include "CommonFramework/ImageTools/ImageStats.h"
#include "CommonFramework/ImageTypes/ImageViewRGB32.h"
#include "CommonFramework/VideoPipeline/VideoFeed.h"
#include "CommonFramework/VideoPipeline/VideoOverlayScopes.h"
#include "CommonFramework/Language.h"
#include "CommonTools/Async/InferenceRoutines.h"
#include "CommonTools/OCR/OCR_RawOCR.h"
#include "CommonTools/Images/ImageFilter.h"
#include "Controllers/SerialPABotBase/Connection/MessageConverter.h"
#include "NintendoSwitch/Commands/NintendoSwitch_Commands_PushButtons.h"
#include "NintendoSwitch/Commands/NintendoSwitch_Commands_Superscalar.h"
#include "NintendoSwitch/Inference/NintendoSwitch_SelectedSettingDetector.h"
#include "NintendoSwitch/Programs/NintendoSwitch_GameEntry.h"
#include "NintendoSwitch/Programs/DateSpam/NintendoSwitch_HomeToDateTime.h"
#include "NintendoSwitch/Inference/NintendoSwitch_ConsoleTypeDetector.h"
#include "PokemonLGPE_DateSpam.h"

namespace PokemonAutomation{
namespace NintendoSwitch{
namespace PokemonLGPE{

// Helper: read and normalize text from a float box.
static std::string read_box_text_ocr(VideoSnapshot& snapshot, const ImageFloatBox& box){
    ImageViewRGB32 image = snapshot;
    ImageViewRGB32 region = extract_box_reference(image, box);
    
    // Try multiple filters to improve OCR accuracy (black and white text)
    std::vector<std::pair<uint32_t, uint32_t>> filters{
        {0xff000000, 0xff404040},  // Black text filter
        {0xff000000, 0xff606060},  // Dark gray text filter
        {0xff808080, 0xffffffff},  // White/light text filter
        {0xffa0a0a0, 0xffffffff},  // Light gray to white filter
    };
    
    std::string best_text;
    size_t best_text_length = 0;
    
    for (const auto& filter : filters){
        size_t text_pixels;
        ImageRGB32 processed = to_blackwhite_rgb32_range(
            text_pixels, region,
            false,  // in_range_black = false means range becomes white, rest black
            filter.first, filter.second
        );
        
        // Check if we have a reasonable amount of text pixels (between 2% and 50% of image)
        double text_ratio = 1.0 - (double)text_pixels / (region.width() * region.height());
        if (text_ratio < 0.02 || text_ratio > 0.50){
            continue;  // Skip filters with too little or too much text
        }
        
        std::string text = OCR::ocr_read(Language::English, processed);
        
        // Keep the longest non-empty result
        if (text.length() > best_text_length){
            best_text = text;
            best_text_length = text.length();
        }
    }
    
    // If no filtered result worked, try raw OCR as fallback
    if (best_text.empty()){
        best_text = OCR::ocr_read(Language::English, region);
    }

    std::string cleaned;
    for (char ch : best_text){
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
static bool retry_navigate_to_date_time_internal(
    ConsoleHandle& console,
    JoyconContext& context,
    VideoOverlaySet& overlays,
    Milliseconds unit
){
    console.log("Retry: Backing out to System menu...", COLOR_YELLOW);
    pbf_press_button(context, BUTTON_B, unit, 500ms);
    context.wait_for_all_requests();
    context.wait_for(500ms);

    // Scroll up 18 times to reset to the top of the menu
    console.log("Retry: Scrolling up to reset to top of menu...", COLOR_BLUE);
    for (int i = 0; i < 18; i++){
        pbf_move_joystick(context, 128, 0, unit, unit);
    }
    context.wait_for_all_requests();
    context.wait_for(500ms);

    // Verify we're at the top by checking for "System Update"
    VideoSnapshot snapshot = console.video().snapshot();
    if (!snapshot){
        console.log("No video during retry System Update check.", COLOR_RED);
        return false;
    }

    ImageFloatBox system_update_box(0.37, 0.19, 0.16, 0.09);
    overlays.add(COLOR_BLUE, system_update_box);

    std::string top_lower = read_box_text_ocr(snapshot, system_update_box);
    console.log("Retry: Top entry OCR (lowercased): \"" + top_lower + "\"");

    bool has_system_word = top_lower.find("system") != std::string::npos;
    bool has_update_word = top_lower.find("update") != std::string::npos;

    if (!has_system_word || !has_update_word){
        console.log("Retry: Top entry does NOT look like 'System Update' after scroll up. Retry may have failed.", COLOR_RED);
        // Continue anyway - better to try than fail completely
    }else{
        console.log("Retry: Verified 'System Update' at top. Proceeding...", COLOR_BLUE);
    }

    // Scroll down to Date and Time
    console.log("Retry: Scrolling down to find 'Date and Time'...", COLOR_BLUE);
    pbf_move_joystick(context, 128, 255, unit, unit);
    pbf_move_joystick(context, 128, 255, unit, unit);
    context.wait_for_all_requests();
    pbf_move_joystick(context, 128, 255, 525ms, unit);
    pbf_move_joystick(context, 128, 255, unit, unit);
    context.wait_for_all_requests();
    context.wait_for(500ms);

    console.log("Retry: Pressing A to enter Date and Time menu.", COLOR_BLUE);
    pbf_press_button(context, BUTTON_A, unit, 500ms);
    context.wait_for_all_requests();
    return true;
}

void home_to_settings_only(ConsoleHandle& console, JoyconContext& context){
    console.log("Navigating from home screen to System Settings only...", COLOR_BLUE);
    
    // Detect console type
    ConsoleTypeDetector_Home detector(console);
    VideoSnapshot snapshot = console.video().snapshot();
    ConsoleType console_type = detector.detect_only(snapshot);
    
    Milliseconds tv = context->timing_variation();
    Milliseconds unit = 100ms + tv;
    
    switch (console_type){
    case ConsoleType::Switch1:{
        // Switch1 navigation: Right 3x, Down 2x, Left 1x, A 3x
        pbf_move_joystick(context, 255, 128, unit, unit);  // Right
        pbf_move_joystick(context, 255, 128, unit, unit);  // Right
        pbf_move_joystick(context, 255, 128, unit, unit);  // Right
        
        pbf_move_joystick(context, 128, 255, unit, unit);  // Down
        pbf_move_joystick(context, 128, 255, unit, unit);  // Down
        
        pbf_move_joystick(context, 0, 128, unit, unit);    // Left
        
        // Press A multiple times to enter System Settings
        pbf_press_button(context, BUTTON_A, unit, unit);
        pbf_press_button(context, BUTTON_A, unit, unit);
        pbf_press_button(context, BUTTON_A, unit, unit);
        
        context.wait_for_all_requests();
        break;
    }
    case ConsoleType::Switch2_Unknown:
    case ConsoleType::Switch2_FW19_International:
    case ConsoleType::Switch2_FW19_JapanLocked:
    case ConsoleType::Switch2_FW20_International:
    case ConsoleType::Switch2_FW20_JapanLocked:{
        // Switch2 navigation: Right 3x, Down 2x, Left 1x, A 3x
        // Stop immediately after entering System Settings - OCR will take over from here
        Milliseconds unit2 = 24ms + tv;
        
        pbf_move_joystick(context, 255, 128, 2*unit2, unit2);  // Right
        pbf_move_joystick(context, 255, 128, 2*unit2, unit2);  // Right
        pbf_move_joystick(context, 255, 128, 2*unit2, unit2);  // Right
        
        pbf_move_joystick(context, 128, 255, 2*unit2, unit2);  // Down
        pbf_move_joystick(context, 128, 255, 2*unit2, unit2);  // Down
        
        pbf_move_joystick(context, 0, 128, 2*unit2, unit2);    // Left
        
        // Press A multiple times to enter System Settings
        pbf_press_button(context, BUTTON_A, 2*unit2, unit2);
        pbf_press_button(context, BUTTON_A, 2*unit2, unit2);
        pbf_press_button(context, BUTTON_A, 2*unit2, unit2);
        
        // Stop here - OCR navigation will take over
        context.wait_for_all_requests();
        break;
    }
    default:
        throw UserSetupError(
            console.logger(),
            "Unsupported console type for home_to_settings_only: " + ConsoleType_strings(console_type)
        );
    }
    
    console.log("Successfully navigated to System Settings.", COLOR_BLUE);
}

bool navigate_to_date_change_with_ocr(ConsoleHandle& console, JoyconContext& context){
    console.log("Starting OCR-guided navigation from System Settings to date change menu...", COLOR_BLUE);

    context.wait_for_all_requests();
    context.wait_for(400ms);

    Milliseconds tv = context->timing_variation();
    Milliseconds unit = 100ms + tv;

    VideoSnapshot snapshot = console.video().snapshot();
    if (!snapshot){
        console.log("No video snapshot available.", COLOR_RED);
        return false;
    }

    VideoOverlaySet overlays(console.overlay());

    // Step 1: Scroll down in left menu to find "System", then press A to enter System submenu
    {
        console.log("Step 1: Scrolling down in left menu to find 'System'...", COLOR_BLUE);
        // Scroll down all the way in the left navigation menu to reach "System"
        pbf_move_joystick(context, 128, 255, 2500ms, unit);
        context.wait_for_all_requests();
        context.wait_for(500ms);
        
        console.log("Step 1: Pressing A to enter System submenu...", COLOR_BLUE);
        pbf_press_button(context, BUTTON_A, unit, 500ms);
        context.wait_for_all_requests();
        context.wait_for(500ms);
    }

    // Step 2: Within System, scroll to "Date and Time" on the right panel and press A.
    {
        console.log("Step 2: Scrolling within System to find 'Date and Time'...", COLOR_BLUE);

        // These timings are adapted from the existing blind navigation.
        pbf_move_joystick(context, 128, 255, unit, unit);
        pbf_move_joystick(context, 128, 255, unit, unit);
        context.wait_for_all_requests();
        pbf_move_joystick(context, 128, 255, 525ms, unit);
        pbf_move_joystick(context, 128, 255, unit, unit);
        context.wait_for_all_requests();
        context.wait_for(500ms);

        console.log("Pressing A to enter Date and Time menu.", COLOR_BLUE);
        pbf_press_button(context, BUTTON_A, unit, 500ms);
        context.wait_for_all_requests();
        context.wait_for(500ms);
    }

    // Step 3: Sanity checks within Date and Time menu
    {
        console.log("Step 3: Performing sanity checks in Date and Time menu...", COLOR_BLUE);

        // Check 1: Verify menu title is "Date and Time"
        {
            context.wait_for_all_requests();
            context.wait_for(500ms);
            snapshot = console.video().snapshot();
            if (!snapshot){
                console.log("No video for menu title check.", COLOR_RED);
                return false;
            }

            ImageFloatBox menu_title_box(0.05, 0.03, 0.20, 0.10);
            overlays.add(COLOR_GREEN, menu_title_box);

            std::string title_lower = read_box_text_ocr(snapshot, menu_title_box);
            console.log("Menu title OCR (lowercased): \"" + title_lower + "\"");

            bool has_date_title = title_lower.find("date") != std::string::npos;
            bool has_time_title = title_lower.find("time") != std::string::npos;

            if (!has_date_title || !has_time_title){
                console.log("Menu title check FAILED. Retrying navigation...", COLOR_RED);
                if (!retry_navigate_to_date_time_internal(console, context, overlays, unit)){
                    console.log("Retry failed. Aborting.", COLOR_RED);
                    return false;
                }
                // Re-check after retry
                context.wait_for_all_requests();
                context.wait_for(500ms);
                snapshot = console.video().snapshot();
                if (!snapshot){
                    console.log("No video after retry.", COLOR_RED);
                    return false;
                }
                title_lower = read_box_text_ocr(snapshot, menu_title_box);
                has_date_title = title_lower.find("date") != std::string::npos;
                has_time_title = title_lower.find("time") != std::string::npos;
                if (!has_date_title || !has_time_title){
                    console.log("Menu title still wrong after retry. Aborting.", COLOR_RED);
                    return false;
                }
            }
            console.log("Menu title check PASSED.", COLOR_BLUE);
        }

        // Check 2: Verify "Sync Clock Using the Internet" text
        {
            snapshot = console.video().snapshot();
            if (!snapshot){
                console.log("No video for sync clock text check.", COLOR_RED);
                return false;
            }

            ImageFloatBox sync_text_box(0.17, 0.19, 0.45, 0.10);
            overlays.add(COLOR_MAGENTA, sync_text_box);

            std::string sync_lower = read_box_text_ocr(snapshot, sync_text_box);
            console.log("Sync clock text OCR (lowercased): \"" + sync_lower + "\"");

            bool has_sync = sync_lower.find("sync") != std::string::npos;
            bool has_clock = sync_lower.find("clock") != std::string::npos;
            bool has_internet = sync_lower.find("internet") != std::string::npos;

            if (!has_sync || !has_clock || !has_internet){
                console.log("Sync clock text check FAILED. Retrying navigation...", COLOR_RED);
                if (!retry_navigate_to_date_time_internal(console, context, overlays, unit)){
                    console.log("Retry failed. Aborting.", COLOR_RED);
                    return false;
                }
                // Re-check after retry
                context.wait_for_all_requests();
                context.wait_for(500ms);
                snapshot = console.video().snapshot();
                if (!snapshot){
                    console.log("No video after retry.", COLOR_RED);
                    return false;
                }
                sync_lower = read_box_text_ocr(snapshot, sync_text_box);
                has_sync = sync_lower.find("sync") != std::string::npos;
                has_clock = sync_lower.find("clock") != std::string::npos;
                has_internet = sync_lower.find("internet") != std::string::npos;
                if (!has_sync || !has_clock || !has_internet){
                    console.log("Sync clock text still wrong after retry. Aborting.", COLOR_RED);
                    return false;
                }
            }
            console.log("Sync clock text check PASSED.", COLOR_BLUE);
        }

        // Check 3: Check toggle state and toggle OFF if needed
        {
            snapshot = console.video().snapshot();
            if (!snapshot){
                console.log("No video for toggle check.", COLOR_RED);
                return false;
            }

            ImageFloatBox toggle_box(0.77, 0.20, 0.05, 0.05);
            overlays.add(COLOR_ORANGE, toggle_box);

            ImageViewRGB32 image = snapshot;
            ImageStats toggle_stats = image_stats(extract_box_reference(image, toggle_box));
            
            // Same logic as DateSpam.cpp: "On" is cyan/teal, "Off" is white
            bool is_cyan = (toggle_stats.average.g > toggle_stats.average.r + 5) &&
                          (toggle_stats.average.b >= toggle_stats.average.r);
            bool is_white = (std::abs(toggle_stats.average.r - toggle_stats.average.g) < 10) &&
                           (std::abs(toggle_stats.average.r - toggle_stats.average.b) < 10) &&
                           (std::abs(toggle_stats.average.g - toggle_stats.average.b) < 10);
            
            bool toggle_appears_on = is_cyan && !is_white;

            console.log(
                "Toggle RGB: [" + std::to_string((int)toggle_stats.average.r) + ", " +
                std::to_string((int)toggle_stats.average.g) + ", " +
                std::to_string((int)toggle_stats.average.b) + "]  cyan=" +
                std::string(is_cyan ? "YES" : "NO") + "  white=" +
                std::string(is_white ? "YES" : "NO") + "  appears_ON=" +
                std::string(toggle_appears_on ? "YES" : "NO")
            );

            if (toggle_appears_on){
                console.log("Toggle is ON (cyan detected, not white). Pressing A to toggle OFF...", COLOR_YELLOW);
                pbf_press_button(context, BUTTON_A, unit, 600ms);
                context.wait_for_all_requests();
                context.wait_for(500ms);
            }else{
                console.log("Toggle is already OFF (white or not cyan). No action needed.", COLOR_BLUE);
            }
        }

        // Check 4: Scroll down twice, press A, verify "Current Date and Time"
        {
            console.log("Scrolling down twice and entering date change menu...", COLOR_BLUE);
            pbf_move_joystick(context, 128, 255, unit, unit);
            context.wait_for_all_requests();
            pbf_move_joystick(context, 128, 255, unit, unit);
            context.wait_for_all_requests();
            // Use default timing for entering date change menu (no extra delays)
            pbf_press_button(context, BUTTON_A, unit, unit);
            context.wait_for_all_requests();
            // Minimal wait for OCR verification only
            context.wait_for(200ms);

            snapshot = console.video().snapshot();
            if (!snapshot){
                console.log("No video for final check.", COLOR_RED);
                return false;
            }

            ImageFloatBox current_dt_box(0.01, 0.01, 0.32, 0.10);
            overlays.add(COLOR_PURPLE, current_dt_box);

            std::string current_dt_lower = read_box_text_ocr(snapshot, current_dt_box);
            console.log("Final check OCR (lowercased): \"" + current_dt_lower + "\"");

            bool has_date_final = current_dt_lower.find("date") != std::string::npos;
            bool has_time_final = current_dt_lower.find("time") != std::string::npos;
            bool has_zone = current_dt_lower.find("zone") != std::string::npos;

            if (!has_date_final || !has_time_final || has_zone){
                if (has_zone){
                    console.log("Final check FAILED: Detected 'Time Zone' instead of 'Date and Time'. Retrying navigation...", COLOR_RED);
                }else{
                    console.log("Final check FAILED: Missing required words. Retrying navigation...", COLOR_RED);
                }
                // Back out twice (from date change menu, then from Date and Time menu)
                pbf_press_button(context, BUTTON_B, unit, 500ms);
                context.wait_for_all_requests();
                if (!retry_navigate_to_date_time_internal(console, context, overlays, unit)){
                    console.log("Retry failed. Aborting.", COLOR_RED);
                    return false;
                }
                // Try the scroll + A again
                pbf_move_joystick(context, 128, 255, unit, unit);
                context.wait_for_all_requests();
                pbf_move_joystick(context, 128, 255, unit, unit);
                context.wait_for_all_requests();
                // Use default timing for entering date change menu (no extra delays)
                pbf_press_button(context, BUTTON_A, unit, unit);
                context.wait_for_all_requests();
                // Minimal wait for OCR verification only
                context.wait_for(200ms);

                snapshot = console.video().snapshot();
                if (snapshot){
                    current_dt_lower = read_box_text_ocr(snapshot, current_dt_box);
                    has_date_final = current_dt_lower.find("date") != std::string::npos;
                    has_time_final = current_dt_lower.find("time") != std::string::npos;
                    has_zone = current_dt_lower.find("zone") != std::string::npos;
                    if (!has_date_final || !has_time_final || has_zone){
                        if (has_zone){
                            console.log("Final check still failed after retry: Detected 'Time Zone'. Aborting.", COLOR_RED);
                        }else{
                            console.log("Final check still failed after retry: Missing required words. Aborting.", COLOR_RED);
                        }
                        return false;
                    }
                }
            }
            console.log("SUCCESS: All OCR checks passed! Ready for date change.", COLOR_GREEN);
        }
    }

    return true;
}

void verify_date_time_menu_selected(ConsoleHandle& console, JoyconContext& context){
    // Comprehensive verification: ensure we're on "Date and Time" menu item AND Sync Clock is OFF
    // Menu structure: "Synchronize Clock via Internet" (top), "Time Zone" (middle), "Date and Time" (bottom)
    // CRITICAL: Must be on "Date and Time" before rolling date, never on "Time Zone" or "Sync Clock"
    
    context.wait_for_all_requests();
    context.wait_for(Milliseconds(400));
    
    Milliseconds tv = context->timing_variation();
    Milliseconds unit = 100ms + tv;
    
    VideoSnapshot snapshot = console.video().snapshot();
    if (!snapshot){
        console.log("WARNING: No video available. Proceeding blind...", COLOR_RED);
        return;
    }

    // PRE-CHECK: Ensure left navigation has "System" selected.
    {
        ImageFloatBox system_label_box(0.09, 0.74, 0.08, 0.08);
        ImageViewRGB32 system_region = extract_box_reference(snapshot, system_label_box);

        std::string system_text = OCR::ocr_read(Language::English, system_region);
        std::string system_clean;
        for (char ch : system_text){
            if (ch != '\r' && ch != '\n'){
                system_clean += ch;
            }
        }
        std::string system_lower = system_clean;
        std::transform(
            system_lower.begin(), system_lower.end(), system_lower.begin(),
            [](unsigned char c){ return (char)std::tolower(c); }
        );

        bool has_system_word = system_lower.find("system") != std::string::npos;

        console.log("System label OCR: \"" + system_clean + "\"");

        if (!has_system_word){
            console.log(
                "System pre-check FAILED: expected 'System' selected. "
                "Not proceeding to date/time manipulation.", COLOR_RED
            );
            return;
        }
    }
    
    // STEP 1: Check and disable "Synchronize Clock via Internet" if it's ON
    console.log("Step 1: Checking 'Synchronize Clock via Internet' status...");
    
    // Add visual overlay boxes so you can see where we're looking
    VideoOverlaySet overlays(console.overlay());
    
    // Based on your screenshot: menu items are in the lower half of the screen
    // "Synchronize Clock via Internet" is the first item, with "On"/"Off" status on the right
    // "Time Zone" shows "Denver" on the right around y=0.70
    // So "Synchronize Clock" status should be above that, around y=0.55-0.65
    
    ImageFloatBox sync_status_box1(0.78, 0.21, 0.05, 0.05);  // Try y=0.50
    
    overlays.add(COLOR_RED, sync_status_box1);
    
    ImageStats sync_status1 = image_stats(extract_box_reference(snapshot, sync_status_box1));
    
    // Debug: Log all positions
    console.log("Box1 (RED, y:0.21) RGB: [" + std::to_string((int)sync_status1.average.r) + ", " + 
                std::to_string((int)sync_status1.average.g) + ", " + 
                std::to_string((int)sync_status1.average.b) + "] sum=" + 
                std::to_string((int)sync_status1.average.sum()));
    console.log("LOOK AT THE COLORED BOXES ON SCREEN - which one covers the 'On' or 'Off' text?");
    
    // Use the detected values
    ImageStats sync_status = sync_status1;
    
    // "On" is cyan/teal text: green and blue are higher than red
    // "Off" is white text: all RGB components are similar and high
    // The cyan "On" text shows as [43, 56, 53] - green and blue slightly higher than red
    // Detection: if G > R+5 AND B > R, it's likely cyan "On"
    // If all components are similar (within 10 of each other), it's white "Off"
    
    bool is_cyan = (sync_status.average.g > sync_status.average.r + 5) && 
                   (sync_status.average.b >= sync_status.average.r);
    bool is_white = (std::abs(sync_status.average.r - sync_status.average.g) < 10) &&
                    (std::abs(sync_status.average.r - sync_status.average.b) < 10) &&
                    (std::abs(sync_status.average.g - sync_status.average.b) < 10);
    
    bool sync_appears_on = is_cyan && !is_white;
    
    console.log("Is cyan: " + std::string(is_cyan ? "YES" : "NO") + 
                ", Is white: " + std::string(is_white ? "YES" : "NO") +
                ", Appears ON: " + std::string(sync_appears_on ? "YES" : "NO"));
    
    if (sync_appears_on){
        console.log("DETECTED: 'Synchronize Clock via Internet' is ON. Preparing to disable it with OCR safety check...", COLOR_YELLOW);
        
        // Strategy: First scroll down to bottom (Date and Time), then scroll up exactly 2 times to Sync Clock
        // This ensures we know exactly where we are
        console.log("Navigating to bottom of menu first...");
        for (int i = 0; i < 5; i++){
            pbf_move_joystick(context, 128, 255, unit, unit);  // Down - go to bottom
            context.wait_for_all_requests();
            context.wait_for(Milliseconds(100));
        }
        
        console.log("Now navigating UP towards 'Synchronize Clock via Internet' with OCR verification...");
        // From "Date and Time" (bottom), go up 2 times to reach "Synchronize Clock" (top)
        for (int step = 0; step < 2; step++){
            pbf_move_joystick(context, 128, 0, unit, unit);  // Up
            context.wait_for_all_requests();
            context.wait_for(Milliseconds(300));

            // After each move, OCR the menu label in the calibrated box and confirm.
            VideoSnapshot step_snapshot = console.video().snapshot();
            if (!step_snapshot){
                console.log("WARNING: No video during OCR step navigation. Skipping A-press safeguard this step.", COLOR_RED);
                continue;
            }

            // This box matches the dev tester coordinates over the menu text.
            ImageFloatBox menu_label_box(0.38, 0.59, 0.14, 0.06);
            ImageViewRGB32 menu_region = extract_box_reference(step_snapshot, menu_label_box);
            std::string ocr_text = OCR::ocr_read(Language::English, menu_region);

            std::string cleaned;
            for (char ch : ocr_text){
                if (ch != '\r' && ch != '\n'){
                    cleaned += ch;
                }
            }

            std::string lowered = cleaned;
            std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c){ return (char)std::tolower(c); });

            console.log("OCR menu text after scroll step " + std::to_string(step + 1) + ": \"" + cleaned + "\"");

            bool looks_like_sync_option =
                lowered.find("synchron") != std::string::npos ||
                lowered.find("sync") != std::string::npos ||
                lowered.find("internet") != std::string::npos;

            if (step == 1){
                // On the final step, we require a positive OCR match before pressing A.
                if (!looks_like_sync_option){
                    console.log("OCR safeguard: expected 'Synchronize Clock via Internet' but saw \"" + cleaned + "\". NOT pressing A.", COLOR_RED);
                    // Bail out early instead of risking a wrong press.
                    return;
                }
            }
        }
        
        // At this point OCR believes we're on the correct menu item.
        console.log("OCR confirmed we're on 'Synchronize Clock via Internet'. Pressing A to toggle OFF...");
        pbf_press_button(context, BUTTON_A, unit, unit);
        context.wait_for_all_requests();
        context.wait_for(Milliseconds(600));  // Wait for toggle animation
        
        console.log("Toggled 'Synchronize Clock via Internet' OFF. Now navigating back to 'Date and Time'...");
        
        // Navigate back down to "Date and Time" (2 times down)
        pbf_move_joystick(context, 128, 255, unit, unit);  // Down to Time Zone
        context.wait_for_all_requests();
        context.wait_for(Milliseconds(250));
        pbf_move_joystick(context, 128, 255, unit, unit);  // Down to Date and Time
        context.wait_for_all_requests();
        context.wait_for(Milliseconds(400));
        
        console.log("Back on 'Date and Time' menu item.");
    } else {
        console.log("'Synchronize Clock via Internet' is OFF (or detection failed - check RGB values above).");
    }
    
    // STEP 2: Ensure we're on "Date and Time" menu item (bottom option)
    console.log("Step 2: Ensuring 'Date and Time' menu item is selected...");
    
    // After handling Sync Clock, we should be on "Date and Time" already if we navigated correctly
    // But let's verify and fix if needed
    
    // Simple approach: Just scroll down multiple times to guarantee we're at the bottom
    // The menu will stop at "Date and Time" (bottom item) even if we scroll too much
    console.log("Scrolling to bottom to ensure 'Date and Time' is selected...");
    for (int i = 0; i < 5; i++){
        pbf_move_joystick(context, 128, 255, unit, unit);  // Down
        context.wait_for_all_requests();
        context.wait_for(Milliseconds(150));
    }
    
    context.wait_for_all_requests();
    context.wait_for(Milliseconds(400));
    
    // Final verification with visual boxes to see where menu items actually are
    snapshot = console.video().snapshot();
    if (snapshot){
        // OCR check that the highlighted right-side menu item is "Date and Time".
        {
            ImageFloatBox date_time_label_box(0.38, 0.59, 0.14, 0.06);
            ImageViewRGB32 dt_region = extract_box_reference(snapshot, date_time_label_box);

            std::string dt_text = OCR::ocr_read(Language::English, dt_region);
            std::string dt_clean;
            for (char ch : dt_text){
                if (ch != '\r' && ch != '\n'){
                    dt_clean += ch;
                }
            }

            std::string dt_lower = dt_clean;
            std::transform(
                dt_lower.begin(), dt_lower.end(), dt_lower.begin(),
                [](unsigned char c){ return (char)std::tolower(c); }
            );

            bool has_date = dt_lower.find("date") != std::string::npos;
            bool has_time = dt_lower.find("time") != std::string::npos;

            console.log("Date/Time label OCR: \"" + dt_clean + "\"");

            if (!has_date || !has_time){
                console.log(
                    "Date/Time pre-check FAILED: expected 'Date and Time' highlighted. "
                    "Not proceeding to roll date.", COLOR_RED
                );
                return;
            }
        }

        // Show boxes at different Y positions to find where menu items are
        // These boxes cover the LEFT side where menu item TEXT appears (not status text)
        ImageFloatBox menu_check_box1(0.15, 0.15, 0.40, 0.05);  // Very top
        ImageFloatBox menu_check_box2(0.15, 0.25, 0.40, 0.05);  // Upper area
        ImageFloatBox menu_check_box3(0.15, 0.35, 0.40, 0.05);  // Middle area
        ImageFloatBox menu_check_box4(0.15, 0.45, 0.40, 0.05);  // Lower-middle area
        ImageFloatBox menu_check_box5(0.15, 0.55, 0.40, 0.05);  // Lower area
        
        VideoOverlaySet menu_overlays(console.overlay());
        menu_overlays.add(COLOR_CYAN, menu_check_box1);
        menu_overlays.add(COLOR_MAGENTA, menu_check_box2);
        menu_overlays.add(COLOR_GREEN, menu_check_box3);
        menu_overlays.add(COLOR_ORANGE, menu_check_box4);
        menu_overlays.add(COLOR_PURPLE, menu_check_box5);
        
        ImageStats menu_stats1 = image_stats(extract_box_reference(snapshot, menu_check_box1));
        ImageStats menu_stats2 = image_stats(extract_box_reference(snapshot, menu_check_box2));
        ImageStats menu_stats3 = image_stats(extract_box_reference(snapshot, menu_check_box3));
        ImageStats menu_stats4 = image_stats(extract_box_reference(snapshot, menu_check_box4));
        ImageStats menu_stats5 = image_stats(extract_box_reference(snapshot, menu_check_box5));
        
        console.log("Menu boxes - CYAN(y:0.15):" + std::to_string((int)menu_stats1.average.sum()) +
                    " MAGENTA(y:0.25):" + std::to_string((int)menu_stats2.average.sum()) +
                    " GREEN(y:0.35):" + std::to_string((int)menu_stats3.average.sum()) +
                    " ORANGE(y:0.45):" + std::to_string((int)menu_stats4.average.sum()) +
                    " PURPLE(y:0.55):" + std::to_string((int)menu_stats5.average.sum()));
        console.log("LOOK: Which colored box covers the HIGHLIGHTED menu item (should have blue border)?");
        console.log("The highlighted item should be 'Date and Time' (bottom option).");
    }
    
    console.log("Ready to roll date. 'Date and Time' should now be selected.");
}

void roll_date_forward_1(JoyconContext& context){
    Milliseconds tv = context->timing_variation();
    //  Slightly slower base unit to make date navigation more forgiving.
    Milliseconds unit = 40ms + tv;

    pbf_move_joystick(context, 128, 0, 2*unit, unit);
    pbf_press_button(context, BUTTON_A, 2*unit, unit);

    pbf_move_joystick(context, 255, 128, 2*unit, unit);
    pbf_move_joystick(context, 128, 0, 2*unit, unit);
    pbf_move_joystick(context, 255, 128, 2*unit, unit);
    pbf_press_button(context, BUTTON_A, 2*unit, unit);
    pbf_move_joystick(context, 255, 128, 2*unit, unit);
    pbf_move_joystick(context, 255, 128, 2*unit, unit);
    pbf_press_button(context, BUTTON_A, 2*unit, unit);
}

void roll_date_backward_N(JoyconContext& context, uint8_t skips){
    if (skips == 0){
        return;
    }

    Milliseconds tv = context->timing_variation();
    //  Slightly slower base unit to make date navigation more forgiving.
    Milliseconds unit = 40ms + tv;


    for (uint8_t c = 0; c < skips - 1; c++){
        pbf_move_joystick(context, 128, 255, 2*unit, unit);
    }

    pbf_press_button(context, BUTTON_A, 2*unit, unit);
    pbf_move_joystick(context, 255, 128, 2*unit, unit);

    for (uint8_t c = 0; c < skips - 1; c++){
        pbf_move_joystick(context, 128, 255, 2*unit, unit);
    }

    pbf_press_button(context, BUTTON_A, 2*unit, unit);
    pbf_move_joystick(context, 255, 128, 2*unit, unit);
    pbf_move_joystick(context, 255, 128, 2*unit, unit);
    pbf_press_button(context, BUTTON_A, 2*unit, unit);
    pbf_press_button(context, BUTTON_A, 2*unit, unit);
}





}

}
}

