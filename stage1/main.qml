import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCharts

ApplicationWindow {
    id: mainWindow
    visible: true
    width: 800
    height: 480
    visibility: Window.FullScreen
    color: "#121212"

    property string currentDisease: ""
    property int totalCycles: 60
    property real targetTop: 64.0
    property real targetBot: 65.0

    StackView {
        id: stack
        anchors.fill: parent
        initialItem: splashPage
    }

    Component {
        id: splashPage
        Rectangle {
            color: "#000000"

            // CRITICAL FIX: Image paths point out of build/ back into stage1/
            AnimatedImage {
                anchors.fill: parent
                source: "file:../lamp_anim.gif"
                fillMode: Image.PreserveAspectCrop
                opacity: 0.4
            }

            Image { source: "file:../icmr_logo.png"; anchors { top: parent.top; left: parent.left; margins: 20 } height: 70; fillMode: Image.PreserveAspectFit }
            Image { source: "file:../ayukriyam_logo.png"; anchors { top: parent.top; horizontalCenter: parent.horizontalCenter; margins: 20 } height: 70; fillMode: Image.PreserveAspectFit }
            Image { source: "file:../iit_logo.png"; anchors { top: parent.top; right: parent.right; margins: 20 } height: 70; fillMode: Image.PreserveAspectFit }

            Text {
                text: "PureGene"
                color: "#000000"
                font.pixelSize: 80
                font.bold: true
                anchors.centerIn: parent
                style: Text.Outline
                styleColor: "#00E5FF"
            }

            Timer { interval: 10000; running: true; repeat: false; onTriggered: stack.replace(modePage) }
        }
    }

    Component {
        id: modePage
        Item {
            RowLayout {
                anchors.centerIn: parent
                spacing: 50
                Button { text: "Calibration Mode"; width: 250; height: 120; font.pixelSize: 22 }
                Button { text: "Operation Mode"; width: 250; height: 120; font.pixelSize: 22; highlighted: true; onClicked: stack.push(diseasePage) }
            }
        }
    }

    Component {
        id: diseasePage
        Item {
            Button { text: "◄ Back"; anchors { top: parent.top; left: parent.left; margins: 10 } font.pixelSize: 18; onClicked: stack.pop() }
            Text { text: "Select Disease"; color: "#00E5FF"; font.pixelSize: 32; font.bold: true; anchors { top: parent.top; horizontalCenter: parent.horizontalCenter; margins: 10 } }

            GridLayout {
                anchors.centerIn: parent; columns: 2; rowSpacing: 30; columnSpacing: 30
                Repeater {
                    model: ["HPV", "HCV", "TB", "Carbapenem"]
                    Button {
                        text: modelData; Layout.preferredWidth: 220; Layout.preferredHeight: 100; font.pixelSize: 26; highlighted: true
                        onClicked: { currentDisease = modelData; stack.push(operationPage) }
                    }
                }
            }
        }
    }

    Component {
        id: operationPage
        Item {
            Button { text: "◄ Back"; anchors { top: parent.top; left: parent.left; margins: 10 } font.pixelSize: 18; onClicked: stack.pop() }
            Text { text: currentDisease + " Operation"; color: "#00E5FF"; font.pixelSize: 32; font.bold: true; anchors { top: parent.top; horizontalCenter: parent.horizontalCenter; margins: 10 } }

            RowLayout {
                anchors.centerIn: parent; spacing: 40

                ColumnLayout {
                    spacing: 30
                    Rectangle {
                        width: 130; height: 130; radius: 65; color: "#1E1E1E"; border.color: "#FF6D00"; border.width: 3
                        Column { anchors.centerIn: parent; spacing: 5; Text { text: "Top (A2)"; color: "#E0E0E0"; font.pixelSize: 16; anchors.horizontalCenter: parent.horizontalCenter } Text { text: Hardware.topTemp.toFixed(1) + "°C"; color: "#FF6D00"; font.pixelSize: 28; font.bold: true; anchors.horizontalCenter: parent.horizontalCenter } Text { text: "Set: " + targetTop + "°C"; color: "#00E5FF"; font.pixelSize: 14; anchors.horizontalCenter: parent.horizontalCenter } }
                        MouseArea { anchors.fill: parent; onClicked: { keypad.target = "top"; keypad.open() } }
                    }
                    Rectangle {
                        width: 130; height: 130; radius: 65; color: "#1E1E1E"; border.color: "#FF6D00"; border.width: 3
                        Column { anchors.centerIn: parent; spacing: 5; Text { text: "Bot (A3)"; color: "#E0E0E0"; font.pixelSize: 16; anchors.horizontalCenter: parent.horizontalCenter } Text { text: Hardware.bottomTemp.toFixed(1) + "°C"; color: "#FF6D00"; font.pixelSize: 28; font.bold: true; anchors.horizontalCenter: parent.horizontalCenter } Text { text: "Set: " + targetBot + "°C"; color: "#00E5FF"; font.pixelSize: 14; anchors.horizontalCenter: parent.horizontalCenter } }
                        MouseArea { anchors.fill: parent; onClicked: { keypad.target = "bot"; keypad.open() } }
                    }
                }

                ColumnLayout {
                    spacing: 15
                    RowLayout { spacing: 20; Button { text: "Tray In"; font.pixelSize: 18; Layout.preferredWidth: 140; Layout.preferredHeight: 60; onClicked: Hardware.trayIn() } Button { text: "Tray Out"; font.pixelSize: 18; Layout.preferredWidth: 140; Layout.preferredHeight: 60; onClicked: Hardware.trayOut() } }
                    RowLayout { spacing: 20; Button { text: "Start Heaters"; font.pixelSize: 18; Layout.preferredWidth: 140; Layout.preferredHeight: 60; onClicked: Hardware.startHeaters() } Button { text: "Stop Heaters"; font.pixelSize: 18; Layout.preferredWidth: 140; Layout.preferredHeight: 60; onClicked: Hardware.stopHeaters() } }
                    Button { text: "Target Cycles: " + totalCycles; font.pixelSize: 18; Layout.preferredWidth: 300; Layout.preferredHeight: 60; onClicked: { keypad.target = "cycles"; keypad.open() } }
                }

                ColumnLayout {
                    spacing: 15
                    Button { text: "Start Optical"; font.pixelSize: 20; font.bold: true; Layout.preferredWidth: 180; Layout.preferredHeight: 70; highlighted: true; onClicked: Hardware.startOptical(totalCycles) }
                    Button { text: "Stop Optical"; font.pixelSize: 18; Layout.preferredWidth: 180; Layout.preferredHeight: 60; onClicked: Hardware.stopOptical() }
                    Item { height: 10 }
                    Button { text: "View Graph"; font.pixelSize: 18; Layout.preferredWidth: 180; Layout.preferredHeight: 60; onClicked: stack.push(graphPage) }
                }
            }
        }
    }

    Component {
        id: graphPage
        Item {
            Button { text: "◄ Back"; anchors { top: parent.top; left: parent.left; margins: 10 } font.pixelSize: 18; z: 2; onClicked: stack.pop() }
            ChartView {
                title: currentDisease + " - Optical RFU Activity"; anchors.fill: parent; theme: ChartView.ChartThemeDark; antialiasing: true
                ValueAxis { id: xAxis; min: 0; max: totalCycles; titleText: "Cycles" }
                ValueAxis { id: yAxis; min: 0; max: 5000; titleText: "ADC Value (RFU)" }
                LineSeries { id: spot1; name: "Spot 1"; axisX: xAxis; axisY: yAxis; color: "#00E5FF" }
                LineSeries { id: spot2; name: "Spot 2"; axisX: xAxis; axisY: yAxis; color: "#00FF87" }
                LineSeries { id: spot3; name: "Spot 3"; axisX: xAxis; axisY: yAxis; color: "#FFEB3B" }
                LineSeries { id: spot4; name: "Spot 4"; axisX: xAxis; axisY: yAxis; color: "#FF6D00" }
                LineSeries { id: spot5; name: "Spot 5"; axisX: xAxis; axisY: yAxis; color: "#E040FB" }
                LineSeries { id: spot6; name: "Spot 6"; axisX: xAxis; axisY: yAxis; color: "#FF1744" }
            }
            Connections {
                target: Hardware
                function onGraphDataUpdated(spot, cycle, rfu) {
                    if (spot === 1) spot1.append(cycle, rfu); else if (spot === 2) spot2.append(cycle, rfu); else if (spot === 3) spot3.append(cycle, rfu); else if (spot === 4) spot4.append(cycle, rfu); else if (spot === 5) spot5.append(cycle, rfu); else if (spot === 6) spot6.append(cycle, rfu)
                }
            }
        }
    }

    Popup {
        id: keypad
        width: 320; height: 380
        x: Math.round((mainWindow.width - width) / 2)
        y: Math.round((mainWindow.height - height) / 2)
        modal: true; focus: true

        background: Rectangle { color: "#1E1E1E"; border.color: "#00E5FF"; border.width: 2; radius: 10 }

        property string target: ""
        property string val: ""

        ColumnLayout {
            anchors.fill: parent; anchors.margins: 15
            Text { text: "Set Value: " + keypad.val; color: "#FFFFFF"; font.pixelSize: 26; Layout.alignment: Qt.AlignHCenter }

            GridLayout {
                columns: 3; rowSpacing: 10; columnSpacing: 10; Layout.alignment: Qt.AlignHCenter
                Repeater {
                    model: ["1","2","3","4","5","6","7","8","9","C","0","OK"]
                    Button {
                        text: modelData; font.pixelSize: 24; Layout.preferredWidth: 80; Layout.preferredHeight: 60
                        palette.button: (modelData === "OK") ? "#43A047" : (modelData === "C") ? "#E53935" : "#333333"
                        palette.buttonText: "#FFFFFF"
                        onClicked: {
                            if(modelData === "C") { keypad.val = "" }
                            else if (modelData === "OK") {
                                if (keypad.val !== "") {
                                    if(keypad.target === "top") { targetTop = parseFloat(keypad.val); Hardware.setTopTemp(targetTop) }
                                    if(keypad.target === "bot") { targetBot = parseFloat(keypad.val); Hardware.setBottomTemp(targetBot) }
                                    if(keypad.target === "cycles") { totalCycles = parseInt(keypad.val) }
                                }
                                keypad.close(); keypad.val = ""
                            } else { keypad.val += modelData }
                        }
                    }
                }
            }
        }
    }

    Connections {
        target: Hardware
        function onRunComplete(msg) { toastMsg.text = msg; toast.open() }
    }

    Popup {
        id: toast
        width: 400
        x: Math.round((mainWindow.width - width) / 2)
        y: mainWindow.height - 100
        background: Rectangle { color: "#1C1C1C"; radius: 5; border.color: "#00FF87" }
        padding: 15

        Text { id: toastMsg; color: "#00FF87"; font.pixelSize: 20; font.bold: true; anchors.centerIn: parent }
        Timer { running: toast.visible; interval: 4000; onTriggered: toast.close() }
    }

    Text {
        id: clockDisplay
        anchors { bottom: parent.bottom; right: parent.right; margins: 15 }
        color: "#00E5FF"; font.pixelSize: 22; font.bold: true
    }
    Timer {
        interval: 1000; running: true; repeat: true
        onTriggered: { var d = new Date(); clockDisplay.text = d.toLocaleTimeString(Qt.locale("en_IN"), "hh:mm:ss A") }
    }
}
