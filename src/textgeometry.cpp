#include "textgeometry.h"
#include <QCoreApplication>
#include <QFontMetricsF>
#include <QTextLayout>
#include <QtQml/qqml.h>
#include <algorithm>

qreal TextGeometry::lineAscent(const QString &text, const QFont &font,
                               qreal width, int number, bool wrap) const
{
    const QFontMetricsF metrics(font);
    const auto display = wrap ? text : metrics.elidedText(text, Qt::ElideRight, width);
    QTextLayout layout(display, font);
    QTextOption option;
    option.setWrapMode(wrap ? QTextOption::WrapAtWordBoundaryOrAnywhere : QTextOption::NoWrap);
    option.setUseDesignMetrics(true);
    layout.setTextOption(option);
    layout.beginLayout();
    qreal ascent = metrics.ascent();
    for (int index = 0; index <= std::clamp(number, 0, 100); ++index) {
        auto line = layout.createLine();
        if (!line.isValid()) break;
        line.setLineWidth(std::max(qreal(1), width));
        ascent = line.ascent();
    }
    layout.endLayout();
    return ascent;
}

static void registerTextGeometry()
{
    qmlRegisterSingletonType<TextGeometry>("SCrip.Native", 1, 0, "TextGeometry",
        [](QQmlEngine *, QJSEngine *) -> QObject * { return new TextGeometry; });
}
Q_COREAPP_STARTUP_FUNCTION(registerTextGeometry)
