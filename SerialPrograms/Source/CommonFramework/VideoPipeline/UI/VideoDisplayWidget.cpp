/*  Video Display
 *
 *  From: https://github.com/PokemonAutomation/
 *
 */

#include <QApplication>
#include <QVBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <mutex>
#include "Common/Cpp/PrettyPrint.h"
#include "Common/Qt/Redispatch.h"
#include "VideoDisplayWidget.h"
#include "VideoDisplayWindow.h"

//#include <iostream>
//using std::cout;
//using std::endl;

namespace PokemonAutomation{



// Shared state for mouse tracking
class MouseInspectorState : public VideoOverlay::MouseListener{
public:
    MouseInspectorState(VideoOverlaySession& overlay)
        : m_overlay(overlay)
    {
        m_overlay.add_mouse_listener(*this);
    }
    virtual ~MouseInspectorState(){
        m_overlay.remove_mouse_listener(*this);
    }

    // MouseListener implementation
    virtual void on_mouse_press(double x, double y) override{
        std::lock_guard<std::mutex> lg(m_lock);
        m_mouse_x = x;
        m_mouse_y = y;
        m_dragging = true;
        m_has_box = true;
        m_box_start_x = x;
        m_box_start_y = y;
        m_box_end_x = x;
        m_box_end_y = y;
    }
    virtual void on_mouse_release(double x, double y) override{
        std::lock_guard<std::mutex> lg(m_lock);
        m_mouse_x = x;
        m_mouse_y = y;
        m_dragging = false;
        m_box_end_x = x;
        m_box_end_y = y;
    }
    virtual void on_mouse_move(double x, double y) override{
        std::lock_guard<std::mutex> lg(m_lock);
        m_mouse_x = x;
        m_mouse_y = y;
        if (m_dragging){
            m_box_end_x = x;
            m_box_end_y = y;
        }
    }

    struct Snapshot{
        double mouse_x = -1.0;
        double mouse_y = -1.0;
        bool has_box = false;
        double box_start_x = 0.0;
        double box_start_y = 0.0;
        double box_end_x = 0.0;
        double box_end_y = 0.0;
    };

    Snapshot get_snapshot(){
        std::lock_guard<std::mutex> lg(m_lock);
        Snapshot snap;
        snap.mouse_x = m_mouse_x;
        snap.mouse_y = m_mouse_y;
        snap.has_box = m_has_box;
        snap.box_start_x = m_box_start_x;
        snap.box_start_y = m_box_start_y;
        snap.box_end_x = m_box_end_x;
        snap.box_end_y = m_box_end_y;
        return snap;
    }

private:
    VideoOverlaySession& m_overlay;
    std::mutex m_lock;

    double m_mouse_x = -1.0;
    double m_mouse_y = -1.0;

    bool m_dragging = false;
    bool m_has_box = false;
    double m_box_start_x = 0.0;
    double m_box_start_y = 0.0;
    double m_box_end_x   = 0.0;
    double m_box_end_y   = 0.0;
};

// Stat for cursor position (first line)
class MouseCursorStat : public OverlayStat{
public:
    MouseCursorStat(MouseInspectorState& state) : m_state(state) {}
    virtual OverlayStatSnapshot get_current() override{
        auto snap = m_state.get_snapshot();
        OverlayStatSnapshot result;
        if (snap.mouse_x < 0 || snap.mouse_y < 0){
            result.text = "Click: (n/a)";
        } else {
            result.text = "Click: (" +
                tostr_fixed(snap.mouse_x, 2) + ", " +
                tostr_fixed(snap.mouse_y, 2) + ")";
        }
        result.color = COLOR_WHITE;
        return result;
    }
private:
    MouseInspectorState& m_state;
};

// Stat for box coordinates (second line)
class MouseBoxStat : public OverlayStat{
public:
    MouseBoxStat(MouseInspectorState& state) : m_state(state) {}
    virtual OverlayStatSnapshot get_current() override{
        auto snap = m_state.get_snapshot();
        OverlayStatSnapshot result;
        if (!snap.has_box){
            result.text = "Box: (none)";
        } else {
            double x0 = std::min(snap.box_start_x, snap.box_end_x);
            double y0 = std::min(snap.box_start_y, snap.box_end_y);
            double w  = std::abs(snap.box_end_x - snap.box_start_x);
            double h  = std::abs(snap.box_end_y - snap.box_start_y);

            result.text = "Box: (" +
                tostr_fixed(x0, 2) + ", " +
                tostr_fixed(y0, 2) + "; " +
                tostr_fixed(w, 2) + " x " +
                tostr_fixed(h, 2) + ")";
        }
        result.color = COLOR_WHITE;
        return result;
    }
private:
    MouseInspectorState& m_state;
};


VideoDisplayWidget::VideoDisplayWidget(
    QWidget& parent, QLayout& holder,
    size_t id,
    CommandReceiver& command_receiver,
    VideoSession& video_session,
    VideoOverlaySession& overlay
)
    : WidgetStackFixedAspectRatio(parent, WidgetStackFixedAspectRatio::ADJUST_HEIGHT_TO_WIDTH)
    , m_holder(holder)
    , m_id(id)
    , m_command_receiver(command_receiver)
    , m_video_session(video_session)
    , m_overlay_session(overlay)
    , m_overlay(new VideoOverlayWidget(*this, overlay))
    , m_underlay(new QWidget(this))
    , m_source_fps(*this)
    , m_display_fps(*this)
    , m_mouse_state(std::make_unique<MouseInspectorState>(overlay))
    , m_mouse_cursor_stat(std::make_unique<MouseCursorStat>(*m_mouse_state))
    , m_mouse_box_stat(std::make_unique<MouseBoxStat>(*m_mouse_state))
{
    this->add_widget(*m_overlay);

    VideoSource* source = video_session.current_source();
    if (source){
        m_video = source->make_display_QtWidget(this);
        this->add_widget(*m_video);
    }

    Resolution resolution = video_session.current_resolution();
    if (resolution){
        set_aspect_ratio(resolution.aspect_ratio());
    }

    m_overlay->setVisible(true);
    m_overlay->setHidden(false);
    m_overlay->raise();

#if 1
    {
        m_underlay->setHidden(true);
        holder.addWidget(m_underlay);

        QVBoxLayout* layout = new QVBoxLayout(m_underlay);
        layout->setAlignment(Qt::AlignTop);

        QHBoxLayout* row_width = new QHBoxLayout();
        layout->addLayout(row_width);
        QHBoxLayout* row_height = new QHBoxLayout();
        layout->addLayout(row_height);

        row_width->addStretch(2);
        row_height->addStretch(2);
        row_width->addWidget(new QLabel("<b>Window Width:</b>", m_underlay), 1);
        row_height->addWidget(new QLabel("<b>Window Height:</b>", m_underlay), 1);

        m_width_box = new QLineEdit(m_underlay);
        row_width->addWidget(m_width_box, 1);
        m_height_box = new QLineEdit(m_underlay);
        row_height->addWidget(m_height_box, 1);

        row_width->addStretch(2);
        row_height->addStretch(2);

        connect(
            m_width_box, &QLineEdit::editingFinished,
            this, [this]{
                bool ok;
                int value = m_width_box->text().toInt(&ok);
                if (ok && 100 <= value){
                    m_last_width = value;
                    if (m_window){
                        m_window->resize(m_last_width, m_last_height);
                    }
                }
                m_width_box->setText(QString::number(m_last_width));
            }
        );
        connect(
            m_height_box, &QLineEdit::editingFinished,
            this, [this]{
                bool ok;
                int value = m_height_box->text().toInt(&ok);
                if (ok && 100 <= value){
                    m_last_height = value;
                    if (m_window){
                        m_window->resize(m_last_width, m_last_height);
                    }
                }
                m_height_box->setText(QString::number(m_last_height));
            }
        );
    }
#endif

    overlay.add_stat(m_source_fps);
    overlay.add_stat(m_display_fps);
    overlay.add_stat(*m_mouse_cursor_stat);
    overlay.add_stat(*m_mouse_box_stat);

    video_session.add_state_listener(*this);
}
VideoDisplayWidget::~VideoDisplayWidget(){
    //  This is an ugly work-around for deadlock that can occur if the
    //  destructor of this is class is called while a reset on the VideoSession
    //  is in flight.
    //
    //  Because Qt requires everything to run on the main thread, outside
    //  threads that reset the VideoSession will get redispatched to the main
    //  thread while holding a lock on the listener. If this redispatch gets
    //  queued behind the task running here, it will wait on the same listener
    //  thus deadlocking.
    //
    //  The work-around is that if we fail to acquire this lock, we process the
    //  event queue to eventually run the task that is holding the lock.
    //
    while (!m_video_session.try_remove_state_listener(*this)){
        m_video_session.logger().log(
            "VideoDisplayWidget::~VideoDisplayWidget(): Lock already held. Processing events...",
            COLOR_ORANGE
        );
        QApplication::processEvents();
    }

    //  Close the window popout first since it holds references to this class.
    move_back_from_window();
    m_overlay_session.remove_stat(*m_mouse_box_stat);
    m_overlay_session.remove_stat(*m_mouse_cursor_stat);
    m_overlay_session.remove_stat(m_display_fps);
    m_overlay_session.remove_stat(m_source_fps);
    delete m_underlay;
}

void VideoDisplayWidget::clear_video_source(){
//    cout << "clear_video_source() = " << m_video << endl;
    if (m_video){
        this->remove_widget(m_video);
        m_video = nullptr;
    }
}

void VideoDisplayWidget::post_startup(VideoSource* source){
//    cout << "post_startup() = " << m_video << endl;
    run_on_object_thread_and_wait(this, [&]{
        clear_video_source();
        if (source){
            m_video = source->make_display_QtWidget(this);
//            cout << "post_startup() - creating = " << m_video << endl;
            this->add_widget(*m_video);
            set_aspect_ratio(source->current_resolution().aspect_ratio());
            m_overlay->raise();
        }
    });
}
void VideoDisplayWidget::pre_shutdown(){
//    cout << "pre_shutdown()" << endl;
    run_on_object_thread_and_wait(this, [&]{
        clear_video_source();
    });
}



void VideoDisplayWidget::move_to_new_window(){
    if (m_window){
        return;
    }
    // The constructor of VideoDisplayWindow handles the transfer of this VideoDisplayWidget to the new window.
    // The constructor also displays the window.
    // So there is nothing else to do in VideoDisplayWidget::move_to_new_window() besides building VideoDisplayWindow.
    this->set_size_policy(EXPAND_TO_BOX);
    m_window.reset(new VideoDisplayWindow(this));
    m_underlay->setHidden(false);
}
void VideoDisplayWidget::move_back_from_window(){
    if (!m_window){
        return;
    }
    m_underlay->setHidden(true);
    this->set_size_policy(ADJUST_HEIGHT_TO_WIDTH);
    m_holder.addWidget(this);
//    this->resize(this->size());
//    cout << "VideoWidget Before: " << m_video->width() << " x " << m_video->height() << endl;
    if (m_video){
        m_video->resize(this->size());
    }
//    cout << "VideoWidget After: " << m_video->width() << " x " << m_video->height() << endl;
    m_holder.update();
    m_window.reset();
}



void VideoDisplayWidget::mouseDoubleClickEvent(QMouseEvent* event){
//    if (!PreloadSettings::instance().DEVELOPER_MODE){
//        return;
//    }
    // If this widget is not already inside a VideoDisplayWindow, move it
    // into a VideoDisplayWindow
    if (!m_window){
        move_to_new_window();
    }else{
        QWidget::mouseDoubleClickEvent(event);
    }
}
void VideoDisplayWidget::paintEvent(QPaintEvent* event){
    WidgetStackFixedAspectRatio::paintEvent(event);
//    cout << "VideoDisplayWidget: " << this->width() << " x " << this->height() << endl;
//    cout << "VideoWidget: " << m_video->width() << " x " << m_video->height() << endl;
}
void VideoDisplayWidget::resizeEvent(QResizeEvent* event){
    WidgetStackFixedAspectRatio::resizeEvent(event);
    m_last_width = this->width();
    m_last_height = this->height();
    m_width_box->setText(QString::number(m_last_width));
    m_height_box->setText(QString::number(m_last_height));
}

void VideoDisplayWidget::mousePressEvent(QMouseEvent* event){
    WidgetStackFixedAspectRatio::mousePressEvent(event);
    double x = (double)event->pos().x() / this->width();
    double y = (double)event->pos().y() / this->height();
    m_overlay_session.issue_mouse_press(x, y);
}
void VideoDisplayWidget::mouseReleaseEvent(QMouseEvent* event){
    WidgetStackFixedAspectRatio::mouseReleaseEvent(event);
    double x = (double)event->pos().x() / this->width();
    double y = (double)event->pos().y() / this->height();
    m_overlay_session.issue_mouse_release(x, y);
}
void VideoDisplayWidget::mouseMoveEvent(QMouseEvent* event){
    WidgetStackFixedAspectRatio::mouseMoveEvent(event);
    double x = (double)event->pos().x() / this->width();
    double y = (double)event->pos().y() / this->height();
    m_overlay_session.issue_mouse_move(x, y);
}

void VideoDisplayWidget::on_key_press(QKeyEvent* event){
    m_overlay_session.issue_key_press(event);
}

void VideoDisplayWidget::on_key_release(QKeyEvent* event){
    m_overlay_session.issue_key_release(event);
}


OverlayStatSnapshot VideoSourceFPS::get_current(){
    double fps = m_parent.m_video_session.fps_source();
    return OverlayStatSnapshot{
        "Video Source FPS: " + tostr_fixed(fps, 2),
        fps < 20 ? COLOR_RED : COLOR_WHITE
    };
}
OverlayStatSnapshot VideoDisplayFPS::get_current(){
    double fps = m_parent.m_video_session.fps_display();
    return OverlayStatSnapshot{
        "Video Display FPS: " + (fps < 0 ? "???" : tostr_fixed(fps, 2)),
        fps >= 0 && fps < 20 ? COLOR_RED : COLOR_WHITE
    };
}



}
