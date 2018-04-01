#include "objectline.h"
#include "../gui/mainwindow.h"
#include "../utility/algorithm.h"

ObjectLine::ObjectLine(
    std::weak_ptr<Controller> sol,
    nrg::dir dir,
    double target,
    double base
) :
    m_sol(std::move(sol)),
    m_dir(dir),
    m_target(target),
    m_base(base),

    m_done(false),

    m_state(State::REQUIRE_READY_MOVE) {}

void ObjectLine::start() {
    m_timer.start(50, this);
}

void ObjectLine::stop() {
    m_timer.stop();
}

bool ObjectLine::is_done() const {
    return m_done;
}

void ObjectLine::timerEvent(QTimerEvent *ev) {
    if (ev->timerId() == m_timer.timerId()) {
        movement_loop();
    }
}

void ObjectLine::movement_loop() {
    CompetitionState &state = Main::get()->state();
    if (
        !state.is_robot_box_fresh() ||
        !state.is_robot_box_valid() ||
        !state.is_object_box_fresh() ||
        !state.is_object_box_valid()
    ) {
        return;
    }
    log() << "Line State: " <<  m_state;
    switch (m_state) {
        case State::REQUIRE_READY_MOVE:
            do_require_ready_move();
            break;
        case State::DOING_READY_MOVE:
            do_doing_ready_move();
            break;
        case State::REQUIRE_OBJECT_MOVE:
            do_require_object_move();
            break;
        case State::DOING_OBJECT_MOVE:
            do_doing_object_move();
            break;
        case State::REQUIRE_CORRECTION:
            do_require_correction();
            break;
        case State::REQUIRE_CORRECTION_READY_MOVE:
            do_require_correction_ready_move();
            break;
        case State::DOING_CORRECTION_READY_MOVE:
            do_doing_correction_ready_move();
            break;
        case State::REQUIRE_CORRECTION_OBJECT_MOVE:
            do_require_correction_object_move();
            break;
        case State::DOING_CORRECTION_OBJECT_MOVE:
            do_doing_correction_object_move();
            break;
        default:
            break;
    }
}

void ObjectLine::do_require_ready_move() {
    nrg::dir side_dir = move_dir_to_side(m_dir);
    m_ready_move = std::make_unique<ReadyMove>(m_sol, side_dir);
    m_ready_move->start();
    m_state = State::DOING_READY_MOVE;
}

void ObjectLine::do_doing_ready_move() {
#ifndef NDEBUG
    assert(!!m_ready_move);
#endif
    if (m_ready_move->is_done()) {
        m_ready_move.reset();
        m_state = State::REQUIRE_OBJECT_MOVE;
    }
}

void ObjectLine::do_require_object_move() {
    nrg::dir dir = m_dir;
    double target = m_target;
    double norm_base = m_base;
    double norm_dev = 10;
    m_object_move = std::make_unique<ObjectMove>(m_sol, dir, target, norm_base, norm_dev);
    m_object_move->start();
    m_state = State::DOING_OBJECT_MOVE;
}

void ObjectLine::do_doing_object_move() {
#ifndef NDEBUG
    assert(!!m_object_move);
#endif
    if (!m_object_move->is_done()) {
        return;
    }
    ObjectMove::Stop stop_cond = m_object_move->get_stop();
#ifndef NDEBUG
    assert(stop_cond != ObjectMove::Stop::OKAY);
#endif
    m_object_move.reset();
    log() << "Stop Condition: " << stop_cond;
    switch (stop_cond) {
        case ObjectMove::Stop::AT_TARGET:
            m_timer.stop();
            m_done = true;
            return;
        case ObjectMove::Stop::WRONG_SIDE:
            m_state = State::REQUIRE_READY_MOVE;
            return;
        case ObjectMove::Stop::EXCEEDED_NORM:
            m_state = State::REQUIRE_CORRECTION;
            return;
        default:
            return;
    }
}

void ObjectLine::do_require_correction() {
    // Determine the correction direction
    CompetitionState &state = Main::get()->state();
    vector2d obj_loc = rect2d(state.get_object_box(true)).center();
    switch (m_dir) {
        case nrg::dir::RIGHT:
        case nrg::dir::LEFT:
            m_correction_dir = obj_loc.y() < m_base ? nrg::dir::DOWN : nrg::dir::UP;
            break;
        case nrg::dir::DOWN:
        case nrg::dir::UP:
            m_correction_dir = obj_loc.x() < m_base ? nrg::dir::RIGHT : nrg::dir::LEFT;
            break;
        default:
            break;
    }
    m_state = State::REQUIRE_CORRECTION_READY_MOVE;
}

void ObjectLine::do_require_correction_ready_move() {
    nrg::dir side_dir = move_dir_to_side(m_correction_dir);
    m_ready_move = std::make_unique<ReadyMove>(m_sol, side_dir);
    m_ready_move->start();
    m_state = State::DOING_CORRECTION_READY_MOVE;
}

void ObjectLine::do_doing_correction_ready_move() {
#ifndef NDEBUG
    assert(!!m_ready_move);
#endif
    if (m_ready_move->is_done()) {
        m_ready_move.reset();
        m_state = State::REQUIRE_CORRECTION_OBJECT_MOVE;
    }
}

void ObjectLine::do_require_correction_object_move() {
    nrg::dir dir = m_correction_dir;
    double target = m_base;
    // Base and deviation don't matter
    double norm_base = 0;
    double norm_dev = 10000;
    m_object_move = std::make_unique<ObjectMove>(m_sol, dir, target, norm_base, norm_dev);
    m_object_move->start();
    m_state = State::DOING_CORRECTION_OBJECT_MOVE;
}

void ObjectLine::do_doing_correction_object_move() {
#ifndef NDEBUG
    assert(!!m_object_move);
#endif
    if (!m_object_move->is_done()) {
        return;
    }
    ObjectMove::Stop stop_cond = m_object_move->get_stop();
#ifndef NDEBUG
    assert(stop_cond != ObjectMove::Stop::OKAY);
    assert(stop_cond != ObjectMove::Stop::EXCEEDED_NORM);
#endif
    m_object_move.reset();
    m_state = State::REQUIRE_READY_MOVE;
}
