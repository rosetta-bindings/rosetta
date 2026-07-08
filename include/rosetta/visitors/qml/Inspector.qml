// SPDX-FileCopyrightText: Copyright (c) fmaerten@gmail.com
// SPDX-License-Identifier: UNLICENSED

// Generic inspector. Every field/method row is rendered from the
// metadata produced by rosetta::QmlVisitor — no per-Person markup.
//
// Field row layout:    label  [editor]              (commits on edit)
// Method row layout:   label  [args...]  Call   <result>
//
// There are no GET / PUT buttons — each editor commits on its natural
// signal (TextField editingFinished, SpinBox valueModified, CheckBox
// toggled, ComboBox activated, Slider release).
//
// Annotations surface here as:
//   readonly  -> editor disabled, "[ro]" suffix on the label
//   range     -> hint shown next to label, error toast on invalid commit
//   combobox  -> editor becomes a drop-down of the allowed choices
//   widget    -> picks the editor when more than one would fit (e.g.
//                int + range can be a SpinBox or a Slider; int 0/1 can
//                be a CheckBox)
//   doc       -> tooltip on label

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import QtQuick.Window

ApplicationWindow {
    id: root
    width: 760
    height: 640
    visible: true
    title: inspector.typeName + " — QML inspector"

    background: Rectangle { color: "#1a1a1a" }

    function logLine(msg, ok) {
        var ts = new Date().toISOString().substr(11, 8);
        var color = ok ? "#4cb050" : "#e57373";
        logArea.append('<span style="color:#555">' + ts + '</span> '
                       + '<span style="color:' + color + '">' + msg + '</span>');
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        // Label {
        //     text: inspector.typeName + " — generated from one C++26 reflection walk"
        //     color: "#888"
        //     font.pixelSize: 13
        // }

        // -------- Fields --------
        Rectangle {
            Layout.fillWidth: true
            color: "#262626"
            border.color: "#3a3a3a"
            radius: 6
            implicitHeight: fieldsCol.implicitHeight + 24

            ColumnLayout {
                id: fieldsCol
                anchors.fill: parent
                anchors.margins: 12
                spacing: 8

                Label {
                    text: "FIELDS"
                    color: "#888"
                    font.pixelSize: 11
                    font.letterSpacing: 1
                }

                Repeater {
                    model: inspector.fields
                    delegate: RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        function commit(v) {
                            var err = inspector.setField(modelData.name, v);
                            if (err.length === 0)
                                root.logLine(modelData.name + " ← "
                                             + JSON.stringify(v), true);
                            else
                                root.logLine(modelData.name + " failed: " + err, false);
                        }

                        // doc tooltip on hover
                        Label {
                            text: modelData.displayName
                                  + (modelData.hasRange
                                     ? " [" + modelData.min + ".." + modelData.max + "]"
                                     : "")
                                  + (modelData.readonly ? " [ro]" : "")
                            color: "#bbb"
                            Layout.preferredWidth: 160
                            ToolTip.visible: hoverHandler.hovered && modelData.doc.length > 0
                            ToolTip.text: modelData.doc
                            HoverHandler { id: hoverHandler }
                        }

                        Loader {
                            id: editorLoader
                            Layout.fillWidth: true
                            sourceComponent: {
                                if (modelData.widget === "radio"
                                    && modelData.hasChoices)         return radioEditor;
                                if (modelData.hasChoices)            return comboEditor;
                                if (modelData.widget === "color")    return colorEditor;
                                if (modelData.widget === "multiline")return multilineEditor;
                                if (modelData.widget === "file")     return fileEditor;
                                if (modelData.widget === "slider"
                                    && modelData.hasRange)           return sliderEditor;
                                if (modelData.widget === "checkbox") return boolEditor;
                                if (modelData.widget === "textfield"
                                    && modelData.type === "int")     return intTextEditor;
                                if (modelData.type === "int")        return intEditor;
                                if (modelData.type === "double")     return doubleEditor;
                                if (modelData.type === "bool")       return boolEditor;
                                return stringEditor;
                            }
                            property var fieldInfo: modelData
                            property var currentValue: modelData.value

                            Component {
                                id: stringEditor
                                TextField {
                                    text: editorLoader.currentValue !== undefined
                                          ? editorLoader.currentValue : ""
                                    enabled: !editorLoader.fieldInfo.readonly
                                    onTextChanged: editorLoader.currentValue = text
                                    onEditingFinished: commit(text)
                                }
                            }
                            Component {
                                id: intTextEditor
                                TextField {
                                    text: String(editorLoader.currentValue || 0)
                                    enabled: !editorLoader.fieldInfo.readonly
                                    validator: IntValidator {
                                        bottom: editorLoader.fieldInfo.hasRange
                                                ? editorLoader.fieldInfo.min : -2147483648
                                        top:    editorLoader.fieldInfo.hasRange
                                                ? editorLoader.fieldInfo.max :  2147483647
                                    }
                                    onTextChanged: editorLoader.currentValue =
                                        parseInt(text || "0")
                                    onEditingFinished: commit(parseInt(text || "0"))
                                }
                            }
                            Component {
                                id: intEditor
                                SpinBox {
                                    from: editorLoader.fieldInfo.hasRange
                                          ? editorLoader.fieldInfo.min : -2147483648
                                    to:   editorLoader.fieldInfo.hasRange
                                          ? editorLoader.fieldInfo.max :  2147483647
                                    value: editorLoader.currentValue || 0
                                    enabled: !editorLoader.fieldInfo.readonly
                                    onValueModified: {
                                        editorLoader.currentValue = value;
                                        commit(value);
                                    }
                                }
                            }
                            Component {
                                id: doubleEditor
                                TextField {
                                    text: String(editorLoader.currentValue || 0)
                                    enabled: !editorLoader.fieldInfo.readonly
                                    validator: DoubleValidator {}
                                    onTextChanged: editorLoader.currentValue = parseFloat(text)
                                    onEditingFinished: commit(parseFloat(text))
                                }
                            }
                            Component {
                                id: boolEditor
                                CheckBox {
                                    checked: !!editorLoader.currentValue
                                    enabled: !editorLoader.fieldInfo.readonly
                                    onToggled: {
                                        editorLoader.currentValue = checked;
                                        commit(checked);
                                    }
                                }
                            }
                            Component {
                                id: comboEditor
                                ComboBox {
                                    model: editorLoader.fieldInfo.choices
                                    enabled: !editorLoader.fieldInfo.readonly
                                    currentIndex: {
                                        var v = editorLoader.currentValue;
                                        for (var i = 0; i < model.length; ++i)
                                            if (model[i] === v) return i;
                                        return 0;
                                    }
                                    onActivated: {
                                        editorLoader.currentValue = model[currentIndex];
                                        commit(model[currentIndex]);
                                    }
                                }
                            }
                            Component {
                                id: radioEditor
                                Row {
                                    spacing: 10
                                    Repeater {
                                        model: editorLoader.fieldInfo.choices
                                        delegate: RadioButton {
                                            text: modelData
                                            checked: modelData === editorLoader.currentValue
                                            enabled: !editorLoader.fieldInfo.readonly
                                            onClicked: {
                                                editorLoader.currentValue = text;
                                                commit(text);
                                            }
                                        }
                                    }
                                }
                            }
                            Component {
                                id: colorEditor
                                RowLayout {
                                    spacing: 8
                                    Rectangle {
                                        width: 48; height: 24; radius: 4
                                        border.color: "#666"
                                        color: editorLoader.currentValue || "#000000"
                                        MouseArea {
                                            anchors.fill: parent
                                            enabled: !editorLoader.fieldInfo.readonly
                                            onClicked: colorDlg.open()
                                        }
                                    }
                                    Label {
                                        text: editorLoader.currentValue || ""
                                        color: "#bbb"
                                    }
                                    ColorDialog {
                                        id: colorDlg
                                        selectedColor: editorLoader.currentValue || "#000000"
                                        onAccepted: {
                                            var hex = selectedColor.toString();
                                            editorLoader.currentValue = hex;
                                            commit(hex);
                                        }
                                    }
                                }
                            }
                            Component {
                                id: multilineEditor
                                Frame {
                                    padding: 0
                                    implicitHeight: 90
                                    TextArea {
                                        anchors.fill: parent
                                        text: editorLoader.currentValue !== undefined
                                              ? editorLoader.currentValue : ""
                                        enabled: !editorLoader.fieldInfo.readonly
                                        wrapMode: TextEdit.Wrap
                                        onTextChanged: editorLoader.currentValue = text
                                        // TextArea has no editingFinished — commit on focus-out
                                        onActiveFocusChanged: {
                                            if (!activeFocus) commit(text);
                                        }
                                    }
                                }
                            }
                            Component {
                                id: fileEditor
                                RowLayout {
                                    spacing: 8
                                    TextField {
                                        Layout.fillWidth: true
                                        text: editorLoader.currentValue !== undefined
                                              ? editorLoader.currentValue : ""
                                        enabled: !editorLoader.fieldInfo.readonly
                                        onTextChanged: editorLoader.currentValue = text
                                        onEditingFinished: commit(text)
                                    }
                                    Button {
                                        text: "…"
                                        implicitWidth: 36
                                        enabled: !editorLoader.fieldInfo.readonly
                                        onClicked: fileDlg.open()
                                    }
                                    FileDialog {
                                        id: fileDlg
                                        onAccepted: {
                                            var p = String(selectedFile)
                                                    .replace(/^file:\/\//, "");
                                            editorLoader.currentValue = p;
                                            commit(p);
                                        }
                                    }
                                }
                            }
                            Component {
                                id: sliderEditor
                                RowLayout {
                                    spacing: 8
                                    Slider {
                                        Layout.fillWidth: true
                                        from: editorLoader.fieldInfo.min
                                        to:   editorLoader.fieldInfo.max
                                        stepSize: editorLoader.fieldInfo.type === "int" ? 1 : 0
                                        value: editorLoader.currentValue || 0
                                        enabled: !editorLoader.fieldInfo.readonly
                                        onMoved: editorLoader.currentValue =
                                            (editorLoader.fieldInfo.type === "int"
                                             ? Math.round(value) : value)
                                        onPressedChanged: {
                                            if (!pressed) commit(editorLoader.currentValue);
                                        }
                                    }
                                    Label {
                                        text: editorLoader.currentValue
                                        color: "#bbb"
                                        Layout.preferredWidth: 40
                                        horizontalAlignment: Text.AlignRight
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // -------- Methods --------
        Rectangle {
            Layout.fillWidth: true
            color: "#262626"
            border.color: "#3a3a3a"
            radius: 6
            implicitHeight: methodsCol.implicitHeight + 24

            ColumnLayout {
                id: methodsCol
                anchors.fill: parent
                anchors.margins: 12
                spacing: 8

                Label {
                    text: "METHODS"
                    color: "#888"
                    font.pixelSize: 11
                    font.letterSpacing: 1
                }

                Repeater {
                    model: inspector.methods
                    delegate: RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Label {
                            visible: !(modelData.button && modelData.button.length > 0)
                            text: modelData.displayName + "(" + modelData.arity + ")"
                                  + (modelData.isStatic ? " [static]" : "")
                            color: "#bbb"
                            Layout.preferredWidth: 160
                            ToolTip.visible: methHover.hovered && modelData.doc.length > 0
                            ToolTip.text: modelData.doc
                            HoverHandler { id: methHover }
                        }

                        // One TextField per parameter. argsRepeater.itemAt(i) gives us back
                        // the field so the Call handler can collect values.
                        Repeater {
                            id: argsRepeater
                            model: modelData.paramTypes
                            delegate: TextField {
                                Layout.preferredWidth: 110
                                placeholderText: modelData
                            }
                        }

                        Item {
                            Layout.fillWidth: true
                            visible: !(modelData.button && modelData.button.length > 0)
                        }

                        Button {
                            text: modelData.button && modelData.button.length > 0
                                  ? modelData.button : "Call"
                            onClicked: {
                                var args = [];
                                for (var i = 0; i < argsRepeater.count; ++i) {
                                    var t = argsRepeater.itemAt(i);
                                    var v = t.text;
                                    if (modelData.paramTypes[i] === "int")
                                        v = parseInt(v || "0");
                                    else if (modelData.paramTypes[i] === "double")
                                        v = parseFloat(v || "0");
                                    else if (modelData.paramTypes[i] === "bool")
                                        v = (v === "true" || v === "1");
                                    args.push(v);
                                }
                                var result = inspector.callMethod(modelData.name, args);
                                root.logLine("CALL " + modelData.name + "("
                                             + args.join(", ") + ") → "
                                             + JSON.stringify(result), true);
                            }
                        }
                    }
                }
            }
        }

        // -------- Log --------
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "#0e0e0e"
            border.color: "#3a3a3a"
            radius: 6

            ScrollView {
                anchors.fill: parent
                anchors.margins: 8
                TextArea {
                    id: logArea
                    readOnly: true
                    textFormat: TextEdit.RichText
                    color: "#e8e8e8"
                    font.family: "Menlo"
                    font.pixelSize: 12
                    background: null
                    wrapMode: TextEdit.Wrap
                }
            }
        }
    }
}
