#pragma once
#include <QObject>
#include <QFont>

// Resolve the actual fallback ascent used by Qt's text shaper. Primary-font
// metrics alone do not describe Android bitmap emoji runs.
class TextGeometry : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    Q_INVOKABLE qreal lineAscent(const QString &text, const QFont &font,
                                qreal width, int number, bool wrap) const;
};
