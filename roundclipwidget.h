#ifndef ROUNDCLIPWIDGET_H
#define ROUNDCLIPWIDGET_H

#pragma once

#include <QColor>
#include <QGraphicsEffect>
#include <QImage>
#include <QWidget>

class RoundClipEffect : public QGraphicsEffect
{
public:
    explicit RoundClipEffect(QObject *parent = nullptr);

    void setRadius(qreal radius);
    qreal radius() const { return m_radius; }

protected:
    void draw(QPainter *painter) override;

private:
    void ensureMask(const QSize &pixelSize, qreal dpr);

    qreal m_radius = 0;
    qreal m_maskDpr = 0;
    QImage m_mask;
};

class RoundClipWidget : public QWidget
{
public:
    explicit RoundClipWidget(QWidget *parent = nullptr);

    void setRadius(int radius);
    int radius() const { return m_radius; }

    void setFillColor(const QColor &color);
    QColor fillColor() const { return m_fill; }

protected:
    void changeEvent(QEvent *event) override;
    // void paintEvent(QPaintEvent *event) override;

private:
    int effectiveRadius() const;
    void applyRadius();

    int m_radius = 12;
    QColor m_fill = QColor(QStringLiteral("#2b2b2b"));
    RoundClipEffect *m_effect = nullptr;
};

#endif // ROUNDCLIPWIDGET_H
