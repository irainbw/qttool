#include "framelesshelper.h"

#include <QAbstractNativeEventFilter>
#include <QApplication>
#include <QCursor>
#include <QEvent>
#include <QGuiApplication>
#include <QHoverEvent>
#include <QMouseEvent>
#include <QScreen>
#include <QWidget>
#include <QWindow>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <QHash>
#endif

namespace {

QPoint localPosOf(const QEvent *event)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    if (event->type() == QEvent::HoverMove)
        return static_cast<const QHoverEvent *>(event)->position().toPoint();
    return static_cast<const QMouseEvent *>(event)->position().toPoint();
#else
    if (event->type() == QEvent::HoverMove)
        return static_cast<const QHoverEvent *>(event)->pos();
    return static_cast<const QMouseEvent *>(event)->pos();
#endif
}

QPoint globalPosOf(const QEvent *event)
{
    if (event->type() == QEvent::HoverMove)
        return QCursor::pos();
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return static_cast<const QMouseEvent *>(event)->globalPosition().toPoint();
#else
    return static_cast<const QMouseEvent *>(event)->globalPos();
#endif
}

} // namespace

#ifdef Q_OS_WIN

class FramelessNativeHub final : public QAbstractNativeEventFilter
{
public:
    static FramelessNativeHub &instance()
    {
        static FramelessNativeHub hub;
        return hub;
    }

    void attach(void *hwnd, FramelessHelper *helper)
    {
        if (!hwnd || !helper)
            return;
        if (m_map.isEmpty() && qApp)
            qApp->installNativeEventFilter(this);
        m_map.insert(hwnd, helper);
    }

    void detach(void *hwnd, FramelessHelper *helper)
    {
        if (!hwnd)
            return;
        if (m_map.value(hwnd) == helper)
            m_map.remove(hwnd);
        if (m_map.isEmpty() && qApp)
            qApp->removeNativeEventFilter(this);
    }

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override
#else
    bool nativeEventFilter(const QByteArray &eventType, void *message, long *result) override
#endif
    {
        if (!message || m_map.isEmpty())
            return false;
        if (eventType != "windows_generic_MSG" && eventType != "windows_dispatcher_MSG")
            return false;

        const HWND hwnd = static_cast<MSG *>(message)->hwnd;
        FramelessHelper *helper = m_map.value(hwnd, nullptr);
        if (!helper)
            return false;

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        return helper->handleNativeEvent(message, result);
#else
        qintptr value = 0;
        const bool handled = helper->handleNativeEvent(message, &value);
        if (handled && result)
            *result = static_cast<long>(value);
        return handled;
#endif
    }

private:
    QHash<void *, FramelessHelper *> m_map;
};

#endif // Q_OS_WIN

FramelessHelper::FramelessHelper(QObject *parent)
    : QObject(parent)
{
}

FramelessHelper::~FramelessHelper()
{
    onRelease(false);
#ifdef Q_OS_WIN
    FramelessNativeHub::instance().detach(m_hwnd, this);
#endif
}

void FramelessHelper::activateOn(QWidget *topLevelWidget)
{
    if (m_window)
        m_window->removeEventFilter(this);

    m_window = topLevelWidget;
    m_titleBarRectDirty = true;
#ifdef Q_OS_WIN
    FramelessNativeHub::instance().detach(m_hwnd, this);
    m_hwnd = nullptr;
    m_chromeApplied = false;
#endif
    if (!m_window)
        return;

    const bool visible = m_window->isVisible();
    m_window->setWindowFlags(m_window->windowFlags()
                             | Qt::FramelessWindowHint
                             | Qt::WindowMinMaxButtonsHint
                             | Qt::WindowSystemMenuHint);
    m_window->setAttribute(Qt::WA_Hover, true);
    m_window->setMouseTracking(true);
    m_window->installEventFilter(this);
    if (!isSystemChromeEnabled())
        watch(m_window);

    if (visible)
        m_window->show();
}

void FramelessHelper::setTitleBar(QWidget *titleBarWidget)
{
    m_titleBar = titleBarWidget;
    m_titleBarRectDirty = true;
    if (m_titleBar && !isSystemChromeEnabled())
        watch(m_titleBar);
}

void FramelessHelper::setSystemChromeEnabled(bool enabled)
{
#ifdef Q_OS_WIN
    if (m_systemChromeEnabled == enabled)
        return;
    m_systemChromeEnabled = enabled;
    if (enabled)
        applyNativeChrome();
    else
        removeNativeChrome();
#else
    Q_UNUSED(enabled);
#endif
}

bool FramelessHelper::isSystemChromeEnabled() const
{
#ifdef Q_OS_WIN
    return m_systemChromeEnabled;
#else
    return false;
#endif
}

void FramelessHelper::watch(QWidget *widget)
{
    if (!widget)
        return;
    widget->removeEventFilter(this);
    widget->installEventFilter(this);
    widget->setMouseTracking(true);
    const auto children = widget->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly);
    for (QWidget *child : children)
        watch(child);
}

bool FramelessHelper::nativeMoveResizeActive() const
{
#ifdef Q_OS_WIN
    return m_systemChromeEnabled && m_hwnd && m_chromeApplied;
#else
    return false;
#endif
}

void FramelessHelper::syncNativeHandle()
{
#ifdef Q_OS_WIN
    void *hwnd = m_window ? reinterpret_cast<void *>(m_window->internalWinId()) : nullptr;
    if (hwnd == m_hwnd)
        return;
    FramelessNativeHub::instance().detach(m_hwnd, this);
    m_hwnd = hwnd;
    if (m_systemChromeEnabled)
        FramelessNativeHub::instance().attach(m_hwnd, this);
#endif
}

void FramelessHelper::applyNativeChrome()
{
#ifdef Q_OS_WIN
    if (!m_systemChromeEnabled)
        return;
    syncNativeHandle();
    auto *hwnd = static_cast<HWND>(m_hwnd);
    if (!hwnd || m_chromeApplied)
        return;
    const LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
    SetWindowLongPtr(hwnd, GWL_STYLE,
                     style | WS_CAPTION | WS_THICKFRAME | WS_MAXIMIZEBOX | WS_MINIMIZEBOX | WS_SYSMENU);
    m_chromeApplied = true;
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                 SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
#endif
}

void FramelessHelper::removeNativeChrome()
{
#ifdef Q_OS_WIN
    FramelessNativeHub::instance().detach(m_hwnd, this);
    auto *hwnd = static_cast<HWND>(m_hwnd);
    if (hwnd && m_chromeApplied) {
        LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
        style &= ~(WS_CAPTION | WS_THICKFRAME);
        SetWindowLongPtr(hwnd, GWL_STYLE, style);
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                     SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    m_chromeApplied = false;
    if (m_window)
        watch(m_window);
    if (m_titleBar)
        watch(m_titleBar);
#endif
}

bool FramelessHelper::isMaximizedState() const
{
    return m_window && (m_pseudoMaximized || m_window->isMaximized());
}

void FramelessHelper::toggleMaximized()
{
    if (!m_window)
        return;
    if (isMaximizedState())
        restoreToNormal();
    else
        maximizeOnScreen(screenAt(QCursor::pos()));
}

QRect FramelessHelper::titleBarRect() const
{
    if (!m_window || !m_titleBar)
        return {};
    if (!m_titleBarRectDirty)
        return m_titleBarRectCache;
    m_titleBarRectCache = QRect(m_titleBar->mapTo(m_window, QPoint(0, 0)), m_titleBar->size());
    m_titleBarRectDirty = false;
    return m_titleBarRectCache;
}

bool FramelessHelper::isCaptionButton(const QWidget *widget) const
{
    if (!widget || widget == m_window || widget == m_titleBar)
        return false;
    return widget->inherits("QAbstractButton")
        || widget->inherits("QComboBox")
        || widget->inherits("QLineEdit")
        || widget->inherits("QAbstractSlider")
        || widget->inherits("QAbstractSpinBox");
}

QScreen *FramelessHelper::screenAt(const QPoint &globalPos) const
{
    if (QScreen *screen = QGuiApplication::screenAt(globalPos))
        return screen;
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    if (m_window && m_window->screen())
        return m_window->screen();
#endif
    return QGuiApplication::primaryScreen();
}

FramelessHelper::ResizeRegion FramelessHelper::hitTest(const QPoint &pos) const
{
    if (!m_window || m_window->isFullScreen())
        return None;

    const int x = pos.x();
    const int y = pos.y();
    const int w = m_window->width();
    const int h = m_window->height();

    if (!isMaximizedState()) {
        const bool onLeft = x < kBorder;
        const bool onRight = x >= w - kBorder;
        const bool onTop = y < kBorder;
        const bool onBottom = y >= h - kBorder;
        if (onLeft || onRight || onTop || onBottom) {
            if (onTop && onLeft) return TopLeft;
            if (onTop && onRight) return TopRight;
            if (onBottom && onLeft) return BottomLeft;
            if (onBottom && onRight) return BottomRight;
            if (onLeft) return Left;
            if (onRight) return Right;
            if (onTop) return Top;
            return Bottom;
        }
    }

    if (m_titleBar) {
        if (!titleBarRect().contains(pos))
            return None;
        if (QWidget *child = m_window->childAt(pos)) {
            for (QWidget *it = child; it && it != m_window; it = it->parentWidget()) {
                if (isCaptionButton(it))
                    return None;
            }
        }
        return Inner;
    }

    if (QWidget *child = m_window->childAt(pos)) {
        for (QWidget *it = child; it && it != m_window; it = it->parentWidget()) {
            if (isCaptionButton(it))
                return None;
        }
    }
    return Inner;
}

void FramelessHelper::updateCursor(ResizeRegion region)
{
    if (!m_window)
        return;
    Qt::CursorShape shape = Qt::ArrowCursor;
    switch (region) {
    case TopLeft:
    case BottomRight:
        shape = Qt::SizeFDiagCursor;
        break;
    case TopRight:
    case BottomLeft:
        shape = Qt::SizeBDiagCursor;
        break;
    case Left:
    case Right:
        shape = Qt::SizeHorCursor;
        break;
    case Top:
    case Bottom:
        shape = Qt::SizeVerCursor;
        break;
    default:
        break;
    }
    if (shape == m_lastCursor)
        return;
    m_lastCursor = shape;
    if (shape == Qt::ArrowCursor)
        m_window->unsetCursor();
    else
        m_window->setCursor(shape);
}

void FramelessHelper::maximizeOnScreen(QScreen *screen)
{
    if (!m_window || !screen)
        return;
    if (!isMaximizedState())
        m_restoreGeometry = m_window->geometry();

    if (m_window->isMaximized())
        m_window->showNormal();

    const QRect avail = screen->availableGeometry();
    QRect seed = m_window->geometry();
    seed.moveCenter(avail.center());
    m_window->setGeometry(seed);
    m_window->showMaximized();
    if (!m_window->isMaximized()) {
        m_window->setGeometry(avail);
        m_pseudoMaximized = true;
    } else {
        m_pseudoMaximized = false;
    }
}

void FramelessHelper::restoreToNormal()
{
    if (!m_window)
        return;
    m_pseudoMaximized = false;
    if (m_window->isMaximized())
        m_window->showNormal();
    if (m_restoreGeometry.isValid())
        m_window->setGeometry(m_restoreGeometry);
}

void FramelessHelper::onPress(ResizeRegion region, const QPoint &localPos, const QPoint &globalPos)
{
    if (!m_window || region == None)
        return;

#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    if (QWindow *handle = m_window->windowHandle()) {
        if (region == Inner && !isMaximizedState() && handle->startSystemMove())
            return;
        Qt::Edges edges;
        if (region == Left || region == TopLeft || region == BottomLeft) edges |= Qt::LeftEdge;
        if (region == Right || region == TopRight || region == BottomRight) edges |= Qt::RightEdge;
        if (region == Top || region == TopLeft || region == TopRight) edges |= Qt::TopEdge;
        if (region == Bottom || region == BottomLeft || region == BottomRight) edges |= Qt::BottomEdge;
        if (region != Inner && handle->startSystemResize(edges))
            return;
    }
#endif

    m_pressed = true;
    m_region = region;
    m_pressLocal = localPos;
    m_pressGlobal = globalPos;
    m_pressGeometry = m_window->geometry();
    if (!isMaximizedState())
        m_restoreGeometry = m_pressGeometry;
    m_window->grabMouse();
}

void FramelessHelper::onMove(const QPoint &globalPos)
{
    if (!m_window || !m_pressed || m_region == None)
        return;

    if (m_region == Inner) {
        if (isMaximizedState()) {
            if ((globalPos - m_pressGlobal).manhattanLength() < QApplication::startDragDistance())
                return;
            QRect restore = m_restoreGeometry.isValid()
                                ? m_restoreGeometry
                                : QRect(QPoint(0, 0), m_window->sizeHint().expandedTo(QSize(800, 560)));
            const int maxW = qMax(1, m_pressGeometry.width());
            int offsetX = qRound(restore.width() * qreal(qBound(0, m_pressLocal.x(), maxW)) / qreal(maxW));
            offsetX = qBound(0, offsetX, restore.width());
            restore.moveTopLeft(QPoint(globalPos.x() - offsetX,
                                       globalPos.y() - qBound(0, m_pressLocal.y(), restore.height())));
            m_pseudoMaximized = false;
            if (m_window->isMaximized())
                m_window->showNormal();
            m_window->setGeometry(restore);
            m_pressGeometry = m_window->geometry();
            m_pressGlobal = globalPos;
        }
        m_window->move(m_pressGeometry.topLeft() + (globalPos - m_pressGlobal));
        return;
    }

    if (isMaximizedState())
        return;

    const QPoint d = globalPos - m_pressGlobal;
    int x = m_pressGeometry.x();
    int y = m_pressGeometry.y();
    int w = m_pressGeometry.width();
    int h = m_pressGeometry.height();
    if (m_region == Left || m_region == TopLeft || m_region == BottomLeft) { x += d.x(); w -= d.x(); }
    if (m_region == Right || m_region == TopRight || m_region == BottomRight) { w += d.x(); }
    if (m_region == Top || m_region == TopLeft || m_region == TopRight) { y += d.y(); h -= d.y(); }
    if (m_region == Bottom || m_region == BottomLeft || m_region == BottomRight) { h += d.y(); }
    m_window->setGeometry(clampResize(QRect(x, y, w, h)));
}

QRect FramelessHelper::clampResize(const QRect &rect) const
{
    if (!m_window)
        return rect;
    int x = rect.x(), y = rect.y(), w = rect.width(), h = rect.height();
    const int minW = qMax(1, m_window->minimumWidth());
    const int minH = qMax(1, m_window->minimumHeight());
    const int maxW = m_window->maximumWidth();
    const int maxH = m_window->maximumHeight();
    const bool fromLeft = m_region == Left || m_region == TopLeft || m_region == BottomLeft;
    const bool fromTop = m_region == Top || m_region == TopLeft || m_region == TopRight;
    if (w < minW) { if (fromLeft) x -= (minW - w); w = minW; }
    else if (w > maxW) { if (fromLeft) x += (w - maxW); w = maxW; }
    if (h < minH) { if (fromTop) y -= (minH - h); h = minH; }
    else if (h > maxH) { if (fromTop) y += (h - maxH); h = maxH; }
    return QRect(x, y, w, h);
}

void FramelessHelper::onRelease(bool applySnap)
{
    if (m_window && QWidget::mouseGrabber() == m_window)
        m_window->releaseMouse();

    const bool draggingCaption = m_pressed && m_region == Inner && m_window && !isMaximizedState();
    m_pressed = false;
    m_region = None;

    if (!applySnap || !draggingCaption)
        return;

    const QPoint pos = QCursor::pos();
    QScreen *screen = screenAt(pos);
    if (!screen)
        return;

    const QRect sg = screen->geometry();
    if (pos.y() <= sg.top() + kBorder) {
        maximizeOnScreen(screen);
        return;
    }

    const QRect avail = screen->availableGeometry();
    QRect half = avail;
    half.setWidth(qMax(m_window->minimumWidth(), avail.width() / 2));
    if (pos.x() <= sg.left() + kBorder) {
        half.moveLeft(avail.left());
        m_window->setGeometry(half);
        return;
    }
    if (pos.x() >= sg.right() - kBorder) {
        half.moveRight(avail.right());
        m_window->setGeometry(half);
        return;
    }

    m_restoreGeometry = m_window->geometry();
}

bool FramelessHelper::handleNativeEvent(void *message, qintptr *result)
{
#ifdef Q_OS_WIN
    if (m_inNative || !m_systemChromeEnabled || !m_hwnd || !message || !result)
        return false;

    const MSG *msg = static_cast<const MSG *>(message);
    if (msg->hwnd != static_cast<HWND>(m_hwnd))
        return false;

    switch (msg->message) {
    case WM_NCCALCSIZE:
        m_inNative = true;
        if (msg->wParam && IsZoomed(msg->hwnd)) {
            auto *params = reinterpret_cast<NCCALCSIZE_PARAMS *>(msg->lParam);
            MONITORINFO info;
            info.cbSize = sizeof(info);
            if (GetMonitorInfo(MonitorFromWindow(msg->hwnd, MONITOR_DEFAULTTONEAREST), &info))
                params->rgrc[0] = info.rcWork;
        }
        *result = 0;
        m_inNative = false;
        return true;
    case WM_NCHITTEST: {
        if (!m_window)
            return false;
        m_inNative = true;
        LRESULT hit = HTCLIENT;
        switch (hitTest(m_window->mapFromGlobal(QCursor::pos()))) {
        case Top: hit = HTTOP; break;
        case Bottom: hit = HTBOTTOM; break;
        case Left: hit = HTLEFT; break;
        case Right: hit = HTRIGHT; break;
        case TopLeft: hit = HTTOPLEFT; break;
        case TopRight: hit = HTTOPRIGHT; break;
        case BottomLeft: hit = HTBOTTOMLEFT; break;
        case BottomRight: hit = HTBOTTOMRIGHT; break;
        case Inner: hit = HTCAPTION; break;
        default: break;
        }
        *result = hit;
        m_inNative = false;
        return true;
    }
    case WM_GETMINMAXINFO: {
        auto *mmi = reinterpret_cast<MINMAXINFO *>(msg->lParam);
        MONITORINFO info;
        info.cbSize = sizeof(info);
        if (!GetMonitorInfo(MonitorFromWindow(msg->hwnd, MONITOR_DEFAULTTONEAREST), &info))
            return false;
        mmi->ptMaxPosition.x = info.rcWork.left - info.rcMonitor.left;
        mmi->ptMaxPosition.y = info.rcWork.top - info.rcMonitor.top;
        mmi->ptMaxSize.x = info.rcWork.right - info.rcWork.left;
        mmi->ptMaxSize.y = info.rcWork.bottom - info.rcWork.top;
        if (m_window) {
            const qreal dpr = m_window->devicePixelRatioF();
            mmi->ptMinTrackSize.x = qRound(m_window->minimumWidth() * dpr);
            mmi->ptMinTrackSize.y = qRound(m_window->minimumHeight() * dpr);
        }
        *result = 0;
        return true;
    }
    default:
        return false;
    }
#else
    Q_UNUSED(message);
    Q_UNUSED(result);
    return false;
#endif
}

bool FramelessHelper::eventFilter(QObject *obj, QEvent *event)
{
    if (!m_window)
        return false;

    const QEvent::Type type = event->type();

    // 高频无关事件：指针比较都不做
    if (type == QEvent::Paint || type == QEvent::Timer || type == QEvent::UpdateRequest)
        return false;

    if (obj == m_window) {
        switch (type) {
        case QEvent::WinIdChange: {
#ifdef Q_OS_WIN
            void *previous = m_hwnd;
            syncNativeHandle();
            if (m_hwnd != previous)
                m_chromeApplied = false;
#endif
            applyNativeChrome();
            m_titleBarRectDirty = true;
            return false;
        }
        case QEvent::Show:
            applyNativeChrome();
            return false;
        case QEvent::Resize:
        case QEvent::Move:
            m_titleBarRectDirty = true;
            return false;
        case QEvent::WindowStateChange:
            m_titleBarRectDirty = true;
            if (m_window->isMaximized()) {
                m_pseudoMaximized = false;
                if (!m_restoreGeometry.isValid())
                    m_restoreGeometry = m_window->normalGeometry();
            }
            return false;
        case QEvent::Hide:
        case QEvent::Close:
            onRelease(false);
            return false;
        case QEvent::Leave:
            if (!m_pressed)
                updateCursor(None);
            return false;
        default:
            break;
        }
    }

    if (nativeMoveResizeActive())
        return false;

    switch (type) {
    case QEvent::HoverMove:
    case QEvent::MouseMove:
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonDblClick:
    case QEvent::MouseButtonRelease:
        break;
    default:
        return false;
    }

    auto *widget = static_cast<QWidget *>(obj);
    const QPoint local = widget->mapTo(m_window, localPosOf(event));

    switch (type) {
    case QEvent::HoverMove:
    case QEvent::MouseMove:
        if (m_pressed) {
            onMove(globalPosOf(event));
            return true;
        }
        updateCursor(isMaximizedState() ? None : hitTest(local));
        return false;
    case QEvent::MouseButtonPress: {
        if (static_cast<const QMouseEvent *>(event)->button() != Qt::LeftButton)
            return false;
        const ResizeRegion region = hitTest(local);
        if (region == None)
            return false;
        onPress(region, local, globalPosOf(event));
        return true;
    }
    case QEvent::MouseButtonDblClick:
        if (static_cast<const QMouseEvent *>(event)->button() != Qt::LeftButton
            || hitTest(local) != Inner) {
            return false;
        }
        toggleMaximized();
        onRelease(false);
        return true;
    case QEvent::MouseButtonRelease:
        if (static_cast<const QMouseEvent *>(event)->button() != Qt::LeftButton || !m_pressed)
            return false;
        onRelease(true);
        return true;
    default:
        return false;
    }
}
