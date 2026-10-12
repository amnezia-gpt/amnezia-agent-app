import QtQuick
import QtQuick.Controls
import QtTest
import "../../../client/ui/qml/Components" as Client

Item {
    id: root
    width: 520
    height: 300
    property string clipboardBundle: ""

    Client.PublicTrustBundleInput {
        id: input
        width: 500
        onPasteRequested: bundle = clipboardBundle
    }
    TestCase {
    name: "PublicTrustBundleInput"
    when: windowShown
    SignalSpy { id: pasteSpy; target: input; signalName: "pasteRequested" }

    function init() { input.bundle = ""; clipboardBundle = ""; pasteSpy.clear() }
    function test_pastePreservesExactBytes_data() {
        return [
            { tag: "LF", value: '{"schema_version":1}\n' },
            { tag: "CRLF", value: '{"schema_version":1}\r\n' },
            { tag: "Unicode", value: '{"kid":"ключ\'@@IMAGE@@"}\n\n' }
        ]
    }
    function test_pastePreservesExactBytes(data) {
        clipboardBundle = data.value
        var button = findChild(input, "pasteTrustButton")
        tryVerify(function() { return button.width > 0 && button.height > 0 })
        mouseClick(button, button.width / 2, button.height / 2)
        compare(pasteSpy.count, 1)
        compare(input.bundle, data.value)
        verify(findChild(input, "trustPreview").readOnly)
    }
    function test_savedProfileKeepsCRLFDespitePreviewNormalization() {
        var saved = '{"issuer":"https://auth.example"}\r\n'
        input.bundle = saved
        compare(input.bundle, saved)
        compare(findChild(input, "trustPreview").text, saved.replace(/\r\n/g, "\n"))
    }
    function test_previewCannotEditBundle() {
        input.bundle = '{"schema_version":1}\n'
        var preview = findChild(input, "trustPreview")
        preview.forceActiveFocus()
        keyClick(Qt.Key_A)
        compare(input.bundle, '{"schema_version":1}\n')
    }
}
}
