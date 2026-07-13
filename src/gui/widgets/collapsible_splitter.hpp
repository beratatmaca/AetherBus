/**
 * @file collapsible_splitter.hpp
 * @brief QSplitter with a click-to-toggle affordance on its handles.
 *
 * Standard QSplitter only lets a pane collapse by dragging its handle all the
 * way to the edge — discoverable to almost no one, and easy to trigger by
 * accident. This subclass adds an explicit, themed chevron drawn on the
 * handle: clicking it (a real click, not a drag) collapses the neighboring
 * pane to zero size, or restores it to whatever size it was before
 * collapsing. Dragging the handle to resize still works exactly as before.
 */
#pragma once

#include <QHash>
#include <QList>
#include <QSplitter>

namespace aether {

class CollapsibleSplitter : public QSplitter {
    Q_OBJECT

public:
    explicit CollapsibleSplitter(Qt::Orientation orientation, QWidget *parent = nullptr);

    /**
     * @brief Mark @p paneIndex as toggleable via its neighboring handle.
     *
     * Pane 0 toggles via @c handle(1) (Qt's @c handle(0) is always inert,
     * leading nothing); any other pane toggles via @c handle(paneIndex).
     */
    void setPaneCollapsible(int paneIndex, bool collapsible = true);

    /** @return Whether @p paneIndex is currently collapsed (size 0). */
    [[nodiscard]] bool isPaneCollapsed(int paneIndex) const;

protected:
    QSplitterHandle *createHandle() override;

private:
    friend class CollapsibleSplitterHandle;

    /// Collapse @p paneIndex if expanded, or restore it if collapsed.
    void toggleCollapse(int paneIndex);

    /// Whether @p paneIndex has been marked collapsible via @ref setPaneCollapsible.
    [[nodiscard]] bool isPaneCollapsible(int paneIndex) const;

    /// paneIndex -> the full sizes() list at the moment it was collapsed, so
    /// restoring replays exactly what Qt already normalized once before
    /// instead of re-deriving it (which can drift by a rounding pixel).
    QHash<int, QList<int>> m_lastFullSizes;
    QList<int> m_collapsiblePanes;
};

}  // namespace aether
