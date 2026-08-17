#ifndef FRAMELESSHELPER_H
#define FRAMELESSHELPER_H

#pragma once

#include <QObject>
#include <QPoint>
#include <QPointer>
#include <QRect>

class QEvent;
class QScreen;
class QWidget;
class FramelessNativeHub;

class FramelessHelper : public QObject
{
    Q_OBJECT
    friend class FramelessNativeHub;
public:
    explicit FramelessHelper(QObject *parent = nullptr);
    ~FramelessHelper() override;

    void activateOn(QWidget *topLevelWidget);
    void setTitleBar(QWidget *titleBarWidget);
    void setSystemChromeEnabled(bool enabled);
    bool isSystemChromeEnabled() const;
    void toggleMaximized();
    bool isMaximizedState() const;

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    enum ResizeRegion {
        None, Top, Bottom, Left, Right,
        TopLeft, TopRight, BottomLeft, BottomRight, Inner
    };

    void watch(QWidget *widget);
    void syncNativeHandle();
    void applyNativeChrome();
    void removeNativeChrome();
    bool nativeMoveResizeActive() const;
    bool handleNativeEvent(void *message, qintptr *result);
    ResizeRegion hitTest(const QPoint &posInTarget) const;
    QRect titleBarRect() const;
    bool isCaptionButton(const QWidget *widget) const;
    QScreen *screenAt(const QPoint &globalPos) const;
    void maximizeOnScreen(QScreen *screen);
    void restoreToNormal();
    void updateCursor(ResizeRegion region);
    void onPress(ResizeRegion region, const QPoint &localPos, const QPoint &globalPos);
    void onMove(const QPoint &globalPos);
    void onRelease(bool applySnap);
    QRect clampResize(const QRect &rect) const;

    static constexpr int kBorder = 8;

    QPointer<QWidget> m_window;
    QPointer<QWidget> m_titleBar;
    QRect m_restoreGeometry;
    mutable QRect m_titleBarRectCache;
    mutable bool m_titleBarRectDirty = true;
    bool m_pseudoMaximized = false;
    Qt::CursorShape m_lastCursor = Qt::ArrowCursor;

#ifdef Q_OS_WIN
    void *m_hwnd = nullptr;
    bool m_chromeApplied = false;
    bool m_inNative = false;
    bool m_systemChromeEnabled = true;
#endif

    bool m_pressed = false;
    ResizeRegion m_region = None;
    QPoint m_pressGlobal;
    QPoint m_pressLocal;
    QRect m_pressGeometry;
};

#endif // FRAMELESSHELPER_H
