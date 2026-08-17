#ifndef FRAMELESSHELPER_H
#define FRAMELESSHELPER_H

#pragma once

#include <QAbstractNativeEventFilter>
#include <QObject>
#include <QPoint>
#include <QPointer>
#include <QRect>

class QEvent;
class QScreen;
class QWidget;

class FramelessHelper : public QObject, public QAbstractNativeEventFilter
{
    Q_OBJECT
public:
    explicit FramelessHelper(QObject *parent = nullptr);
    ~FramelessHelper() override;

    void activateOn(QWidget *topLevelWidget);
    void setTitleBar(QWidget *titleBarWidget);
    void toggleMaximized();
    bool isMaximizedState() const;

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;
#else
    bool nativeEventFilter(const QByteArray &eventType, void *message, long *result) override;
#endif

private:
    enum ResizeRegion {
        None, Top, Bottom, Left, Right,
        TopLeft, TopRight, BottomLeft, BottomRight, Inner
    };
    enum class SnapKind { None, Maximized, Left, Right };

    bool isManagedWidget(const QObject *obj) const;
    bool useNativeHitTest() const;
    ResizeRegion getRegion(const QPoint &posInTarget) const;
    void updateCursorShape(const QPoint &posInTarget);
    QPoint mapToTarget(const QObject *watched, const QPoint &pos) const;
    QRect titleBarRectInTarget() const;
    bool isInteractiveWidget(const QWidget *widget) const;
    bool isEffectivelyMaximized() const;
    bool isTiled() const;
    QScreen *screenAt(const QPoint &globalPos) const;
    void maximizeOnScreen(QScreen *screen);
    void restoreToNormal(const QRect &geometry);
    void restoreForDrag(const QPoint &globalPos, const QPoint &localPos);
    void handleDragMove(const QPoint &globalPos);
    void startInteraction(ResizeRegion region, const QPoint &localPos, const QPoint &globalPos);
    void finishInteraction(bool applySnap);
    QRect clampResizeGeometry(const QRect &candidate) const;
    QRect restoreGeometryHint() const;
    void applyPendingSnap();
    void applyNativeWindowStyle();
    bool handleNativeEvent(const QByteArray &eventType, void *message, qintptr *result);
    Qt::Edges edgesFromRegion(ResizeRegion region) const;

    int m_padding = 8;
    int m_snapThreshold = 8;
    bool m_isPressed = false;
    SnapKind m_snapKind = SnapKind::None;
    QPoint m_dragStartPos;
    QPoint m_pressLocalPos;
    QRect m_windowRectBeforeDrag;
    QRect m_restoreGeometry;
    ResizeRegion m_currentRegion = None;
    SnapKind m_pendingSnap = SnapKind::None;
    QPointer<QScreen> m_pendingSnapScreen;

    QPointer<QWidget> m_targetWidget;
    QPointer<QWidget> m_titleBarWidget;
};

#endif // FRAMELESSHELPER_H
