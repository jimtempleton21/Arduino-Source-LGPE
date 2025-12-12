/*  Video Source Selector Widget
 *
 *  From: https://github.com/PokemonAutomation/
 *
 */

#include <QHBoxLayout>
#include <QLabel>
#include <QComboBox>
#include <QPushButton>
#include "Common/Qt/NoWheelComboBox.h"
#include "CommonFramework/Panels/ConsoleSettingsStretch.h"
#include "CommonFramework/VideoPipeline/Backends/CameraImplementations.h"
#include "CommonFramework/GlobalSettingsPanel.h"
#include "CommonFramework/VideoPipeline/VideoPipelineOptions.h"
#include "VideoSourceSelectorWidget.h"

#include "CommonFramework/VideoPipeline/VideoSources/VideoSource_Null.h"
#include "CommonFramework/VideoPipeline/VideoSources/VideoSource_Camera.h"

//#include <iostream>
//using std::cout;
//using std::endl;

namespace PokemonAutomation{


VideoSourceSelectorWidget::~VideoSourceSelectorWidget(){
    m_session.remove_state_listener(*this);
}
VideoSourceSelectorWidget::VideoSourceSelectorWidget(Logger& logger, VideoSession& session)
    : m_logger(logger)
    , m_session(session)
{
    QHBoxLayout* layout0 = new QHBoxLayout(this);
    layout0->setContentsMargins(0, 0, 0, 0);

    layout0->addWidget(new QLabel("<b>Video Input:</b>", this), CONSOLE_SETTINGS_STRETCH_L0_LABEL);

    QHBoxLayout* layout1 = new QHBoxLayout();
    layout0->addLayout(layout1, CONSOLE_SETTINGS_STRETCH_L0_RIGHT);
    layout1->setContentsMargins(0, 0, 0, 0);

    m_sources_box = new NoWheelComboBox(this);
    m_sources_box->setMaxVisibleItems(20);
    layout1->addWidget(m_sources_box, CONSOLE_SETTINGS_STRETCH_L1_BODY);
    layout1->addSpacing(5);

    m_resolution_box = new NoWheelComboBox(this);
    m_resolution_box->setMaxVisibleItems(20);
    layout1->addWidget(m_resolution_box, CONSOLE_SETTINGS_STRETCH_L1_RIGHT);
    layout1->addSpacing(5);

    m_rotation_box = new NoWheelComboBox(this);
    m_rotation_box->setMaxVisibleItems(10);
    m_rotation_box->addItem("0°");
    m_rotation_box->addItem("90°");
    m_rotation_box->addItem("180°");
    m_rotation_box->addItem("-90°");
    layout1->addWidget(m_rotation_box, 1);
    layout1->addSpacing(5);

    m_reset_button = new QPushButton("Reset Video", this);
    layout1->addWidget(m_reset_button, CONSOLE_SETTINGS_STRETCH_L1_BUTTON);

    update_source_list();
    update_resolution_list();
    update_rotation_list();

    // Set the action for the video source selection box
    connect(
        m_sources_box, static_cast<void(QComboBox::*)(int)>(&QComboBox::activated),
        this, [this](int index){
            if (0 <= index && index < (int)m_sources.size()){
                m_sources[index]->run_post_select();
                m_session.set_source(m_sources[index]);
            }else{
                m_session.set_source(std::make_unique<VideoSourceDescriptor_Null>());
            }
        }
    );

    // Set the action for the video resolution selection box
    connect(
        m_resolution_box, static_cast<void(QComboBox::*)(int)>(&QComboBox::activated),
        this, [this](int index){
            if (index < 0 || index >= (int)m_resolutions.size()){
                return;
            }
            Resolution resolution = m_resolutions[index];
            m_session.set_resolution(resolution);
        }
    );

    // Set the action for the video rotation selection box
    connect(
        m_rotation_box, static_cast<void(QComboBox::*)(int)>(&QComboBox::activated),
        this, [](int index){
            VideoRotation rotation = VideoRotation::ROTATE_0;
            switch (index){
            case 0:
                rotation = VideoRotation::ROTATE_0;
                break;
            case 1:
                rotation = VideoRotation::ROTATE_90;
                break;
            case 2:
                rotation = VideoRotation::ROTATE_180;
                break;
            case 3:
                rotation = VideoRotation::ROTATE_NEGATIVE_90;
                break;
            default:
                return;
            }
            GlobalSettings::instance().VIDEO_PIPELINE->VIDEO_ROTATION.set(rotation);
        }
    );

    //  Set the action for the video reset button
    connect(
        m_reset_button, &QPushButton::clicked,
        this, [this](bool){
            update_source_list();
            m_session.reset();
        }
    );

    m_session.add_state_listener(*this);
}




void VideoSourceSelectorWidget::update_source_list(){
    m_sources_box->clear();
    m_sources.clear();

    std::shared_ptr<const VideoSourceDescriptor> current_descriptor = m_session.descriptor();

    //  Add all the static options.
    {
        VideoSourceOption option;
        m_session.get(option);
        m_sources.emplace_back(option.get_descriptor_from_cache(VideoSourceType::None));
        m_sources.emplace_back(option.get_descriptor_from_cache(VideoSourceType::StillImage));
    }

    //  Now add all the cameras.
    for (const CameraInfo& info : get_all_cameras()){
        m_sources.emplace_back(std::make_unique<VideoSourceDescriptor_Camera>(info));
    }

    int index = -1;
    for (int c = 0; c < (int)m_sources.size(); c++){
        const VideoSourceDescriptor& descriptor = *m_sources[c];
        m_sources_box->addItem(QString::fromStdString(descriptor.display_name()));
        if (*current_descriptor == descriptor){
            index = c;
        }
    }
    if (index >= 0){
        m_sources_box->setCurrentIndex((int)index);
    }else{
        m_logger.log("Unable to find entry for this source.", COLOR_RED);
    }
}
void VideoSourceSelectorWidget::update_resolution_list(){
    m_resolution_box->clear();

    Resolution camera_resolution = m_session.current_resolution();
    m_resolutions = m_session.supported_resolutions();

    int index = -1;
    for (int c = 0; c < (int)m_resolutions.size(); c++){
        const Resolution& size = m_resolutions[c];
        m_resolution_box->addItem(
            QString::fromStdString(
                std::to_string(size.width) + " x " +
                std::to_string(size.height) + " " +
                aspect_ratio_as_string(size)
            )
        );
        if (size == camera_resolution){
            index = c;
        }
    }
    if (index >= 0){
        m_resolution_box->setCurrentIndex(index);
    }else{
        m_logger.log("Unable to find entry for this resolution.", COLOR_RED);
    }
}
void VideoSourceSelectorWidget::update_rotation_list(){
    VideoRotation current_rotation = GlobalSettings::instance().VIDEO_PIPELINE->VIDEO_ROTATION;
    int index = 0;
    switch (current_rotation){
    case VideoRotation::ROTATE_0:
        index = 0;
        break;
    case VideoRotation::ROTATE_90:
        index = 1;
        break;
    case VideoRotation::ROTATE_180:
        index = 2;
        break;
    case VideoRotation::ROTATE_NEGATIVE_90:
        index = 3;
        break;
    }
    m_rotation_box->setCurrentIndex(index);
}




void VideoSourceSelectorWidget::post_startup(VideoSource* source){
    update_source_list();
    update_resolution_list();
}








}
