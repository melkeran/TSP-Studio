import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import TSPStudio 1.0

Window {
    id: root
    width: 1920; height: 1280; visible: true
    title: "TSP Studio"
    color: "#0f172a"
    property bool isSolving: false
    property int solveTime: 0
    property real currentDist: 0.0
    property real cityPointSize: guiController.cityPointSize
    property real routeLineThickness: guiController.routeLineThickness
    
    onCityPointSizeChanged: guiController.cityPointSize = cityPointSize
    onRouteLineThicknessChanged: guiController.routeLineThickness = routeLineThickness

    Timer {
        id: solveTimer
        interval: 100; running: root.isSolving; repeat: true
        onTriggered: solveTime += 100
    }

    component Btn: Button {
        id: b; property color bg: "#3b82f6"; property color hbg: "#2563eb"
        property string btnIcon: ""
        property bool active: false
        contentItem: RowLayout {
            spacing: 8
            Text { text: b.btnIcon; font.pixelSize: 18; color: "#fff"; visible: b.btnIcon !== ""; Layout.alignment: Qt.AlignVCenter }
            Text { text: b.text; font.bold: true; font.pixelSize: 14; color: "#fff"; Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter; Layout.alignment: Qt.AlignVCenter }
        }
        background: Rectangle { radius: 8
            color: b.down ? Qt.darker(b.bg, 1.3) : b.hovered ? b.hbg : b.bg
            border.color: b.active ? "#fff" : "transparent"; border.width: 2
            Behavior on color { ColorAnimation { duration: 120 } }
        }
    }

    component Param : ColumnLayout {
        property string label: ""; property double min: 0; property double max: 100
        property double val: 50; property double step: 1; signal changed(double v)
        Layout.fillWidth: true; spacing: 4
        RowLayout {
            Layout.fillWidth: true
            Text { text: label; color: "#94a3b8"; font.pixelSize: 14; font.bold: true }
            Item { Layout.fillWidth: true }
            Text { text: s.value.toFixed(step < 1?2:0); color: "#3b82f6"; font.pixelSize: 14; font.family: "Consolas"; font.bold: true }
        }
        Slider {
            id: s; Layout.fillWidth: true; Layout.preferredHeight: 24
            from: min; to: max; value: val; stepSize: step
            onPositionChanged: if (pressed) changed(value)
            Component.onCompleted: changed(value)
            background: Rectangle { height: 4; y: 10; width: parent.width; color: "#334155"; radius: 2
                Rectangle { width: s.visualPosition * parent.width; height: 4; color: "#3b82f6"; radius: 2 } }
            handle: Rectangle { x: s.visualPosition * (s.availableWidth - 14); y: 5; width: 14; height: 14; radius: 7; color: "#f8fafc" }
        }
    }

    component ProColorPicker : Popup {
        id: cpd; property string targetName
        width: 320; height: 440; modal: true; focus: true
        background: Rectangle { color: "#0f172a"; radius: 12; border.color: "#3b82f6"; border.width: 1 }
        
        property real hue: 0.0; property real sat: 1.0; property real val: 1.0
        function updateFromColor(clr) {
            hue = clr.hsvHue; sat = clr.hsvSaturation; val = clr.hsvValue;
        }

        onAboutToShow: updateFromColor(mv[targetName])

        contentItem: ColumnLayout {
            spacing: 15
            Item { Layout.preferredHeight: 15; Layout.fillWidth: true } // Top Padding
            Text { text: "COLOR STUDIO"; color: "#3b82f6"; font.pixelSize: 15; font.bold: true; Layout.alignment: Qt.AlignHCenter; font.letterSpacing: 2 }
            
            // Saturation-Value Picker
            Rectangle {
                id: svBox; Layout.alignment: Qt.AlignHCenter; width: 280; height: 180; radius: 8; clip: true
                color: Qt.hsva(cpd.hue, 1, 1, 1)
                Rectangle { anchors.fill: parent; gradient: Gradient { orientation: Gradient.Horizontal; GradientStop { position: 0.0; color: "#ffffff" } GradientStop { position: 1.0; color: "transparent" } } }
                Rectangle { anchors.fill: parent; gradient: Gradient { orientation: Gradient.Vertical; GradientStop { position: 0.0; color: "transparent" } GradientStop { position: 1.0; color: "#000000" } } }
                Rectangle {
                    x: cpd.sat * (svBox.width) - 10; y: (1.0 - cpd.val) * (svBox.height) - 10
                    width: 20; height: 20; radius: 10; color: "transparent"; border.color: "#fff"; border.width: 2
                    Rectangle { anchors.fill: parent; anchors.margins: 4; radius: 6; color: Qt.hsva(cpd.hue, cpd.sat, cpd.val, 1); border.color: "#000"; border.width: 1 }
                }
                MouseArea {
                    anchors.fill: parent
                    function handle(m) {
                        cpd.sat = Math.max(0, Math.min(1, m.x / width));
                        cpd.val = 1.0 - Math.max(0, Math.min(1, m.y / height));
                    }
                    onPressed: handle(mouse); onPositionChanged: handle(mouse)
                }
            }

            // Hue Slider
            Rectangle {
                id: hSlider; Layout.alignment: Qt.AlignHCenter; width: 280; height: 24; radius:12
                gradient: Gradient { orientation: Gradient.Horizontal
                    GradientStop { position: 0.00; color: "#ff0000" } GradientStop { position: 0.17; color: "#ffff00" }
                    GradientStop { position: 0.33; color: "#00ff00" } GradientStop { position: 0.50; color: "#00ffff" }
                    GradientStop { position: 0.67; color: "#0000ff" } GradientStop { position: 0.83; color: "#ff00ff" }
                    GradientStop { position: 1.00; color: "#ff0000" }
                }
                Rectangle {
                    x: cpd.hue * (hSlider.width - 8); y: -2; width: 8; height: 28; color: "#fff"; radius: 4; border.color: "#000"; border.width: 1
                }
                MouseArea {
                    anchors.fill: parent
                    onPressed: cpd.hue = Math.max(0, Math.min(1, mouse.x / width))
                    onPositionChanged: cpd.hue = Math.max(0, Math.min(1, mouse.x / width))
                }
            }

            // Preview & Hex
            RowLayout {
                Layout.alignment: Qt.AlignHCenter; width: 280; spacing: 10
                Rectangle { width: 48; height: 48; radius: 8; color: Qt.hsva(cpd.hue, cpd.sat, cpd.val, 1); border.color: "#3b82f6"; border.width: 1 }
                ColumnLayout {
                    spacing: 2
                    Text { text: "HEX CODE"; color: "#64748b"; font.pixelSize: 10; font.bold: true; font.letterSpacing: 1 }
                    Rectangle {
                        width: 100; height: 32; radius: 6; color: "#1e293b"; border.color: "#3b82f6"; border.width: 1
                        Text {
                            anchors.centerIn: parent; color: "#fff"; font.family: "Consolas"; font.pixelSize: 14; font.bold: true
                            text: Qt.hsva(cpd.hue, cpd.sat, cpd.val, 1).toString().toUpperCase()
                        }
                    }
                }
                Item { Layout.fillWidth: true }
                Btn {
                    text: "SELECT"; bg: "#3b82f6"; hbg: "#2563eb"; Layout.preferredWidth: 100; Layout.preferredHeight: 38
                    onClicked: { guiController[cpd.targetName] = Qt.hsva(cpd.hue, cpd.sat, cpd.val, 1); cpd.close() }
                }
            }
            Item { Layout.preferredHeight: 15; Layout.fillWidth: true } // Bottom Padding
        }
    }
    ProColorPicker { id: proColorPicker }


    FileDialog {
        id: openFileDialog
        title: "Open TSP File"
        nameFilters: ["TSP files (*.tsp)", "All files (*)"]
        onAccepted: {
            guiController.loadTSPFile(openFileDialog.selectedFile.toString());
        }
    }

    SplitView {
        anchors.fill: parent; orientation: Qt.Horizontal
        handle: Rectangle { implicitWidth:6
            color: SplitHandle.pressed?"#3b82f6":SplitHandle.hovered?"#475569":"#334155" }

        // ── MAP ──
        Item {
            SplitView.fillWidth: true; SplitView.minimumWidth: 300
            Rectangle { anchors.fill:parent; color:"#0f172a" }
            FileDialog { id: saveTourDialog; title: "Save TSP Studio Tour"; nameFilters: ["Tour files (*.tour)", "All files (*)"]; fileMode: FileDialog.SaveFile; onAccepted: guiController.saveRoute(selectedFile) }
    FileDialog { id: loadTourDialog; title: "Load TSP Studio Tour"; nameFilters: ["Tour files (*.tour)", "All files (*)"]; fileMode: FileDialog.OpenFile; onAccepted: guiController.loadRoute(selectedFile) }

    TSPStudioMapView {
                id: mv; anchors.fill:parent; anchors.margins:2
                controller: guiController
                cityRadius: guiController.cityPointSize
                routeThickness: guiController.routeLineThickness
                cityColor: guiController.cityColor
                routeColor: guiController.routeColor
                delaunayColor: guiController.delaunayColor
            }
            DropArea {
                anchors.fill:parent
                onEntered: function(d){d.accept()}
                onDropped: function(d){if(d.hasUrls) guiController.loadTSPFile(d.urls[0])}
            }
            Column {
                anchors.centerIn:parent; spacing:12; visible: guiController.cityCount === 0
                Text { text: "Welcome to TSP Studio"; color: "#3b82f6"; font.pixelSize: 32; font.bold: true; anchors.horizontalCenter: parent.horizontalCenter }
                Text { text: "Interactive Mode Active"; color: "#64748b"; font.pixelSize: 18; anchors.horizontalCenter: parent.horizontalCenter }
                Rectangle { width: 100; height: 1; color: "#334155"; anchors.horizontalCenter: parent.horizontalCenter }
                
                Text { text: "🖱️ DOUBLE-CLICK to add locations"; color: "#cbd5e1"; font.pixelSize: 20; font.bold: true; anchors.horizontalCenter: parent.horizontalCenter }
                Text { text: "🖱️ RIGHT-CLICK city to remove it"; color: "#94a3b8"; font.pixelSize: 18; anchors.horizontalCenter: parent.horizontalCenter }
                Text { text: "🖱️ DRAG left-click to move cities"; color: "#94a3b8"; font.pixelSize: 18; anchors.horizontalCenter: parent.horizontalCenter }
                Text { text: "🖱️ MIDDLE-CLICK & drag to pan"; color: "#64748b"; font.pixelSize: 18; anchors.horizontalCenter: parent.horizontalCenter }
                Text { text: "🖱️ SCROLL to zoom in/out"; color: "#64748b"; font.pixelSize: 18; anchors.horizontalCenter: parent.horizontalCenter }
                Text { text: "📂 DROP .tsp file to load data"; color: "#64748b"; font.pixelSize: 18; anchors.horizontalCenter: parent.horizontalCenter }
            }
            Rectangle {
                anchors.top:parent.top; anchors.right:parent.right; anchors.margins:12
                width:dl.implicitWidth+20; height:dl.implicitHeight+10; radius:6
                color:"#1e293b"; border.color:"#334155"; visible:dl.text!==""
                Text {
                    id: dl
                    anchors.centerIn: parent; color: "#38bdf8"; font.pixelSize: 20; font.bold: true
                    text: {
                        if (currentDist <= 0) return "";
                        let t = root.isSolving ? solveTime : finalTime;
                        return "Dist: " + currentDist.toFixed(2) + " | " + (t/1000).toFixed(1) + "s";
                    }
                }
            }
        }

        // ── PANEL ──
        Rectangle {
            id: sidePanel
            SplitView.preferredWidth: 320; SplitView.minimumWidth: 280; SplitView.maximumWidth: 500
            color: "#1e293b"
            
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 0; spacing: 0

                // Header / "Action Bar"
                Rectangle {
                    Layout.fillWidth: true; height: 60; color: "#0f172a"
                    RowLayout {
                        anchors.fill: parent; anchors.margins: 12
                        Text { text: "⚙️ CONTROL CENTER"; color: "#3b82f6"; font.pixelSize: 16; font.bold: true; font.letterSpacing: 1.5 }
                        Item { Layout.fillWidth: true }
                        Btn { btnIcon: "📁"; text: "OPEN"; Layout.preferredHeight: 36; Layout.preferredWidth: 90; bg: "#334155"; hbg: "#475569"; onClicked: openFileDialog.open() }
                    }
                    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 2; color: "#3b82f6" }
                }

                Flickable {
                    id: controlFlick
                    Layout.fillWidth: true; Layout.fillHeight: true; clip: true
                    contentWidth: width; contentHeight: controlContent.height + 40
                    
                    ScrollBar.vertical: ScrollBar { 
                        id: vbar; width: 14; policy: ScrollBar.AlwaysOn; z: 100
                        anchors.right: parent.right
                        background: Rectangle { anchors.fill: parent; color: "#0f172a"; opacity: 0.2; radius: 7 }
                        contentItem: Rectangle { 
                            implicitWidth: 8; radius: 4; 
                            color: parent.pressed ? "#60a5fa" : (parent.hovered ? "#3b82f6" : "#64748b")
                        }
                    }

                    ColumnLayout {
                        id: controlContent
                        width: controlFlick.width - 32; x: 16; y: 20; spacing: 20

                        // ALGORITHM SECTION
                        ColumnLayout {
                            Layout.fillWidth: true; spacing: 10
                            RowLayout {
                                Text { text: "⚡ ALGORITHM"; color: "#64748b"; font.pixelSize: 13; font.bold: true; font.letterSpacing: 2 }
                                Rectangle { Layout.fillWidth: true; height: 1; color: "#334155"; Layout.alignment: Qt.AlignVCenter }
                            }
                            ComboBox {
                                id: algo; Layout.fillWidth: true; Layout.preferredHeight: 40
                                model: ["2-Opt Local Search (2-Opt)", "3-Opt Local Search (3-Opt)", "5-Opt Local Search (5-Opt)", "Cuckoo Search Algorithm (CS)", "Ecological Cycle TSP Studio (ECO)", "Genetic Algorithm (GA)", "Gray Wolf Optimization (GWO)", "Guided Local Search (GLS)", "Iterated Local Search (ILS)", "Lin-Kernighan Algorithm (LK)", "Pelican Optimization Algorithm (POA)", "Random Shuffle", "Tabu Search Algorithm (TS)", "Whale Optimization Algorithm (WOA)"]
                                currentIndex: Math.max(0, model.indexOf(guiController.lastAlgorithm))
                                onActivated: guiController.lastAlgorithm = currentText
                                background: Rectangle { color: "#0f172a"; radius: 8; border.color: algo.activeFocus ? "#3b82f6" : "#334155"; border.width: 1 }
                                contentItem: Text { leftPadding: 12; text: algo.displayText; font.pixelSize: 16; color: "#f8fafc"; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
                                delegate: ItemDelegate {
                                    width: algo.width; height: 40; contentItem: Text { text: modelData; color: highlighted ? "#ffffff" : "#cbd5e1"; font.pixelSize: 15; verticalAlignment: Text.AlignVCenter }
                                    background: Rectangle { color: highlighted ? "#3b82f6" : "transparent"; radius: 4; anchors.fill: parent; anchors.margins: 2 }
                                }
                                popup: Popup { y: algo.height + 4; width: algo.width; implicitHeight: Math.min(contentItem.implicitHeight, 400); padding: 4
                                    contentItem: ListView { clip: true; implicitHeight: contentHeight; model: algo.delegateModel; currentIndex: algo.highlightedIndex; ScrollIndicator.vertical: ScrollIndicator { } }
                                    background: Rectangle { color: "#1e293b"; border.color: "#3b82f6"; border.width: 1; radius: 8 }
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true; spacing: 10
                                Btn {
                                    Layout.fillWidth: true; Layout.preferredHeight: 48
                                    text: isSolving ? "STOP" : "RUN SOLVER"
                                    btnIcon: isSolving ? "⏹" : "🚀"
                                    bg: isSolving ? "#f43f5e" : "#10b981"; hbg: isSolving ? "#e11d48" : "#059669"
                                    active: isSolving
                                    onClicked: isSolving ? guiController.stopSolver() : guiController.runSolver(algo.currentText)
                                }
                                Btn {
                                    visible: !isSolving; Layout.preferredWidth: 100; Layout.preferredHeight: 48
                                    text: "RESET"
                                    btnIcon: "🔄"
                                    bg: "#475569"; hbg: "#334155"
                                    onClicked: guiController.resetTSPStudio()
                                }
                            }
                        }

                        // TUNING SECTION
                        ColumnLayout {
                            Layout.fillWidth: true; spacing: 12; visible: !isSolving
                            RowLayout {
                                Text { text: "🛠️ TUNING"; color: "#64748b"; font.pixelSize: 13; font.bold: true; font.letterSpacing: 2 }
                                Rectangle { Layout.fillWidth: true; height: 1; color: "#334155"; Layout.alignment: Qt.AlignVCenter }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true; spacing: 6
                                Text { text: "INITIAL TOUR"; color: "#94a3b8"; font.pixelSize: 14; font.bold: true }
                                RowLayout {
                                    Layout.fillWidth: true; spacing: 10
                                    ComboBox {
                                        id: initMethod; Layout.fillWidth: true; Layout.preferredHeight: 38
                                        model: ["Nearest Neighbor", "Christofides", "Random Shuffle"]
                                        currentIndex: 1 // Christofides Default
                                        background: Rectangle { color: "#0f172a"; radius: 8; border.color: initMethod.activeFocus ? "#3b82f6" : "#334155"; border.width: 1 }
                                        contentItem: Text { leftPadding: 12; text: initMethod.displayText; color: "#f8fafc"; font.pixelSize: 14; verticalAlignment: Text.AlignVCenter }
                                        delegate: ItemDelegate {
                                            width: initMethod.width; height: 36; contentItem: Text { text: modelData; color: highlighted ? "#ffffff" : "#cbd5e1"; font.pixelSize: 14; verticalAlignment: Text.AlignVCenter }
                                            background: Rectangle { color: highlighted ? "#3b82f6" : "transparent"; radius: 4; anchors.fill: parent; anchors.margins: 2 }
                                        }
                                        onCurrentIndexChanged: { let m = "nearest_neighbor"; if (currentIndex === 1) m = "christofides"; else if (currentIndex === 2) m = "random"; guiController.setAlgorithmParam("initial_tour_method", m); }
                                    }
                                    Btn {
                                        text: "PLOT"
                                        Layout.preferredWidth: 80; Layout.preferredHeight: 38
                                        bg: "#6366f1"; hbg: "#4f46e5"
                                        onClicked: guiController.plotInitialSolution()
                                    }
                                }
                                RowLayout {
                                    Layout.fillWidth: true; spacing: 10; visible: !isSolving
                                    Btn {
                                        text: "SAVE TOUR"; btnIcon: "💾"; Layout.fillWidth: true; Layout.preferredHeight: 38
                                        bg: "#475569"; hbg: "#334155"
                                        onClicked: saveTourDialog.open()
                                    }
                                    Btn {
                                        text: "LOAD TOUR"; btnIcon: "📁"; Layout.fillWidth: true; Layout.preferredHeight: 38
                                        bg: "#475569"; hbg: "#334155"
                                        onClicked: loadTourDialog.open()
                                    }
                                }
                            }

                            Param { label: "Search Candidates (Global)"; min: 1; max: 500; val: Math.min(500, guiController.cityCount); step: 1; onChanged: (v) => guiController.setAlgorithmParam("global_cand_size", v) }

                            // Algorithm Specific Params
                            ColumnLayout {
                                Layout.fillWidth: true; spacing: 4

                                ColumnLayout {
                                    visible: algo.currentText === "Ant Colony Optimization"; Layout.fillWidth: true; spacing: 0
                                    Param { label: "Ants"; min:10; max:100; val:40; onChanged: (v) => guiController.setAlgorithmParam("aco_ants", v) }
                                    Param { label: "Alpha"; min:0; max:5; val:1; step:0.1; onChanged: (v) => guiController.setAlgorithmParam("aco_alpha", v) }
                                    Param { label: "Beta"; min:0; max:10; val:3; step:0.1; onChanged: (v) => guiController.setAlgorithmParam("aco_beta", v) }
                                    Param { label: "Evaporation"; min:0.01; max:1; val:0.5; step:0.01; onChanged: (v) => guiController.setAlgorithmParam("aco_rho", v) }
                                }

                                ColumnLayout {
                                    visible: algo.currentText === "Genetic Algorithm (GA)"; Layout.fillWidth: true; spacing: 5
                                    Text { text: "GA Variant Architecture"; color: "#cbd5e1"; font.pixelSize: 12; font.bold: true }
                                    ComboBox {
                                        Layout.fillWidth: true; Layout.preferredHeight: 40
                                        model: ["Standard Memetic (S-GA)", "Island Model Distributed (IM-GA)", "Cellular Grid (CG-GA)", "Age-Layered Population (ALPS)", "Edge Assembly Crossover (EAX)"]
                                        background: Rectangle { color: "#0f172a"; radius: 6; border.color: parent.activeFocus ? "#3b82f6" : "#334155" }
                                        contentItem: Text { leftPadding: 10; text: parent.displayText; color: "#e2e8f0"; font.pixelSize: 14; verticalAlignment: Text.AlignVCenter }
                                        onCurrentTextChanged: guiController.setAlgorithmParam("ga_variant", currentText)
                                        Component.onCompleted: guiController.setAlgorithmParam("ga_variant", currentText)
                                    }
                                    Param { label: "Population"; min:10; max:200; val:40; onChanged: (v) => guiController.setAlgorithmParam("ga_pop", v) }
                                    Param { label: "Generations"; min:100; max:2000; val:500; step:50; onChanged: (v) => guiController.setAlgorithmParam("ga_gen", v) }
                                    Param { label: "Mutation %"; min:0; max:100; val:10; onChanged: (v) => guiController.setAlgorithmParam("ga_mut", v) }
                                }
                                ColumnLayout {
                                    visible: algo.currentText === "Cuckoo Search Algorithm (CS)"; Layout.fillWidth: true; spacing: 5
                                    Text { text: "CS Variant Architecture"; color: "#cbd5e1"; font.pixelSize: 12; font.bold: true }
                                    ComboBox {
                                        Layout.fillWidth: true; Layout.preferredHeight: 40
                                        model: ["Standard Memetic (S-CS)", "ILS-Guided Discovery (ILS-CS)", "Adaptive Step-Size (A-CS)", "Elitist Replacement (E-CS)", "Cellular Cuckoo (C-CS)", "Chaotic Fractal Flights (CF-CS)"]
                                        background: Rectangle { color: "#0f172a"; radius: 6; border.color: parent.activeFocus ? "#3b82f6" : "#334155" }
                                        contentItem: Text { leftPadding: 10; text: parent.displayText; color: "#e2e8f0"; font.pixelSize: 14; verticalAlignment: Text.AlignVCenter }
                                        onCurrentTextChanged: guiController.setAlgorithmParam("cs_variant", currentText)
                                        Component.onCompleted: guiController.setAlgorithmParam("cs_variant", currentText)
                                    }
                                    Param { label: "Nests"; min: 10; max: 200; val: 40; step: 10; onChanged: (v) => guiController.setAlgorithmParam("cs_nests", v) }
                                    Param { label: "Discovery %"; min: 0.1; max: 0.8; val: 0.25; step: 0.05; onChanged: (v) => guiController.setAlgorithmParam("cs_prob", v) }
                                    Param { label: "Kick Inner Steps"; min: 10; max: 500; val: 50; step: 10; onChanged: (v) => guiController.setAlgorithmParam("cs_kick_steps", v) }
                                    Param { label: "Kick Reversal %"; min: 0.01; max: 0.5; val: 0.15; step: 0.01; onChanged: (v) => guiController.setAlgorithmParam("cs_kick_size", v) }
                                }
                                ColumnLayout {
                                    visible: algo.currentText === "Gray Wolf Optimization (GWO)"; Layout.fillWidth: true; spacing: 5
                                    Text { text: "GWO Variant Architecture"; color: "#cbd5e1"; font.pixelSize: 12; font.bold: true }
                                    ComboBox {
                                        Layout.fillWidth: true; Layout.preferredHeight: 40
                                        model: ["Standard Memetic (S-GWO)", "ILS-Guided Alpha (ILS-GWO)", "Hierarchical Wolf (H-GWO)", "Non-Linear Parameter (NL-GWO)", "Enhanced Exploration (EE-GWO)", "Multi-Leader Swarm (ML-GWO)"]
                                        background: Rectangle { color: "#0f172a"; radius: 6; border.color: parent.activeFocus ? "#3b82f6" : "#334155" }
                                        contentItem: Text { leftPadding: 10; text: parent.displayText; color: "#e2e8f0"; font.pixelSize: 14; verticalAlignment: Text.AlignVCenter }
                                        onCurrentTextChanged: guiController.setAlgorithmParam("gwo_variant", currentText)
                                        Component.onCompleted: guiController.setAlgorithmParam("gwo_variant", currentText)
                                    }
                                    Param { label: "Pack Size"; min: 10; max: 200; val: 40; step: 10; onChanged: (v) => guiController.setAlgorithmParam("gwo_size", v) }
                                    Param { label: "Max Iterations"; min: 100; max: 2000; val: 500; step: 100; onChanged: (v) => guiController.setAlgorithmParam("gwo_iters", v) }
                                    Param { label: "Kick Inner Steps"; min: 10; max: 500; val: 50; step: 10; onChanged: (v) => guiController.setAlgorithmParam("gwo_kick_steps", v) }
                                    Param { label: "Kick Reversal %"; min: 0.01; max: 0.5; val: 0.15; step: 0.01; onChanged: (v) => guiController.setAlgorithmParam("gwo_kick_size", v) }
                                }

                                ColumnLayout {
                                    visible: algo.currentText === "Whale Optimization Algorithm (WOA)"; Layout.fillWidth: true; spacing: 5
                                    Text { text: "WOA Variant Architecture"; color: "#cbd5e1"; font.pixelSize: 12; font.bold: true }
                                    ComboBox {
                                        Layout.fillWidth: true; Layout.preferredHeight: 40
                                        model: ["Standard Memetic (S-WOA)", "ILS-Guided Leader (ILS-WOA)", "Levy Flight Attack (LF-WOA)", "Adaptive Weight (AW-WOA)", "Chaotic Bubble-Net (CB-WOA)", "Oppositional Based (OBL-WOA)"]
                                        background: Rectangle { color: "#0f172a"; radius: 6; border.color: parent.activeFocus ? "#3b82f6" : "#334155" }
                                        contentItem: Text { leftPadding: 10; text: parent.displayText; color: "#e2e8f0"; font.pixelSize: 14; verticalAlignment: Text.AlignVCenter }
                                        onCurrentTextChanged: guiController.setAlgorithmParam("woa_variant", currentText)
                                        Component.onCompleted: guiController.setAlgorithmParam("woa_variant", currentText)
                                    }
                                    Param { label: "Whales"; min: 10; max: 200; val: 40; step: 10; onChanged: (v) => guiController.setAlgorithmParam("woa_size", v) }
                                    Param { label: "Spiral Factor"; min: 0.1; max: 1.0; val: 0.5; step: 0.1; onChanged: (v) => guiController.setAlgorithmParam("woa_spiral", v) }
                                    Param { label: "Kick Inner Steps"; min: 10; max: 500; val: 50; step: 10; onChanged: (v) => guiController.setAlgorithmParam("woa_kick_steps", v) }
                                    Param { label: "Kick Reversal %"; min: 0.01; max: 0.5; val: 0.15; step: 0.01; onChanged: (v) => guiController.setAlgorithmParam("woa_kick_size", v) }
                                }
                                ColumnLayout {
                                    visible: algo.currentText === "Pelican Optimization Algorithm (POA)"; Layout.fillWidth: true; spacing: 5
                                    Text { text: "POA Variant Architecture"; color: "#cbd5e1"; font.pixelSize: 12; font.bold: true }
                                    ComboBox {
                                        Layout.fillWidth: true; Layout.preferredHeight: 40
                                        model: ["Standard Memetic (S-POA)", "ILS-Guided Search (ILS-POA)", "Levy Flight Search (LF-POA)", "Adaptive Phase (AP-POA)", "Chaotic Pelican Swarm (CP-POA)", "Oppositional Based (OBL-POA)"]
                                        background: Rectangle { color: "#0f172a"; radius: 6; border.color: parent.activeFocus ? "#3b82f6" : "#334155" }
                                        contentItem: Text { leftPadding: 10; text: parent.displayText; color: "#e2e8f0"; font.pixelSize: 14; verticalAlignment: Text.AlignVCenter }
                                        onCurrentTextChanged: guiController.setAlgorithmParam("poa_variant", currentText)
                                        Component.onCompleted: guiController.setAlgorithmParam("poa_variant", currentText)
                                    }
                                    Param { label: "Pelicans"; min: 10; max: 200; val: 40; step: 10; onChanged: (v) => guiController.setAlgorithmParam("poa_size", v) }
                                    Param { label: "Max Iterations"; min: 100; max: 2000; val: 500; step: 100; onChanged: (v) => guiController.setAlgorithmParam("poa_iters", v) }
                                    Param { label: "Kick Inner Steps"; min: 10; max: 500; val: 50; step: 10; onChanged: (v) => guiController.setAlgorithmParam("poa_kick_steps", v) }
                                    Param { label: "Kick Reversal %"; min: 0.01; max: 0.5; val: 0.15; step: 0.01; onChanged: (v) => guiController.setAlgorithmParam("poa_kick_size", v) }
                                }
                                ColumnLayout {
                                    visible: algo.currentText === "Tabu Search Algorithm (TS)"; Layout.fillWidth: true; spacing: 0
                                    Param { label: "Max Steps"; min:100; max:5000; val:800; step:100; onChanged: (v) => guiController.setAlgorithmParam("tabu_steps", v) }
                                    Param { label: "Tenure"; min:5; max:200; val:50; step:5; onChanged: (v) => guiController.setAlgorithmParam("tabu_tenure", v) }
                                }
                                ColumnLayout {
                                    visible: algo.currentText === "Ecological Cycle TSP Studio (ECO)"; Layout.fillWidth: true; spacing: 5
                                    Text { text: "Eco Variant Architecture"; color: "#cbd5e1"; font.pixelSize: 12; font.bold: true }
                                    ComboBox {
                                        Layout.fillWidth: true; Layout.preferredHeight: 40
                                        model: ["Standard Memetic (S-ECO)", "ILS-Guided Recycling (ILS-ECO)", "Dynamic Trophic Levels (DTL-ECO)"]
                                        background: Rectangle { color: "#0f172a"; radius: 6; border.color: parent.activeFocus ? "#3b82f6" : "#334155" }
                                        contentItem: Text { leftPadding: 10; text: parent.displayText; color: "#e2e8f0"; font.pixelSize: 14; verticalAlignment: Text.AlignVCenter }
                                        onCurrentTextChanged: guiController.setAlgorithmParam("eco_variant", currentText)
                                    }
                                    Param { label: "Population"; min: 10; max: 200; val: 50; step: 10; onChanged: (v) => guiController.setAlgorithmParam("eco_size", v) }
                                    Param { label: "Max Iterations"; min: 100; max: 5000; val: 1000; step: 100; onChanged: (v) => guiController.setAlgorithmParam("eco_iters", v) }
                                    Param { label: "Producer %"; min: 0.1; max: 0.8; val: 0.2; step: 0.05; onChanged: (v) => guiController.setAlgorithmParam("eco_producers", v) }
                                    Param { label: "Kick Inner Steps"; min: 10; max: 500; val: 50; step: 10; onChanged: (v) => guiController.setAlgorithmParam("eco_kick_steps", v) }
                                    Param { label: "Kick Reversal %"; min: 0.01; max: 0.5; val: 0.15; step: 0.01; onChanged: (v) => guiController.setAlgorithmParam("eco_kick_size", v) }
                                }
                                ColumnLayout {
                                    visible: algo.currentText === "Guided Local Search (GLS)"; Layout.fillWidth: true; spacing: 5
                                    Text { text: "GLS Search Strategy"; color: "#cbd5e1"; font.pixelSize: 12; font.bold: true }
                                    ComboBox {
                                        Layout.fillWidth: true; Layout.preferredHeight: 32
                                        model: ["2-Opt Search", "3-Opt Search", "5-Opt Search"]
                                        background: Rectangle { color: "#0f172a"; radius: 6; border.color: parent.activeFocus ? "#3b82f6" : "#334155" }
                                        contentItem: Text { leftPadding: 10; text: parent.displayText; color: "#e2e8f0"; font.pixelSize: 14; verticalAlignment: Text.AlignVCenter }
                                        onCurrentTextChanged: guiController.setAlgorithmParam("gls_strategy", currentText)
                                        Component.onCompleted: guiController.setAlgorithmParam("gls_strategy", currentText)
                                    }
                                    Param { label: "Lambda (Scaling)"; min: 0.01; max: 1.0; val: 0.1; step: 0.05; onChanged: (v) => guiController.setAlgorithmParam("gls_lambda", v) }
                                    Param { label: "Max Iterations"; min: 10; max: 5000; val: 1000; step: 50; onChanged: (v) => guiController.setAlgorithmParam("gls_iters", v) }
                                    Param { label: "Kick Interval"; min: 0; max: 500; val: 50; step: 5; onChanged: (v) => guiController.setAlgorithmParam("gls_kick_interval", v) }
                                    Param { label: "Kick Inner Steps"; min: 10; max: 1000; val: 100; step: 10; onChanged: (v) => guiController.setAlgorithmParam("gls_kick_steps", v) }
                                    Param { label: "Kick Reversal %"; min: 0.01; max: 0.5; val: 0.15; step: 0.01; onChanged: (v) => guiController.setAlgorithmParam("gls_kick_size", v) }
                                }
                                ColumnLayout {
                                    visible: algo.currentText === "Iterated Local Search (ILS)"; Layout.fillWidth: true; spacing: 5
                                    Text { text: "ILS Configuration (Large Random Reversal Only)"; color: "#cbd5e1"; font.pixelSize: 12; font.bold: true }

                                    Text { text: "Search Algorithm"; color: "#94a3b8"; font.pixelSize: 11; font.bold: true }
                                    ComboBox {
                                        Layout.fillWidth: true; Layout.preferredHeight: 40
                                        model: ["2-Opt Local Search", "3-Opt Local Search", "5-Opt Local Search"]
                                        background: Rectangle { color: "#0f172a"; radius: 6; border.color: parent.activeFocus ? "#3b82f6" : "#334155" }
                                        contentItem: Text { leftPadding: 10; text: parent.displayText; color: "#e2e8f0"; font.pixelSize: 14; verticalAlignment: Text.AlignVCenter }
                                        onCurrentTextChanged: guiController.setAlgorithmParam("ils_search_type", currentText)
                                        Component.onCompleted: guiController.setAlgorithmParam("ils_search_type", currentText)
                                    }
                                    Param { label: "Reversal Size (Max %)"; min:0.02; max:0.6; val:0.25; step:0.01; onChanged: (v) => guiController.setAlgorithmParam("ils_kick", v) }
                                    Param { label: "Steps (Iterations)"; min:50; max:5000; val:500; step:50; onChanged: (v) => guiController.setAlgorithmParam("ils_steps", v) }
                                }
                                ColumnLayout {
                                    visible: algo.currentText === "Guided Iterated Local Search (GILS)"; Layout.fillWidth: true; spacing: 0
                                    Param { label: "Lambda"; min:0.01; max:1.0; val:0.15; step:0.01; onChanged: (v) => guiController.setAlgorithmParam("gils_lambda", v) }
                                    Param { label: "Kick Interval"; min:20; max:500; val:120; step:10; onChanged: (v) => guiController.setAlgorithmParam("gils_kick_interval", v) }
                                    Param { label: "Kick Size %"; min:0.01; max:0.3; val:0.1; step:0.01; onChanged: (v) => guiController.setAlgorithmParam("gils_kick_size", v) }
                                    Param { label: "Max Steps"; min:100; max:5000; val:1000; step:100; onChanged: (v) => guiController.setAlgorithmParam("gils_steps", v) }
                                }
                                ColumnLayout {
                                    visible: algo.currentText === "Lin-Kernighan Algorithm (LK)"; Layout.fillWidth: true; spacing: 0
                                    Param { label: "Search Candidates"; min: 1; max: 500; val: 50; step: 1; onChanged: (v) => guiController.setAlgorithmParam("lk_cand", v) }
                                    Param { label: "Max Iterations"; min:10; max:2000; val:500; step:50; onChanged: (v) => guiController.setAlgorithmParam("lk_iters", v) }
                                }
                            }
                        }

                        // VISUALIZATION SECTION
                        ColumnLayout {
                            Layout.fillWidth: true; spacing: 12
                            RowLayout {
                                Text { text: "🎨 DISPLAY"; color: "#64748b"; font.pixelSize: 13; font.bold: true; font.letterSpacing: 2 }
                                Rectangle { Layout.fillWidth: true; height: 1; color: "#334155"; Layout.alignment: Qt.AlignVCenter }
                            }

                            Btn {
                                Layout.fillWidth: true; Layout.preferredHeight: 38
                                text: "COMPUTE DELAUNAY"
                                btnIcon: "📐"
                                bg: "#8b5cf6"; hbg: "#7c3aed"
                                onClicked: guiController.computeDelaunay()
                            }

                            CheckBox {
                                id: dtCheck; text: "Show Delaunay Mesh"; checked: guiController.showDelaunay; Layout.fillWidth: true
                                contentItem: Text { text: dtCheck.text; font.pixelSize: 15; color: "#94a3b8"; leftPadding: dtCheck.indicator.width + dtCheck.spacing; verticalAlignment: Text.AlignVCenter }
                                onCheckedChanged: guiController.showDelaunay = checked
                            }

                            Param { label: "City Size"; min: 0; max: 10; val: root.cityPointSize; step: 0.1; onChanged: (v) => root.cityPointSize = v }
                            Param { label: "Route Thickness"; min: 1; max: 10; val: root.routeLineThickness; step: 0.5; onChanged: (v) => root.routeLineThickness = v }

                            // Color Cards
                            GridLayout {
                                columns: 2; Layout.fillWidth: true; columnSpacing: 10; rowSpacing: 10
                                
                                ColumnLayout {
                                    Text { text: "CITY COLOR"; color: "#64748b"; font.pixelSize: 11; font.bold: true }
                                    Rectangle {
                                        Layout.fillWidth: true; height: 36; radius: 6; color: mv.cityColor; border.color: "#334155"
                                        MouseArea { anchors.fill: parent; onClicked: { proColorPicker.targetName = "cityColor"; proColorPicker.x = (root.width - proColorPicker.width) / 2; proColorPicker.y = (root.height - proColorPicker.height) / 2; proColorPicker.open(); } }
                                        Text { anchors.centerIn: parent; text: "SET"; color: "#fff"; font.bold: true; font.pixelSize: 12; style: Text.Outline; styleColor: "#000" }
                                    }
                                }
                                ColumnLayout {
                                    Text { text: "ROUTE COLOR"; color: "#64748b"; font.pixelSize: 11; font.bold: true }
                                    Rectangle {
                                        Layout.fillWidth: true; height: 36; radius: 6; color: mv.routeColor; border.color: "#334155"
                                        MouseArea { anchors.fill: parent; onClicked: { proColorPicker.targetName = "routeColor"; proColorPicker.x = (root.width - proColorPicker.width) / 2; proColorPicker.y = (root.height - proColorPicker.height) / 2; proColorPicker.open(); } }
                                        Text { anchors.centerIn: parent; text: "SET"; color: "#fff"; font.bold: true; font.pixelSize: 12; style: Text.Outline; styleColor: "#000" }
                                    }
                                }
                                ColumnLayout {
                                    Text { text: "MESH COLOR"; color: "#64748b"; font.pixelSize: 11; font.bold: true }
                                    Rectangle {
                                        Layout.fillWidth: true; height: 36; radius: 6; color: mv.delaunayColor; border.color: "#334155"
                                        MouseArea { anchors.fill: parent; onClicked: { proColorPicker.targetName = "delaunayColor"; proColorPicker.x = (root.width - proColorPicker.width) / 2; proColorPicker.y = (root.height - proColorPicker.height) / 2; proColorPicker.open(); } }
                                        Text { anchors.centerIn: parent; text: "SET"; color: "#fff"; font.bold: true; font.pixelSize: 12; style: Text.Outline; styleColor: "#000" }
                                    }
                                }
                                ColumnLayout {
                                    Text { text: "SYSTEM LOGS"; color: "#64748b"; font.pixelSize: 11; font.bold: true }
                                    RowLayout {
                                        Text { text: "Tracing"; color: "#94a3b8"; font.pixelSize: 13; Layout.fillWidth: true }
                                        Switch { checked: guiController.tracingEnabled; onToggled: guiController.tracingEnabled = checked; scale: 0.8 }
                                    }
                                }
                            }

                            GridLayout {
                                columns: 2; Layout.fillWidth: true; rowSpacing: 10; columnSpacing: 10
                                Btn {
                                    text: "Rotate CCW"; btnIcon: "↺"
                                    Layout.fillWidth: true; Layout.preferredHeight: 38
                                    bg: "#334155"; hbg: "#475569"
                                    onClicked: guiController.rotateCities(-90)
                                }
                                Btn {
                                    text: "Rotate CW"; btnIcon: "↻"
                                    Layout.fillWidth: true; Layout.preferredHeight: 38
                                    bg: "#334155"; hbg: "#475569"
                                    onClicked: guiController.rotateCities(90)
                                }
                                Btn {
                                    text: "H-Flip"; btnIcon: "↔"
                                    Layout.fillWidth: true; Layout.preferredHeight: 38
                                    bg: "#334155"; hbg: "#475569"
                                    onClicked: guiController.flipCities(true)
                                }
                                Btn {
                                    text: "V-Flip"; btnIcon: "↕"
                                    Layout.fillWidth: true; Layout.preferredHeight: 38
                                    bg: "#334155"; hbg: "#475569"
                                    onClicked: guiController.flipCities(false)
                                }
                            }

                            Btn {
                                Layout.fillWidth: true; Layout.preferredHeight: 38
                                text: "CLEAR MAP"
                                btnIcon: "🧹"
                                bg: "#94a3b8"; hbg: "#64748b"
                                onClicked: guiController.clearCities()
                            }
                        }
                    }
                }

                // Global Config Actions
                Rectangle { Layout.fillWidth: true; height: 1; color: "#334155" }
                RowLayout {
                    Layout.fillWidth: true; Layout.margins: 10; spacing: 10
                    Button {
                        id: resetConfigBtn; text: "RESET DEFAULTS"; Layout.fillWidth: true; Layout.preferredHeight: 38
                        onClicked: guiController.resetDefaults()
                        background: Rectangle { color: resetConfigBtn.down ? "#1e293b" : "transparent"; radius: 8; border.color: "#334155"; border.width: 1 }
                        contentItem: Text { text: "RESET DEFAULTS"; color: "#64748b"; font.bold: true; font.pixelSize: 11; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    }
                    Button {
                        id: saveConfigBtn; text: "SAVE CONFIG"; Layout.fillWidth: true; Layout.preferredHeight: 38
                        onClicked: guiController.saveSettings()
                        background: Rectangle { color: saveConfigBtn.down ? "#1e293b" : "#3b82f6"; radius: 8 }
                        contentItem: Text { text: "SAVE CONFIG"; color: "#fff"; font.bold: true; font.pixelSize: 11; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    }
                }

                // Terminal section
                Rectangle {
                    Layout.fillWidth: true; Layout.preferredHeight: 220; color: "#0f172a"
                    ColumnLayout {
                        anchors.fill: parent; spacing: 0
                        Rectangle {
                            Layout.fillWidth: true; height: 30; color: "#1e293b"
                            RowLayout {
                                anchors.fill: parent; anchors.margins: 8
                                Text { text: "📟 TERMINAL OUTPUT"; color: "#10b981"; font.pixelSize: 11; font.bold: true; font.letterSpacing: 1 }
                                Item { Layout.fillWidth: true }
                                Button {
                                    id: clearBtn; text: "CLEAR"; flat: true
                                    background: Rectangle { color: clearBtn.hovered ? "#334155" : "transparent"; radius: 4 }
                                    contentItem: Text { text: "CLEAR"; color: clearBtn.hovered ? "#f8fafc" : "#64748b"; font.bold: true; font.pixelSize: 10; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                                    onClicked: lt.text = "> Ready"
                                    implicitWidth: 50; implicitHeight: 20
                                }
                            }
                        }
                        Rectangle {
                            Layout.fillWidth: true; Layout.fillHeight: true; color: "#000000"; clip: true
                            Flickable {
                                id: lf; anchors.fill: parent; anchors.margins: 10; contentWidth: lt.width; contentHeight: lt.height
                                Text { id: lt; width: lf.width; wrapMode: Text.Wrap; color: "#10b981"; font.family: "Consolas"; font.pixelSize: 13; text: "> Ready" }
                                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                            }
                        }
                    }
                }
            }
        }
    }
    property int finalTime: 0
    Connections {
        target: guiController
        function onNewLogMessage(m){lt.text+="\n> "+m;lf.contentY=Math.max(0,lt.height-lf.height)}
        function onSolvingStatusChanged(s){
            isSolving = s;
            if (s) { solveTime = 0; currentDist = 0; }
        }
        function onMetricsUpdated(d,t){
            currentDist = d;
            if (!isSolving) finalTime = t;
        }
    }
}
