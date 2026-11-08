import QtQuick

Canvas {
        property real radius: 8
        onRadiusChanged: requestPaint()
        onHeightChanged: requestPaint()
        property string source: ""
        onSourceChanged: { if (source) loadImage(source); requestPaint() }
        onAvailableChanged: if (available && source && !isImageLoaded(source)) loadImage(source)
        onImageLoaded: requestPaint()
        onWidthChanged: requestPaint()
        onPaint: {
            const c = getContext("2d"); c.reset(); c.beginPath()
            const r = Math.min(radius, width / 2, height / 2)
            c.moveTo(r,0); c.lineTo(width-r,0); c.quadraticCurveTo(width,0,width,r)
            c.lineTo(width,height-r); c.quadraticCurveTo(width,height,width-r,height)
            c.lineTo(r,height); c.quadraticCurveTo(0,height,0,height-r)
            c.lineTo(0,r); c.quadraticCurveTo(0,0,r,0); c.closePath(); c.clip()
            c.fillStyle = "#e9e7e9"; c.fillRect(0,0,width,height)
            if (source && isImageLoaded(source)) c.drawImage(source,0,0,width,height)
        }
}
