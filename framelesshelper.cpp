#include "framelesshelper.h"

#include <QApplication>
#include <QCursor>
#include <QEvent>
#include <QGuiApplication>
#include <QHoverEvent>
#include <QMouseEvent>
#include <QScreen>
#include <QWidget>
#include <QWindow>
#include <limits>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace {

QPoint localPosFromEvent(const QEvent *event)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    if (event->type() == QEvent::HoverMove) {
        return static_cast<const QHoverEvent *>(event)->position().toPoint();
    }
    switch (event->type()) {
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::MouseButtonDblClick:
    case QEvent::MouseMove:
        return static_cast<const QMouseEvent *>(event)->position().toPoint();
    default:
        break;
    }
#else
    if (event->type() == QEvent::HoverMove) {
        return static_cast<const QHoverEvent *>(event)->pos();
    }
    switch (event->type()) {
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::MouseButtonDblClick:
    case QEvent::MouseMove:
        return static_cast<const QMouseEvent *>(event)->pos();
    default:
        break;
    }
#endif
    return {};
}

QPoint globalPosFromEvent(const QEvent *event)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    switch (event->type()) {
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::MouseButtonDblClick:
    case QEvent::MouseMove:
        return static_cast<const QMouseEvent *>(event)->globalPosition().toPoint();
    default:
        break;
    }
#else
    switch (event->type()) {
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::MouseButtonDblClick:
    case QEvent::MouseMove:
        return static_cast<const QMouseEvent *>(event)->globalPos();
    default:
        break;
    }
#endif
    return QCursor::pos();
}

int distanceToRect(const QPoint &pos, const QRect &rect)
{
    const int dx = qMax(0, qMax(rect.left() - pos.x(), pos.x() - rect.right()));
    const int dy = qMax(0, qMax(rect.top() - pos.y(), pos.y() - rect.bottom()));
    return dx * dx + dy * dy;
}

} // namespace

FramelessHelper::FramelessHelper(QObject *parent)
    : QObject(parent)
{
}

FramelessHelper::~FramelessHelper()
{
    finishInteraction(false);
    if (qApp) {
        qApp->removeEventFilter(this);
        qApp->removeNativeEventFilter(this);
    }
}

void FramelessHelper::activateOn(QWidget *topLevelWidget)
{
    if (qApp) {
        qApp->removeEventFilter(this);
        qApp->removeNativeEventFilter(this);
    }

    m_targetWidget = topLevelWidget;
    if (!m_targetWidget) {
        return;
    }

    const bool wasVisible = m_targetWidget->isVisible();
    m_targetWidget->setWindowFlags(m_targetWidget->windowFlags()
                                   | Qt::FramelessWindowHint
                                   | Qt::WindowMinMaxButtonsHint
                                   | Qt::WindowSystemMenuHint);
    m_targetWidget->setAttribute(Qt::WA_Hover, true);
    m_targetWidget->setMouseTracking(true);

    if (qApp) {
        qApp->installEventFilter(this);
        qApp->installNativeEventFilter(this);
    }

    applyNativeWindowStyle();
    if (wasVisible) {
        m_targetWidget->show();
        applyNativeWindowStyle();
    }
}

void FramelessHelper::setTitleBar(QWidget *titleBarWidget)
{
    m_titleBarWidget = titleBarWidget;
    if (!m_titleBarWidget) {
        return;
    }
    m_titleBarWidget->setAttribute(Qt::WA_Hover, true);
    m_titleBarWidget->setMouseTracking(true);
}

void FramelessHelper::toggleMaximized()
{
    if (!m_targetWidget) {
        return;
    }
    if (isMaximizedState()) {
        restoreToNormal(restoreGeometryHint());
    } else {
        maximizeOnScreen(screenAt(QCursor::pos()));
    }
}

bool FramelessHelper::isMaximizedState() const
{
    return isEffectivelyMaximized();
}

bool FramelessHelper::isManagedWidget(const QObject *obj) const
{
    const auto *widget = qobject_cast<const QWidget *>(obj);
    return widget && m_targetWidget && widget->window() == m_targetWidget;
}

bool FramelessHelper::useNativeHitTest() const
{
#ifdef Q_OS_WIN
    return m_targetWidget != nullptr;
#else
    return false;
#endif
}

QPoint FramelessHelper::mapToTarget(const QObject *watched, const QPoint &pos) const
{
    if (!m_targetWidget || watched == m_targetWidget) {
        return pos;
    }
    const auto *src = qobject_cast<const QWidget *>(watched);
    if (!src) {
        return pos;
    }
    return src->mapTo(m_targetWidget, pos);
}

QRect FramelessHelper::titleBarRectInTarget() const
{
    if (!m_targetWidget || !m_titleBarWidget) {
        return {};
    }
    return QRect(m_titleBarWidget->mapTo(m_targetWidget, QPoint(0, 0)), m_titleBarWidget->size());
}

bool FramelessHelper::isInteractiveWidget(const QWidget *widget) const
{
    if (!widget || widget == m_targetWidget || widget == m_titleBarWidget) {
        return false;
    }
    return widget->inherits("QAbstractButton")
        || widget->inherits("QComboBox")
        || widget->inherits("QLineEdit")
        || widget->inherits("QAbstractSlider")
        || widget->inherits("QAbstractSpinBox")
        || widget->inherits("QTabBar")
        || widget->inherits("QMenuBar");
}

bool FramelessHelper::isEffectivelyMaximized() const
{
    if (!m_targetWidget) {
        return false;
    }
    return m_snapKind == SnapKind::Maximized || m_targetWidget->isMaximized();
}

bool FramelessHelper::isTiled() const
{
    return isEffectivelyMaximized() || m_snapKind == SnapKind::Left || m_snapKind == SnapKind::Right;
}

QScreen *FramelessHelper::screenAt(const QPoint &globalPos) const
{
    if (QScreen *screen = QGuiApplication::screenAt(globalPos)) {
        return screen;
    }

    QScreen *best = nullptr;
    int bestDist = std::numeric_limits<int>::max();
    const auto screens = QGuiApplication::screens();
    for (QScreen *screen : screens) {
        if (!screen) {
            continue;
        }
        const QRect geo = screen->geometry();
        if (geo.contains(globalPos)) {
            return screen;
        }
        const int dist = distanceToRect(globalPos, geo);
        if (dist < bestDist) {
            bestDist = dist;
            best = screen;
        }
    }
    if (best) {
        return best;
    }
    if (m_targetWidget) {
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
        if (QScreen *current = m_targetWidget->screen()) {
            return current;
        }
#else
        if (QWindow *handle = m_targetWidget->windowHandle()) {
            if (QScreen *current = handle->screen()) {
                return current;
            }
        }
#endif
    }
    return QGuiApplication::primaryScreen();
}

FramelessHelper::ResizeRegion FramelessHelper::getRegion(const QPoint &posInTarget) const
{
    if (!m_targetWidget || m_targetWidget->isFullScreen()) {
        return None;
    }

    const int x = posInTarget.x();
    const int y = posInTarget.y();
    const int w = m_targetWidget->width();
    const int h = m_targetWidget->height();

    const bool maximized = isEffectivelyMaximized();
    if (!maximized) {
        if (x < m_padding && y < m_padding) return TopLeft;
        if (x > w - m_padding && y < m_padding) return TopRight;
        if (x < m_padding && y > h - m_padding) return BottomLeft;
        if (x > w - m_padding && y > h - m_padding) return BottomRight;
        if (x < m_padding) return Left;
        if (x > w - m_padding) return Right;
        if (y < m_padding) return Top;
        if (y > h - m_padding) return Bottom;
    }

    QWidget *child = m_targetWidget->childAt(posInTarget);
    while (child && child != m_targetWidget) {
        if (isInteractiveWidget(child)) {
            return None;
        }
        child = child->parentWidget();
    }

    if (m_titleBarWidget) {
        if (titleBarRectInTarget().contains(posInTarget)) {
            return Inner;
        }
        return None;
    }

    if (x >= m_padding && x <= w - m_padding && y >= m_padding && y <= h - m_padding) {
        return Inner;
    }
    if (maximized) {
        return Inner;
    }
    return None;
}

void FramelessHelper::updateCursorShape(const QPoint &posInTarget)
{
    if (!m_targetWidget) {
        return;
    }
    if (isEffectivelyMaximized() || m_targetWidget->isFullScreen()) {
        m_targetWidget->unsetCursor();
        return;
    }

    switch (getRegion(posInTarget)) {
    case TopLeft:
    case BottomRight:
        m_targetWidget->setCursor(Qt::SizeFDiagCursor);
        break;
    case TopRight:
    case BottomLeft:
        m_targetWidget->setCursor(Qt::SizeBDiagCursor);
        break;
    case Left:
    case Right:
        m_targetWidget->setCursor(Qt::SizeHorCursor);
        break;
    case Top:
    case Bottom:
        m_targetWidget->setCursor(Qt::SizeVerCursor);
        break;
    default:
        m_targetWidget->unsetCursor();
        break;
    }
}

QRect FramelessHelper::restoreGeometryHint() const
{
    if (m_restoreGeometry.isValid()) {
        return m_restoreGeometry;
    }
    if (m_targetWidget && m_targetWidget->isMaximized()) {
        const QRect normal = m_targetWidget->normalGeometry();
        if (normal.isValid()) {
            return normal;
        }
    }
    if (m_targetWidget) {
        return QRect(m_targetWidget->pos(),
                     m_targetWidget->minimumSizeHint().expandedTo(QSize(800, 560)));
    }
    return {};
}

void FramelessHelper::maximizeOnScreen(QScreen *screen)
{
    if (!m_targetWidget || !screen) {
        return;
    }

    if (!isTiled()) {
        m_restoreGeometry = m_targetWidget->geometry();
    }

    const QRect avail = screen->availableGeometry();
    if (m_targetWidget->isMaximized()) {
        m_targetWidget->showNormal();
    }

    QRect seed = m_targetWidget->geometry();
    seed.moveCenter(avail.center());
    m_targetWidget->setGeometry(seed);
    m_targetWidget->showMaximized();
    if (!m_targetWidget->isMaximized()) {
        m_targetWidget->setGeometry(avail);
    }
    m_snapKind = SnapKind::Maximized;
    m_pendingSnap = SnapKind::None;
    m_pendingSnapScreen = nullptr;
}

void FramelessHelper::restoreToNormal(const QRect &geometry)
{
    if (!m_targetWidget) {
        return;
    }

    m_snapKind = SnapKind::None;
    m_pendingSnap = SnapKind::None;
    m_pendingSnapScreen = nullptr;
    if (m_targetWidget->isMaximized()) {
        m_targetWidget->showNormal();
    }
    if (geometry.isValid()) {
        m_targetWidget->setGeometry(geometry);
    }
}

void FramelessHelper::restoreForDrag(const QPoint &globalPos, const QPoint &localPos)
{
    if (!m_targetWidget) {
        return;
    }

    QRect restore = restoreGeometryHint();
    if (restore.width() < m_targetWidget->minimumWidth()) {
        restore.setWidth(m_targetWidget->minimumWidth());
    }
    if (restore.height() < m_targetWidget->minimumHeight()) {
        restore.setHeight(m_targetWidget->minimumHeight());
    }

    const int maxW = qMax(1, m_windowRectBeforeDrag.width());
    const int localX = qBound(0, localPos.x(), maxW);
    int offsetX = 0;
    if (localX < restore.width() / 2) {
        offsetX = localX;
    } else if (maxW - localX < restore.width() / 2) {
        offsetX = restore.width() - (maxW - localX);
    } else {
        offsetX = qRound(restore.width() * (qreal(localX) / qreal(maxW)));
    }
    offsetX = qBound(0, offsetX, restore.width());
    const int offsetY = qBound(0, localPos.y(), restore.height());
    restore.moveTopLeft(QPoint(globalPos.x() - offsetX, globalPos.y() - offsetY));

    restoreToNormal(restore);
    m_windowRectBeforeDrag = m_targetWidget->geometry();
    m_dragStartPos = globalPos;
}

void FramelessHelper::handleDragMove(const QPoint &globalPos)
{
    if (!m_targetWidget || m_currentRegion == None) {
        return;
    }

    if (m_currentRegion == Inner) {
        if (isTiled()) {
            const QPoint delta = globalPos - m_dragStartPos;
            if (delta.manhattanLength() < QApplication::startDragDistance()) {
                return;
            }
            restoreForDrag(globalPos, m_pressLocalPos);
        }

        const QPoint delta = globalPos - m_dragStartPos;
        m_targetWidget->move(m_windowRectBeforeDrag.topLeft() + delta);

        QScreen *screen = screenAt(globalPos);
        m_pendingSnap = SnapKind::None;
        m_pendingSnapScreen = nullptr;
        if (screen) {
            const QRect sg = screen->geometry();
            if (globalPos.y() <= sg.top() + m_snapThreshold) {
                m_pendingSnap = SnapKind::Maximized;
                m_pendingSnapScreen = screen;
            } else if (globalPos.x() <= sg.left() + m_snapThreshold) {
                m_pendingSnap = SnapKind::Left;
                m_pendingSnapScreen = screen;
            } else if (globalPos.x() >= sg.right() - m_snapThreshold) {
                m_pendingSnap = SnapKind::Right;
                m_pendingSnapScreen = screen;
            }
        }
        return;
    }

    if (isEffectivelyMaximized()) {
        return;
    }

    m_snapKind = SnapKind::None;
    QRect newGeometry = m_windowRectBeforeDrag;
    const QPoint delta = globalPos - m_dragStartPos;
    int x = newGeometry.x();
    int y = newGeometry.y();
    int w = newGeometry.width();
    int h = newGeometry.height();
    if (m_currentRegion == Left || m_currentRegion == TopLeft || m_currentRegion == BottomLeft) {
        x += delta.x();
        w -= delta.x();
    }
    if (m_currentRegion == Right || m_currentRegion == TopRight || m_currentRegion == BottomRight) {
        w += delta.x();
    }
    if (m_currentRegion == Top || m_currentRegion == TopLeft || m_currentRegion == TopRight) {
        y += delta.y();
        h -= delta.y();
    }
    if (m_currentRegion == Bottom || m_currentRegion == BottomLeft || m_currentRegion == BottomRight) {
        h += delta.y();
    }
    m_targetWidget->setGeometry(clampResizeGeometry(QRect(x, y, w, h)));
}

QRect FramelessHelper::clampResizeGeometry(const QRect &candidate) const
{
    if (!m_targetWidget) {
        return candidate;
    }

    int x = candidate.x();
    int y = candidate.y();
    int w = candidate.width();
    int h = candidate.height();
    const int minW = qMax(1, m_targetWidget->minimumWidth());
    const int minH = qMax(1, m_targetWidget->minimumHeight());
    const int maxW = m_targetWidget->maximumWidth();
    const int maxH = m_targetWidget->maximumHeight();

    const bool fromLeft = m_currentRegion == Left || m_currentRegion == TopLeft || m_currentRegion == BottomLeft;
    const bool fromTop = m_currentRegion == Top || m_currentRegion == TopLeft || m_currentRegion == TopRight;

    if (w < minW) {
        if (fromLeft) {
            x -= (minW - w);
        }
        w = minW;
    } else if (w > maxW) {
        if (fromLeft) {
            x += (w - maxW);
        }
        w = maxW;
    }
    if (h < minH) {
        if (fromTop) {
            y -= (minH - h);
        }
        h = minH;
    } else if (h > maxH) {
        if (fromTop) {
            y += (h - maxH);
        }
        h = maxH;
    }
    return QRect(x, y, w, h);
}

Qt::Edges FramelessHelper::edgesFromRegion(ResizeRegion region) const
{
    Qt::Edges edges;
    if (region == Left || region == TopLeft || region == BottomLeft) {
        edges |= Qt::LeftEdge;
    }
    if (region == Right || region == TopRight || region == BottomRight) {
        edges |= Qt::RightEdge;
    }
    if (region == Top || region == TopLeft || region == TopRight) {
        edges |= Qt::TopEdge;
    }
    if (region == Bottom || region == BottomLeft || region == BottomRight) {
        edges |= Qt::BottomEdge;
    }
    return edges;
}

void FramelessHelper::startInteraction(ResizeRegion region, const QPoint &localPos, const QPoint &globalPos)
{
    if (!m_targetWidget || region == None) {
        return;
    }

#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    if (QWindow *handle = m_targetWidget->windowHandle()) {
        if (region == Inner && !isTiled() && handle->startSystemMove()) {
            return;
        }
        if (region != Inner && handle->startSystemResize(edgesFromRegion(region))) {
            return;
        }
    }
#endif

    m_isPressed = true;
    m_currentRegion = region;
    m_pressLocalPos = localPos;
    m_dragStartPos = globalPos;
    m_windowRectBeforeDrag = m_targetWidget->geometry();
    m_pendingSnap = SnapKind::None;
    m_pendingSnapScreen = nullptr;
    if (!isTiled()) {
        m_restoreGeometry = m_windowRectBeforeDrag;
    }
    m_targetWidget->grabMouse();
}

void FramelessHelper::applyPendingSnap()
{
    if (!m_pendingSnapScreen || m_pendingSnap == SnapKind::None || !m_targetWidget) {
        return;
    }

    const QRect avail = m_pendingSnapScreen->availableGeometry();
    if (m_pendingSnap == SnapKind::Maximized) {
        maximizeOnScreen(m_pendingSnapScreen);
        return;
    }

    if (!isTiled()) {
        m_restoreGeometry = m_targetWidget->geometry();
    }
    if (m_targetWidget->isMaximized()) {
        m_targetWidget->showNormal();
    }

    QRect half = avail;
    half.setWidth(avail.width() / 2);
    if (m_pendingSnap == SnapKind::Right) {
        half.moveLeft(avail.left() + avail.width() - half.width());
    }
    m_targetWidget->setGeometry(half);
    m_snapKind = m_pendingSnap;
}

void FramelessHelper::finishInteraction(bool applySnap)
{
    if (m_targetWidget && QWidget::mouseGrabber() == m_targetWidget) {
        m_targetWidget->releaseMouse();
    }

    if (applySnap && m_currentRegion == Inner) {
        applyPendingSnap();
    } else if (m_targetWidget && m_currentRegion != None && m_currentRegion != Inner && !isTiled()) {
        m_restoreGeometry = m_targetWidget->geometry();
    } else if (m_targetWidget && m_currentRegion == Inner && !isTiled()) {
        m_restoreGeometry = m_targetWidget->geometry();
    }

    m_isPressed = false;
    m_currentRegion = None;
    m_pendingSnap = SnapKind::None;
    m_pendingSnapScreen = nullptr;
}

void FramelessHelper::applyNativeWindowStyle()
{
#ifdef Q_OS_WIN
    if (!m_targetWidget) {
        return;
    }
    HWND hwnd = reinterpret_cast<HWND>(m_targetWidget->winId());
    if (!hwnd) {
        return;
    }
    const LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
    SetWindowLongPtr(hwnd, GWL_STYLE,
                     style | WS_CAPTION | WS_THICKFRAME | WS_MAXIMIZEBOX | WS_MINIMIZEBOX | WS_SYSMENU);
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                 SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
#else
    Q_UNUSED(this);
#endif
}

bool FramelessHelper::handleNativeEvent(const QByteArray &eventType, void *message, qintptr *result)
{
#ifdef Q_OS_WIN
    if (!m_targetWidget || !message || !result) {
        return false;
    }
    if (eventType != "windows_generic_MSG" && eventType != "windows_dispatcher_MSG") {
        return false;
    }

    MSG *msg = static_cast<MSG *>(message);
    if (msg->hwnd != reinterpret_cast<HWND>(m_targetWidget->winId())) {
        return false;
    }

    switch (msg->message) {
    case WM_NCCALCSIZE:
        if (msg->wParam) {
            if (IsZoomed(msg->hwnd)) {
                auto *params = reinterpret_cast<NCCALCSIZE_PARAMS *>(msg->lParam);
                HMONITOR monitor = MonitorFromWindow(msg->hwnd, MONITOR_DEFAULTTONEAREST);
                MONITORINFO info;
                info.cbSize = sizeof(info);
                if (GetMonitorInfo(monitor, &info)) {
                    params->rgrc[0] = info.rcWork;
                }
            }
            *result = 0;
            return true;
        }
        break;
    case WM_NCHITTEST: {
        const QPoint local = m_targetWidget->mapFromGlobal(QCursor::pos());
        switch (getRegion(local)) {
        case Top: *result = HTTOP; break;
        case Bottom: *result = HTBOTTOM; break;
        case Left: *result = HTLEFT; break;
        case Right: *result = HTRIGHT; break;
        case TopLeft: *result = HTTOPLEFT; break;
        case TopRight: *result = HTTOPRIGHT; break;
        case BottomLeft: *result = HTBOTTOMLEFT; break;
        case BottomRight: *result = HTBOTTOMRIGHT; break;
        case Inner: *result = HTCAPTION; break;
        default: *result = HTCLIENT; break;
        }
        return true;
    }
    case WM_GETMINMAXINFO: {
        auto *mmi = reinterpret_cast<MINMAXINFO *>(msg->lParam);
        HMONITOR monitor = MonitorFromWindow(msg->hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO info;
        info.cbSize = sizeof(info);
        if (GetMonitorInfo(monitor, &info)) {
            const RECT &work = info.rcWork;
            const RECT &monitorRect = info.rcMonitor;
            mmi->ptMaxPosition.x = work.left - monitorRect.left;
            mmi->ptMaxPosition.y = work.top - monitorRect.top;
            mmi->ptMaxSize.x = work.right - work.left;
            mmi->ptMaxSize.y = work.bottom - work.top;
            *result = 0;
            return true;
        }
        break;
    }
    default:
        break;
    }
#else
    Q_UNUSED(eventType);
    Q_UNUSED(message);
    Q_UNUSED(result);
#endif
    return false;
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
bool FramelessHelper::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result)
{
    return handleNativeEvent(eventType, message, result);
}
#else
bool FramelessHelper::nativeEventFilter(const QByteArray &eventType, void *message, long *result)
{
    qintptr value = 0;
    const bool handled = handleNativeEvent(eventType, message, &value);
    if (handled && result) {
        *result = static_cast<long>(value);
    }
    return handled;
}
#endif

bool FramelessHelper::eventFilter(QObject *obj, QEvent *event)
{
    if (!isManagedWidget(obj)) {
        return QObject::eventFilter(obj, event);
    }

    if (obj == m_targetWidget) {
        switch (event->type()) {
        case QEvent::Show:
        case QEvent::WinIdChange:
            applyNativeWindowStyle();
            break;
        case QEvent::WindowStateChange:
            if (m_targetWidget->isMaximized()) {
                m_snapKind = SnapKind::Maximized;
                if (!m_restoreGeometry.isValid()) {
                    m_restoreGeometry = m_targetWidget->normalGeometry();
                }
            } else if (!m_isPressed && m_snapKind == SnapKind::Maximized) {
                m_snapKind = SnapKind::None;
            }
            break;
        case QEvent::Hide:
        case QEvent::Close:
            finishInteraction(false);
            break;
        default:
            break;
        }
    }

    if (useNativeHitTest()) {
        return QObject::eventFilter(obj, event);
    }

    switch (event->type()) {
    case QEvent::HoverMove: {
        if (!m_isPressed) {
            updateCursorShape(mapToTarget(obj, localPosFromEvent(event)));
        }
        break;
    }
    case QEvent::Leave: {
        if (!m_isPressed && obj == m_targetWidget) {
            m_targetWidget->unsetCursor();
        }
        break;
    }
    case QEvent::MouseButtonPress: {
        const auto *mouseEvent = static_cast<const QMouseEvent *>(event);
        if (mouseEvent->button() != Qt::LeftButton) {
            break;
        }
        const QPoint localPos = mapToTarget(obj, localPosFromEvent(event));
        const ResizeRegion region = getRegion(localPos);
        if (region != None) {
            startInteraction(region, localPos, globalPosFromEvent(event));
            return true;
        }
        break;
    }
    case QEvent::MouseButtonDblClick: {
        const auto *mouseEvent = static_cast<const QMouseEvent *>(event);
        if (mouseEvent->button() != Qt::LeftButton) {
            break;
        }
        const QPoint localPos = mapToTarget(obj, localPosFromEvent(event));
        if (getRegion(localPos) == Inner) {
            toggleMaximized();
            finishInteraction(false);
            return true;
        }
        break;
    }
    case QEvent::MouseMove: {
        if (!m_isPressed || m_currentRegion == None) {
            updateCursorShape(mapToTarget(obj, localPosFromEvent(event)));
            break;
        }
        handleDragMove(globalPosFromEvent(event));
        return true;
    }
    case QEvent::MouseButtonRelease: {
        const auto *mouseEvent = static_cast<const QMouseEvent *>(event);
        if (mouseEvent->button() != Qt::LeftButton) {
            break;
        }
        if (m_isPressed || m_currentRegion != None) {
            finishInteraction(true);
            return true;
        }
        break;
    }
    default:
        break;
    }

    return QObject::eventFilter(obj, event);
}
