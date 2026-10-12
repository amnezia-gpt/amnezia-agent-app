import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root
    // QTextDocument normalizes CRLF. Keep the exact input outside the preview.
    property string bundle: ""
    signal pasteRequested()
    spacing: 8

    ScrollView {
        Layout.fillWidth: true
        Layout.preferredHeight: 160
        TextArea {
            objectName: "trustPreview"
            text: root.bundle
            readOnly: true
            wrapMode: TextEdit.WrapAnywhere
            selectByMouse: true
            placeholderText: qsTr("Public signing bundle JSON")
        }
    }

    Button {
        objectName: "pasteTrustButton"
        Layout.fillWidth: true
        text: qsTr("Paste public signing bundle")
        onClicked: root.pasteRequested()
    }
}
