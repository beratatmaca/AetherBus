#include "gui/widgets/collapsible_splitter.hpp"

#include <QApplication>
#include <QEnterEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

namespace aether {

namespace {

constexpr int kDefaultExpandedSize = 260;  ///< Sensible fallback width for a pane that was never seen expanded
                                           ///< (e.g. a session restored from settings with no remembered size yet).

int handleIndexOf(QSplitter *splitter, QSplitterHandle *handle) {
    for (int i = 0; i < splitter->count(); ++i) {
        if (splitter->handle(i) == handle) {
            return i;
        }
    }
    return -1;
}

}  // namespace

/**
 * @brief Draws a small, palette-driven chevron on top of the normal QSS-styled
 * handle background, only for handles that border a collapsible pane —
 * clicking (not dragging) toggles that pane.
 *
 * Dragging to resize still works unchanged; this only adds a click affordance
 * on top of it.
 */
class CollapsibleSplitterHandle : public QSplitterHandle {
public:
    CollapsibleSplitterHandle(Qt::Orientation orientation, CollapsibleSplitter *parent)
        : QSplitterHandle(orientation, parent), m_owner(parent) {
        setAttribute(Qt::WA_Hover, true);
    }

protected:
    void paintEvent(QPaintEvent *event) override {
        QSplitterHandle::paintEvent(event);

        const int paneIndex = targetPane();
        if (paneIndex < 0) {
            return;
        }

        const int myIndex = handleIndexOf(m_owner, this);
        const bool paneBeforeHandle = (paneIndex == myIndex - 1);
        const bool collapsed = m_owner->isPaneCollapsed(paneIndex);

        int sign = paneBeforeHandle ? -1 : 1;
        if (collapsed) {
            sign = -sign;
        }

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        // A subtle rounded backdrop makes this read as a distinct clickable
        // affordance rather than a stray hairline, without being loud.
        constexpr int kPillLong = 18;
        constexpr int kPillShort = 11;
        QRect pill(0, 0, orientation() == Qt::Horizontal ? kPillShort : kPillLong,
                   orientation() == Qt::Horizontal ? kPillLong : kPillShort);
        pill.moveCenter(rect().center());
        QColor bg = palette().color(QPalette::Button);
        bg.setAlpha(m_hovering ? 150 : 70);
        painter.setPen(Qt::NoPen);
        painter.setBrush(bg);
        painter.drawRoundedRect(pill, 3, 3);

        const QColor glyphColor = m_hovering ? palette().color(QPalette::Highlight) : palette().color(QPalette::WindowText);
        painter.setPen(QPen(glyphColor, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));

        const QPoint c = rect().center();
        constexpr int kReach = 4;
        constexpr int kSpread = 3;
        QPainterPath path;
        if (orientation() == Qt::Horizontal) {
            // Vertical bar; chevron opens left or right.
            if (sign < 0) {
                path.moveTo(c.x() + kSpread, c.y() - kReach);
                path.lineTo(c.x() - kSpread, c.y());
                path.lineTo(c.x() + kSpread, c.y() + kReach);
            } else {
                path.moveTo(c.x() - kSpread, c.y() - kReach);
                path.lineTo(c.x() + kSpread, c.y());
                path.lineTo(c.x() - kSpread, c.y() + kReach);
            }
        } else {
            // Horizontal bar; chevron opens up or down.
            if (sign < 0) {
                path.moveTo(c.x() - kReach, c.y() + kSpread);
                path.lineTo(c.x(), c.y() - kSpread);
                path.lineTo(c.x() + kReach, c.y() + kSpread);
            } else {
                path.moveTo(c.x() - kReach, c.y() - kSpread);
                path.lineTo(c.x(), c.y() + kSpread);
                path.lineTo(c.x() + kReach, c.y() - kSpread);
            }
        }
        painter.drawPath(path);
    }

    void mousePressEvent(QMouseEvent *event) override {
        m_pressPos = event->pos();
        QSplitterHandle::mousePressEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override {
        QSplitterHandle::mouseReleaseEvent(event);
        if ((event->pos() - m_pressPos).manhattanLength() < QApplication::startDragDistance()) {
            const int paneIndex = targetPane();
            if (paneIndex >= 0) {
                m_owner->toggleCollapse(paneIndex);
            }
        }
    }

    void enterEvent(QEnterEvent *event) override {
        m_hovering = true;
        update();
        QSplitterHandle::enterEvent(event);
    }

    void leaveEvent(QEvent *event) override {
        m_hovering = false;
        update();
        QSplitterHandle::leaveEvent(event);
    }

private:
    /**
     * @brief Which pane (if any) this handle toggles: the pane immediately before
     * it, or immediately after, whichever was marked collapsible.
     *
     * Returns -1 if neither neighbor is collapsible.
     */
    [[nodiscard]] int targetPane() const {
        const int idx = handleIndexOf(m_owner, const_cast<CollapsibleSplitterHandle *>(this));
        if (idx <= 0) {
            return -1;
        }
        if (m_owner->isPaneCollapsible(idx - 1)) {
            return idx - 1;
        }
        if (m_owner->isPaneCollapsible(idx)) {
            return idx;
        }
        return -1;
    }

    CollapsibleSplitter *m_owner;
    QPoint m_pressPos;
    bool m_hovering = false;
};

CollapsibleSplitter::CollapsibleSplitter(Qt::Orientation orientation, QWidget *parent) : QSplitter(orientation, parent) {
    setProperty("collapsibleHandles", true);
}

QSplitterHandle *CollapsibleSplitter::createHandle() {
    return new CollapsibleSplitterHandle(orientation(), this);
}

void CollapsibleSplitter::setPaneCollapsible(int paneIndex, bool collapsible) {
    if (collapsible) {
        if (!m_collapsiblePanes.contains(paneIndex)) {
            m_collapsiblePanes.append(paneIndex);
        }
    } else {
        m_collapsiblePanes.removeAll(paneIndex);
    }
}

bool CollapsibleSplitter::isPaneCollapsible(int paneIndex) const {
    return m_collapsiblePanes.contains(paneIndex);
}

bool CollapsibleSplitter::isPaneCollapsed(int paneIndex) const {
    const QList<int> currentSizes = sizes();
    if (paneIndex < 0 || paneIndex >= currentSizes.size()) {
        return false;
    }
    return currentSizes[paneIndex] == 0;
}

void CollapsibleSplitter::toggleCollapse(int paneIndex) {
    QList<int> currentSizes = sizes();
    if (paneIndex < 0 || paneIndex >= currentSizes.size()) {
        return;
    }

    if (currentSizes[paneIndex] > 0) {
        m_lastFullSizes[paneIndex] = currentSizes;
        currentSizes[paneIndex] = 0;
        setSizes(currentSizes);
        return;
    }

    const auto it = m_lastFullSizes.constFind(paneIndex);
    if (it != m_lastFullSizes.constEnd()) {
        setSizes(it.value());
        return;
    }
    currentSizes[paneIndex] = kDefaultExpandedSize;
    setSizes(currentSizes);
}

}  // namespace aether
