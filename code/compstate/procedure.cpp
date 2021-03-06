#include "procedure.h"
#include "compstate.h"
#include "common.h"
#include "parammanager.h"

#include "../camera/statusbox.h"
#include "../camera/statuslabel.h"
#include "../controller/controller.h"
#include "../gui/global.h"
#include "../utility/logger.h"

#include <QBasicTimer>
#include <QTimerEvent>

#define DIR_RIGHT "RIGHT"
#define DIR_LEFT  "LEFT"
#define DIR_DOWN  "DOWN"
#define DIR_UP    "UP"

static QString err_text(double x, double y) {
    QString text;
    text.sprintf("Error: (%6.1f , %6.1f )", x, y);
    return text;
}

static QString index_text(std::size_t index) {
    return "Index: " + QString::number(index);
}

static QString perp_text(double err_x, double err_y, double norm_sq) {
    QString text;
    text.sprintf("PerpD: (%6.1f , %6.1f ) : %6.1f", err_x, err_y, norm_sq);
    return text;
}

class Procedure::Impl {
public:
    Impl(
        double t_loc_accept,
        double t_norm_dev,
        const path2d &t_path);

    double loc_accept;
    double norm_dev;
    path2d path;
    vector2d initial;
    std::size_t index;
    QBasicTimer timer;
};

Procedure::Impl::Impl(
    double t_loc_accept,
    double t_norm_dev,
    const path2d &t_path) :
    loc_accept(t_loc_accept),
    norm_dev(t_norm_dev),
    path(t_path),
    index(0) {}

Procedure::Procedure(
    std::weak_ptr<Controller> sol,
    const path2d &path,
    double loc_accept,
    double norm_dev
) :
    m_impl(std::make_unique<Impl>(loc_accept, norm_dev, path)),
    m_sol(std::move(sol)),
    m_done(false) {
    // Create the status labels and set their initial values
    if (auto lp = Main::get()->status_box().lock()) {
        m_dir_label = lp->add_label("IDLE");
        m_err_label = lp->add_label(err_text(0, 0));
        m_index_label = lp->add_label(index_text(m_impl->index));
        m_perp_label = lp->add_label(perp_text(0, 0, 0));
    }
}

Procedure::~Procedure() {
    // Remove status labels
    if (auto lp = Main::get()->status_box().lock()) {
        lp->remove_label(m_dir_label);
        lp->remove_label(m_err_label);
        lp->remove_label(m_index_label);
        lp->remove_label(m_perp_label);
    }
}

bool Procedure::is_done() const {
    return m_done;
}

bool Procedure::is_stopped() const {
    return !m_impl->timer.isActive();
}

void Procedure::start() {
    // Start the time and grab the initial robot location
    CompetitionState &state = Main::get()->state();
    m_impl->timer.start(g_pm->timer_reg, this);
    m_impl->initial = algo::rect_center(state.get_robot_box());
    Q_EMIT started();
}

void Procedure::stop() {
    // Stop the timer
    m_impl->timer.stop();
    Q_EMIT stopped();
}

void Procedure::timerEvent(QTimerEvent *ev) {
    if (ev->timerId() == m_impl->timer.timerId()) {
        movement_loop();
    }
}

void Procedure::movement_loop() {
    // If the path has been traversed or solenoid expired, stop timer
    if (m_impl->index == m_impl->path.size() || m_sol.expired()) {
        m_impl->timer.stop();
        m_done = true;
        Q_EMIT finished();
        return;
    }

    // If the box has not been updated, tracker has lost acquisition, skip this loop
    CompetitionState &state = Main::get()->state();
    if (
        !state.is_robot_box_fresh() ||
        !state.is_robot_box_valid()
    ) { return; }

    // Acquire the current robot position
    vector2d center = algo::rect_center(Main::get()->state().get_robot_box(true));
    vector2d target = m_impl->path[m_impl->index];
    // Source node is either the initial position or the last node
    vector2d source = m_impl->index > 0 ? m_impl->path[m_impl->index - 1] : m_impl->initial;

    // Find differences in each axis
    double err_x = target.x() - center.x();
    double err_y = target.y() - center.y();
    m_err_label->setText(err_text(err_x, err_y));

    // If within acceptance range, move to next point
    if (hypot(err_x, err_y) < m_impl->loc_accept) {
        ++m_impl->index;
        return;
    }
    m_index_label->setText(index_text(m_impl->index));

    // Calculate perpendicular distance to ensure the robot is straddling the line
    vector2d intersect = algo::perp_intersect(center, source, target);
    vector2d norm_diff = intersect - center;
    double norm_diff_sq = norm_diff.norm_sq();
    m_perp_label->setText(perp_text(norm_diff.x(), norm_diff.y(), norm_diff_sq));
    if (norm_diff_sq > m_impl->norm_dev * m_impl->norm_dev) {
        target = intersect;
        err_x = norm_diff.x();
        err_y = norm_diff.y();
    }

    // Attempt to reduce the error
    if (fabs(err_x) > fabs(err_y)) {
        double estimated_power = fabs(err_x);
        // Move in horizontal
        if (target.x() > center.x()) { move_right(estimated_power); }
        else { move_left(estimated_power); }
    } else {
        double estimated_power = fabs(err_y);
        // Move in vertical
        if (target.y() > center.y()) { move_down(estimated_power); }
        else { move_up(estimated_power); }
    }
}

void Procedure::move_right(double estimated_power) {
    // Right => +X
    if (m_dir_label) { m_dir_label->setText(DIR_RIGHT); }
    if (auto sol = m_sol.lock()) {
        sol->move({static_cast<int>(estimated_power), 0});
    }
}

void Procedure::move_left(double estimated_power) {
    // Left => -X
    if (m_dir_label) { m_dir_label->setText(DIR_LEFT); }
    if (auto sol = m_sol.lock()) {
        sol->move({-static_cast<int>(estimated_power), 0});
    }
}

void Procedure::move_up(double estimated_power) {
    // Up => -Y
    if (m_dir_label) { m_dir_label->setText(DIR_UP); }
    if (auto sol = m_sol.lock()) {
        sol->move({0, -static_cast<int>(estimated_power)});
    }
}

void Procedure::move_down(double estimated_power) {
    // Down => +Y
    if (m_dir_label) { m_dir_label->setText(DIR_DOWN); }
    if (auto sol = m_sol.lock()) {
        sol->move({0, static_cast<int>(estimated_power)});
    }
}
