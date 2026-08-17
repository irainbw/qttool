#include "roundclipwidget.h"

#include <QEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPixmap>
#include <QTransform>

RoundClipEffect::RoundClipEffect(QObject *parent)
    : QGraphicsEffect(parent)
{
}

void RoundClipEffect::setRadius(qreal radius)
{
    radius = qMax(qreal(0), radius);
    if (qFuzzyCompare(m_radius, radius))
        return;
    m_radius = radius;
    m_mask = QImage();
    update();
}

void RoundClipEffect::draw(QPainter *painter)
{
    QPoint offset;
    const QPixmap src = sourcePixmap(Qt::DeviceCoordinates, &offset, QGraphicsEffect::NoPad);
    if (src.isNull())
        return;

    const QTransform old = painter->worldTransform();
    painter->setWorldTransform(QTransform());

    if (m_radius <= 0.5) {
        painter->drawPixmap(offset, src);
        painter->setWorldTransform(old);
        return;
    }

    const qreal dpr = src.devicePixelRatio();
    ensureMask(src.size(), dpr);

    QImage dst = src.toImage();
    if (dst.format() != QImage::Format_ARGB32_Premultiplied)
        dst = dst.convertToFormat(QImage::Format_ARGB32_Premultiplied);

    QPainter blend(&dst);
    blend.setCompositionMode(QPainter::CompositionMode_DestinationIn);
    blend.drawImage(0, 0, m_mask);
    blend.end();

    painter->drawImage(offset, dst);
    painter->setWorldTransform(old);
}

void RoundClipEffect::ensureMask(const QSize &pixelSize, qreal dpr)
{
    if (m_mask.size() == pixelSize && qFuzzyCompare(m_maskDpr, dpr))
        return;

    m_maskDpr = dpr;
    m_mask = QImage(pixelSize, QImage::Format_ARGB32_Premultiplied);
    m_mask.fill(Qt::transparent);

    QPainter p(&m_mask);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(Qt::white);
    p.drawRoundedRect(QRectF(0.5, 0.5, pixelSize.width() - 1.0, pixelSize.height() - 1.0),
                      m_radius * dpr, m_radius * dpr);
}

RoundClipWidget::RoundClipWidget(QWidget *parent)
    : QWidget(parent)
    , m_effect(new RoundClipEffect(this))
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_StyledBackground, true);
    setAutoFillBackground(false);
    setGraphicsEffect(m_effect);
    m_effect->setRadius(m_radius);
}

void RoundClipWidget::setRadius(int radius)
{
    radius = qMax(0, radius);
    if (m_radius == radius)
        return;
    m_radius = radius;
    applyRadius();
    update();
}

void RoundClipWidget::setFillColor(const QColor &color)
{
    if (m_fill == color)
        return;
    m_fill = color;
    update();
}

void RoundClipWidget::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange)
        applyRadius();
}

void RoundClipWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), m_fill);
}

int RoundClipWidget::effectiveRadius() const
{
    if (isWindow() && (isMaximized() || isFullScreen()))
        return 0;
    return m_radius;
}

void RoundClipWidget::applyRadius()
{
    m_effect->setRadius(effectiveRadius());
}
