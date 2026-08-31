import QtQuick
import QtQuick.Window

Item {
    id: control

    property string iconName
    property color iconColor: "#909191"

    width: 16
    height: 16

    Canvas {
        id: iconCanvas

        readonly property real dpr: Screen.devicePixelRatio
        width: control.width * dpr
        height: control.height * dpr
        transformOrigin: Item.TopLeft
        scale: 1 / dpr
        onDprChanged: requestPaint()

        onPaint: {
            var context = getContext("2d");
            context.setTransform(dpr, 0, 0, dpr, 0, 0);
            context.clearRect(0, 0, width, height);
            context.strokeStyle = control.iconColor;
            context.fillStyle = control.iconColor;
            context.lineWidth = 1.4;
            context.lineCap = "round";
            context.lineJoin = "round";
            context.beginPath();

            var name = control.iconName;
            if (name === "heading") {
                context.moveTo(3, 3.5);
                context.lineTo(3, 12.5);
                context.moveTo(8.5, 3.5);
                context.lineTo(8.5, 12.5);
                context.moveTo(3, 8);
                context.lineTo(8.5, 8);
                context.moveTo(11.5, 6.5);
                context.lineTo(13.5, 6.5);
                context.moveTo(11.5, 9.5);
                context.lineTo(14.5, 9.5);
            } else if (name === "quote") {
                context.moveTo(3.5, 3);
                context.lineTo(3.5, 13);
                context.moveTo(6.5, 5);
                context.lineTo(13, 5);
                context.moveTo(6.5, 8.5);
                context.lineTo(12, 8.5);
                context.moveTo(6.5, 12);
                context.lineTo(10.5, 12);
            } else if (name === "divider") {
                context.moveTo(2.5, 8);
                context.lineTo(13.5, 8);
                context.moveTo(5, 5.5);
                context.lineTo(5, 10.5);
                context.moveTo(11, 5.5);
                context.lineTo(11, 10.5);
            } else if (name === "bullet") {
                context.arc(4, 4, 1.1, 0, Math.PI * 2);
                context.arc(4, 8, 1.1, 0, Math.PI * 2);
                context.arc(4, 12, 1.1, 0, Math.PI * 2);
                context.fill();
                context.beginPath();
                context.moveTo(7.5, 4);
                context.lineTo(13.5, 4);
                context.moveTo(7.5, 8);
                context.lineTo(13.5, 8);
                context.moveTo(7.5, 12);
                context.lineTo(12, 12);
            } else if (name === "numbered") {
                context.moveTo(3, 3.5);
                context.lineTo(5.5, 3.5);
                context.lineTo(5.5, 6.5);
                context.moveTo(3, 8);
                context.lineTo(6, 8);
                context.moveTo(3, 11.5);
                context.lineTo(6, 11.5);
                context.moveTo(8, 4);
                context.lineTo(13.5, 4);
                context.moveTo(8, 8);
                context.lineTo(13.5, 8);
                context.moveTo(8, 12);
                context.lineTo(12, 12);
            } else if (name === "task") {
                context.rect(2.5, 2.5, 5.5, 5.5);
                context.moveTo(3.5, 5.2);
                context.lineTo(4.8, 6.6);
                context.lineTo(7.4, 3.6);
                context.moveTo(10, 5);
                context.lineTo(14, 5);
                context.moveTo(2.5, 11.5);
                context.lineTo(8, 11.5);
                context.moveTo(10, 11.5);
                context.lineTo(14, 11.5);
            } else if (name === "inlineCode") {
                context.moveTo(6, 3.5);
                context.lineTo(3, 8);
                context.lineTo(6, 12.5);
                context.moveTo(10, 3.5);
                context.lineTo(13, 8);
                context.lineTo(10, 12.5);
            } else if (name === "fence") {
                context.moveTo(3, 4);
                context.lineTo(13, 4);
                context.moveTo(3, 8);
                context.lineTo(11, 8);
                context.moveTo(3, 12);
                context.lineTo(12, 12);
            } else if (name === "table") {
                context.rect(2.5, 3, 11, 10);
                context.moveTo(2.5, 7);
                context.lineTo(13.5, 7);
                context.moveTo(8, 3);
                context.lineTo(8, 13);
            } else if (name === "image") {
                context.rect(2.5, 3.5, 11, 9);
                context.moveTo(4.5, 10.5);
                context.lineTo(6.8, 7.6);
                context.lineTo(9.2, 10);
                context.lineTo(10.6, 8.5);
                context.lineTo(13, 11);
                context.moveTo(11.2, 5.6);
                context.arc(11.2, 5.6, 0.9, 0, Math.PI * 2);
            } else if (name === "link") {
                context.moveTo(6.5, 5);
                context.lineTo(4.2, 5);
                context.quadraticCurveTo(2.4, 5, 2.4, 7.2);
                context.quadraticCurveTo(2.4, 9.4, 4.2, 9.4);
                context.lineTo(6.8, 9.4);
                context.moveTo(9.5, 11);
                context.lineTo(11.8, 11);
                context.quadraticCurveTo(13.6, 11, 13.6, 8.8);
                context.quadraticCurveTo(13.6, 6.6, 11.8, 6.6);
                context.lineTo(9.2, 6.6);
            } else if (name === "date") {
                context.rect(3, 3.5, 10, 9.5);
                context.moveTo(3, 6.5);
                context.lineTo(13, 6.5);
                context.moveTo(6, 2.5);
                context.lineTo(6, 5);
                context.moveTo(10, 2.5);
                context.lineTo(10, 5);
                context.moveTo(6, 9.2);
                context.lineTo(7.3, 10.6);
                context.lineTo(10.4, 7.8);
            } else if (name === "bold") {
                context.moveTo(4, 3);
                context.lineTo(4, 13);
                context.moveTo(4, 3.4);
                context.lineTo(8.2, 3.4);
                context.quadraticCurveTo(11.2, 3.4, 11.2, 6);
                context.quadraticCurveTo(11.2, 8, 8.2, 8);
                context.lineTo(4, 8);
                context.moveTo(8.2, 8);
                context.quadraticCurveTo(12, 8, 12, 10.6);
                context.quadraticCurveTo(12, 12.8, 8.4, 12.8);
                context.lineTo(4, 12.8);
            } else if (name === "italic") {
                context.moveTo(9.5, 3);
                context.lineTo(6.5, 13);
                context.moveTo(6, 3);
                context.lineTo(12.5, 3);
                context.moveTo(3.5, 13);
                context.lineTo(10, 13);
            } else {
                context.moveTo(3, 8);
                context.lineTo(13, 8);
            }

            context.stroke();
        }

        Connections {
            target: control
            function onIconColorChanged() { iconCanvas.requestPaint(); }
            function onIconNameChanged() { iconCanvas.requestPaint(); }
        }
    }
}
