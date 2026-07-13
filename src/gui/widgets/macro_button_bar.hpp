/**
 * @file macro_button_bar.hpp
 * @brief Reusable macro button bar widget shared by all session types.
 *
 * Provides the common chrome (container, HBoxLayout, empty hint label, rebuild
 * loop) so that Serial, CAN and Ethernet sessions do not repeat the same
 * layout boilerplate.  Each concrete macro type only needs to implement a thin
 * subclass that supplies its own data model and button-click action.
 */
#pragma once

#include <QHBoxLayout>
#include <QLabel>
#include <QLayoutItem>
#include <QMenu>
#include <QPointer>
#include <QPushButton>
#include <QWidget>

namespace aether {

/**
 * @brief Abstract base for a horizontal row of quick-send macro buttons.
 *
 * Concrete subclasses must override:
 *  - @ref macroCount()
 *  - @ref macroName(int)
 *  - @ref macroToolTip(int)
 *  - @ref onMacroTriggered(int)
 *  - @ref buildContextMenu(int, QMenu &)
 */
class MacroButtonBar : public QWidget {
    Q_OBJECT

public:
    explicit MacroButtonBar(QWidget *parent = nullptr);

    /**
     * @brief Rebuild all macro buttons from the current data model.
     *
     * Clears existing buttons, then calls the virtual helpers to populate
     * fresh ones.  Subclasses should call this after any mutation of their
     * macro list.
     */
    void rebuildButtons();

protected:
    // ------------------------------------------------------------------ //
    //  Subclass interface                                                  //
    // ------------------------------------------------------------------ //

    /** @brief Number of macros in the data model. */
    virtual int macroCount() const = 0;

    /** @brief Display name for macro at @p index. */
    virtual QString macroName(int index) const = 0;

    /** @brief Tooltip text for the button at @p index. */
    virtual QString macroToolTip(int index) const = 0;

    /**
     * @brief Called when the user left-clicks the button at @p index.
     *
     * Subclass should emit its send signal / invoke its backend here.
     */
    virtual void onMacroTriggered(int index) = 0;

    /**
     * @brief Populate @p menu with context actions for macro at @p index.
     *
     * The base implementation calls this to build the right-click context
     * menu shown on each button.  Subclasses are free to add any actions
     * (Edit, Delete, Flip direction, …).
     */
    virtual void buildContextMenu(int index, QMenu &menu) = 0;

private:
    QHBoxLayout *m_layout = nullptr;
    QPointer<QLabel> m_emptyHint;
};

}  // namespace aether
