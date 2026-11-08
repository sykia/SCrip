import QtQuick

Canvas {
        property string name: "music"
        property color ink: "#242426"
        width: 20; height: 20
        onNameChanged: requestPaint()
        onInkChanged: requestPaint()
        onPaint: {
            const c = getContext("2d"); c.reset(); c.scale(width / 24, height / 24)
            c.strokeStyle = ink; c.fillStyle = ink; c.lineWidth = 1.65; c.lineCap = "round"; c.lineJoin = "round"; c.beginPath()
            switch (name) {
            case "more": c.arc(5,12,1,0,Math.PI*2); c.moveTo(13,12); c.arc(12,12,1,0,Math.PI*2); c.moveTo(20,12); c.arc(19,12,1,0,Math.PI*2); c.fill(); return
            case "search": c.arc(10,10,6,0,Math.PI*2); c.moveTo(15,15); c.lineTo(21,21); break
            case "home": c.moveTo(3,11); c.lineTo(12,3); c.lineTo(21,11); c.moveTo(5,10); c.lineTo(5,21); c.lineTo(10,21); c.lineTo(10,14); c.lineTo(14,14); c.lineTo(14,21); c.lineTo(19,21); c.lineTo(19,10); break
            case "liked": case "heart": c.moveTo(12,21); c.bezierCurveTo(-5,10,5,-2,12,7); c.bezierCurveTo(19,-2,29,10,12,21); if (name === "liked") c.fill(); break
            case "music": c.moveTo(9,17); c.lineTo(9,5); c.lineTo(20,3); c.lineTo(20,15); c.moveTo(9,8); c.lineTo(20,6); c.moveTo(9,17); c.ellipse(6,18,3,2,0,0,Math.PI*2); c.moveTo(20,15); c.ellipse(17,16,3,2,0,0,Math.PI*2); break
            case "grid": for(let y=4;y<20;y+=9) for(let x=4;x<20;x+=9) c.rect(x,y,6,6); break
            case "list": for(let y=6;y<21;y+=6) { c.moveTo(8,y); c.lineTo(21,y); c.moveTo(3,y); c.lineTo(3.2,y) } break
            case "clock": c.arc(12,12,9,0,Math.PI*2); c.moveTo(12,6); c.lineTo(12,12); c.lineTo(16,14); break
            case "profile": c.arc(12,8,3.5,0,Math.PI*2); c.moveTo(4,21); c.bezierCurveTo(4,12,20,12,20,21); break
            case "settings": for(let y=5;y<22;y+=7) { const x=y===12?15:8; c.moveTo(3,y); c.lineTo(x-2,y); c.moveTo(x+2,y); c.lineTo(21,y); c.moveTo(x+2,y); c.arc(x,y,2,0,Math.PI*2) } break
            case "play": c.moveTo(8,4); c.lineTo(20,12); c.lineTo(8,20); c.closePath(); c.fill(); return
            case "pause": c.rect(6,4,4,16); c.rect(14,4,4,16); c.fill(); return
            case "next": c.moveTo(4,5); c.lineTo(14,12); c.lineTo(4,19); c.closePath(); c.fill(); c.moveTo(16,5); c.lineTo(16,19); break
            case "previous": c.moveTo(20,5); c.lineTo(10,12); c.lineTo(20,19); c.closePath(); c.fill(); c.moveTo(8,5); c.lineTo(8,19); break
            case "plus": c.moveTo(12,5); c.lineTo(12,19); c.moveTo(5,12); c.lineTo(19,12); break
            case "close": c.moveTo(6,6); c.lineTo(18,18); c.moveTo(6,18); c.lineTo(18,6); break
            case "volume": c.moveTo(4,9); c.lineTo(8,9); c.lineTo(13,5); c.lineTo(13,19); c.lineTo(8,15); c.lineTo(4,15); c.closePath(); c.moveTo(17,8); c.quadraticCurveTo(22,12,17,16); break
            case "repeat": c.moveTo(5,9); c.lineTo(5,6); c.lineTo(19,6); c.lineTo(19,10); c.moveTo(16,7); c.lineTo(19,10); c.lineTo(22,7); c.moveTo(19,15); c.lineTo(19,18); c.lineTo(5,18); c.lineTo(5,14); c.moveTo(2,17); c.lineTo(5,14); c.lineTo(8,17); break
            case "shuffle": c.moveTo(3,6); c.bezierCurveTo(12,6,12,18,21,18); c.moveTo(18,15); c.lineTo(21,18); c.lineTo(18,21); c.moveTo(3,18); c.bezierCurveTo(12,18,12,6,21,6); c.moveTo(18,3); c.lineTo(21,6); c.lineTo(18,9); break
            case "cloud": c.moveTo(6,18); c.bezierCurveTo(-1,18,0,10,6,10); c.bezierCurveTo(6,1,19,1,19,10); c.bezierCurveTo(25,10,25,18,19,18); c.closePath(); break
            case "back": c.translate(24,0); c.scale(-1,1);
            case "arrow": c.moveTo(5,12); c.lineTo(19,12); c.moveTo(14,7); c.lineTo(19,12); c.lineTo(14,17); break
            }
            c.stroke()
        }
}
