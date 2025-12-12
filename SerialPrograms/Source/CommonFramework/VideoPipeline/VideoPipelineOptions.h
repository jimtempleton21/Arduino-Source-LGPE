/*  Video Pipeline Options
 *
 *  From: https://github.com/PokemonAutomation/
 *
 */

#ifndef PokemonAutomation_VideoPipeline_VideoPipelineOptions_H
#define PokemonAutomation_VideoPipeline_VideoPipelineOptions_H

#include "Common/Cpp/Options/GroupOption.h"
#include "Common/Cpp/Options/BooleanCheckBoxOption.h"
#include "Common/Cpp/Options/SimpleIntegerOption.h"
#include "Common/Cpp/Options/EnumDropdownOption.h"
#include "Backends/CameraImplementations.h"

namespace PokemonAutomation{


enum class VideoRotation{
    ROTATE_0,
    ROTATE_90,
    ROTATE_180,
    ROTATE_NEGATIVE_90,
};

inline EnumDropdownDatabase<VideoRotation> make_VideoRotation_database(){
    return EnumDropdownDatabase<VideoRotation>({
        {VideoRotation::ROTATE_0, "0", "0°"},
        {VideoRotation::ROTATE_90, "90", "90°"},
        {VideoRotation::ROTATE_180, "180", "180°"},
        {VideoRotation::ROTATE_NEGATIVE_90, "-90", "-90°"},
    });
}

inline double video_rotation_to_degrees(VideoRotation rotation){
    switch (rotation){
    case VideoRotation::ROTATE_0:
        return 0.0;
    case VideoRotation::ROTATE_90:
        return 90.0;
    case VideoRotation::ROTATE_180:
        return 180.0;
    case VideoRotation::ROTATE_NEGATIVE_90:
        return -90.0;
    }
    return 0.0;
}


class VideoPipelineOptions : public GroupOption{
public:
    VideoPipelineOptions()
        : GroupOption(
            "Video Pipeline",
            LockMode::LOCK_WHILE_RUNNING,
            GroupOption::EnableMode::ALWAYS_ENABLED, true
        )
#if QT_VERSION_MAJOR == 5
        , ENABLE_FRAME_SCREENSHOTS(
            "<b>Enable Frame Screenshots:</b><br>"
            "Attempt to use QVideoProbe and QVideoFrame for screenshots.",
            LockMode::UNLOCK_WHILE_RUNNING,
            true
        )
#endif
        , AUTO_RESET_SECONDS(
            "<b>Video Auto-Reset:</b><br>"
            "Attempt to reset the video if this many seconds has elapsed since the last video frame (in order to fix issues with RDP disconnection, etc).<br>"
            "This option is not supported by all video frameworks.",
            LockMode::UNLOCK_WHILE_RUNNING,
            5
        )
        , VIDEO_ROTATION(
            "<b>Video Rotation:</b><br>"
            "Rotate the video input display. Useful for fixing orientation issues with broken video cards.",
            make_VideoRotation_database(),
            LockMode::UNLOCK_WHILE_RUNNING,
            VideoRotation::ROTATE_0
        )
    {
        PA_ADD_OPTION(VIDEO_BACKEND);
#if QT_VERSION_MAJOR == 5
        PA_ADD_OPTION(ENABLE_FRAME_SCREENSHOTS);
#endif

        PA_ADD_OPTION(AUTO_RESET_SECONDS);
        PA_ADD_OPTION(VIDEO_ROTATION);
    }

public:
    VideoBackendOption VIDEO_BACKEND;
#if QT_VERSION_MAJOR == 5
    BooleanCheckBoxOption ENABLE_FRAME_SCREENSHOTS;
#endif

    SimpleIntegerOption<uint8_t> AUTO_RESET_SECONDS;
    EnumDropdownOption<VideoRotation> VIDEO_ROTATION;
};



}
#endif
