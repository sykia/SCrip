import QtQuick

Canvas {
        property real radius: 8
        onRadiusChanged: requestPaint()
        onHeightChanged: requestPaint()
        property string source: ""
        property string displayedSource: ""
        property var requestedSources: []
        function requestSource(url) {
            if (!available || !url || requestedSources.indexOf(url) !== -1) return
            requestedSources = requestedSources.concat([url])
            loadImage(url)
            imageLoadPoll.start()
        }
        function releaseUnusedSources() {
            const keep = [source, displayedSource]
            const remaining = []
            for (const url of requestedSources) {
                if (keep.indexOf(url) !== -1 || isImageLoading(url)) {
                    remaining.push(url)
                } else {
                    if (isImageLoaded(url)) unloadImage(url)
                }
            }
            requestedSources = remaining
        }
        onSourceChanged: {
            if (source) {
                if (isImageLoaded(source)) displayedSource = source
                else requestSource(source)
            } else {
                displayedSource = ""
            }
            releaseUnusedSources()
            requestPaint()
        }
        onAvailableChanged: if (available && source && !isImageLoaded(source)) requestSource(source)
        onImageLoaded: {
            if (source && isImageLoaded(source)) displayedSource = source
            releaseUnusedSources()
            requestPaint()
        }
        onWidthChanged: requestPaint()
        Timer {
            id: imageLoadPoll
            interval: 50
            repeat: true
            running: false
            onTriggered: {
                if (source && isImageLoaded(source)) displayedSource = source
                releaseUnusedSources()
                if (!source || !isImageLoading(source)) stop()
                requestPaint()
            }
        }
        Component.onDestruction: {
            for (const url of requestedSources)
                if (isImageLoaded(url)) unloadImage(url)
        }
        onPaint: {
            const c = getContext("2d"); c.reset(); c.beginPath()
            const r = Math.min(radius, width / 2, height / 2)
            c.moveTo(r,0); c.lineTo(width-r,0); c.quadraticCurveTo(width,0,width,r)
            c.lineTo(width,height-r); c.quadraticCurveTo(width,height,width-r,height)
            c.lineTo(r,height); c.quadraticCurveTo(0,height,0,height-r)
            c.lineTo(0,r); c.quadraticCurveTo(0,0,r,0); c.closePath(); c.clip()
            c.fillStyle = "#e9e7e9"; c.fillRect(0,0,width,height)
            if (displayedSource && isImageLoaded(displayedSource)) c.drawImage(displayedSource,0,0,width,height)
        }
}
