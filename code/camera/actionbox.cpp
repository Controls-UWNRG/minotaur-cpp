#include <QVBoxLayout>

#include "actionbox.h"
#include "../utility/utility.h"

ActionBox::ActionBox(QWidget *parent) :
    QDialog(parent),
    m_layout(std::make_unique<QVBoxLayout>()) {
    // Create the layout and set it
    reset_actions();
    setLayout(m_layout.get());
}

void ActionBox::reset_actions() {
    // Replace the layout with a new, empty one
    m_layout = std::make_unique<QVBoxLayout>();
    // Clearing list causes unique pointers to deallocate
    m_actions.clear();
    hide();
}

void ActionBox::set_actions() {
    // Set the new layout and show the UI
    setLayout(m_layout.get());
    show();
}

ActionButton *ActionBox::add_action(QString &&label) {
    // New button index
    std::size_t size = m_actions.size();
    // Emplace the pointer
    m_actions.push_back(std::make_unique<ActionButton>(std::forward<QString>(label), this));
    m_layout->addWidget(m_actions[size].get());
    // Return reference to the button
    return m_actions[size].get();
}
