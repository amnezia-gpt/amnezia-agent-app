import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import PageEnum 1.0
import Style 1.0

import "./"
import "../Controls2"
import "../Controls2/TextTypes"
import "../Config"
import "../Components"

PageType {
    id: root

    BackButtonType {
        id: backButton

        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.topMargin: 20 + PageController.safeAreaTopMargin
    }

    ListViewType {
        id: listView
        anchors.top: backButton.bottom
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        anchors.left: parent.left

        header: ColumnLayout {
            width: listView.width

            BaseHeaderType {
                Layout.fillWidth: true
                Layout.rightMargin: 16
                Layout.leftMargin: 16

                headerText: "Dev menu"
            }
        }
        
        model: 1 // fake model to force the ListView to be created without a model

        spacing: 16

        delegate: ColumnLayout {
            width: listView.width

            TextFieldWithHeaderType {
                id: gatewayEndpointField

                Layout.fillWidth: true
                Layout.topMargin: 16
                Layout.rightMargin: 16
                Layout.leftMargin: 16

                headerText: qsTr("Gateway endpoint")
                textField.text: SettingsController.gatewayEndpoint

                buttonImageSource: textField.text !== "" ? "qrc:/images/controls/refresh-cw.svg" : ""

                clickedFunc: function() {
                    SettingsController.resetGatewayEndpoint()
                    gatewayEndpointField.textField.text = SettingsController.gatewayEndpoint
                }
            }

            BasicButtonType {
                id: saveButton

                Layout.fillWidth: true
                Layout.margins: 16

                text: qsTr("Save")

                clickedFunc: function() {
                    var trimmed = gatewayEndpointField.textField.text.replace(/^\s+|\s+$/g, '')
                    gatewayEndpointField.textField.text = trimmed
                    if (trimmed !== SettingsController.gatewayEndpoint) {
                        SettingsController.gatewayEndpoint = trimmed
                    }
                    PageController.showNotificationMessage(qsTr("Settings saved"))
                }
            }
        }

        footer: ColumnLayout {
            width: listView.width

            ParagraphTextType {
                Layout.fillWidth: true
                Layout.margins: 16
                text: qsTr("Agent workload environment (new installations)")
            }

            ComboBox {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                model: ["local", "dev"]
                currentIndex: model.indexOf(SettingsController.agentWorkloadEnvironment)
                onActivated: SettingsController.agentWorkloadEnvironment = currentText
            }

            ParagraphTextType {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                text: qsTr("Local uses the local backend through its public tunnel. Existing deployments keep their saved environment.")
            }

            TextFieldWithHeaderType {
                id: localAuthField
                visible: SettingsController.agentWorkloadEnvironment === "local"
                Layout.fillWidth: true
                Layout.margins: 16
                headerText: qsTr("Local Auth issuer (HTTPS)")
                textField.text: SettingsController.localAgentBackendProfile.authIssuer || ""
            }

            TextFieldWithHeaderType {
                id: localRouterField
                visible: SettingsController.agentWorkloadEnvironment === "local"
                Layout.fillWidth: true
                Layout.margins: 16
                headerText: qsTr("Local Router base URL (HTTPS, /v1)")
                textField.text: SettingsController.localAgentBackendProfile.routerBaseUrl || ""
            }

            TextFieldWithHeaderType {
                id: localRuntimeField
                visible: SettingsController.agentWorkloadEnvironment === "local"
                Layout.fillWidth: true
                Layout.margins: 16
                headerText: qsTr("Local Runtime Gateway base URL (HTTPS, without /v1)")
                textField.text: SettingsController.localAgentBackendProfile.runtimeGatewayBaseUrl || ""
            }

            BasicButtonType {
                visible: SettingsController.agentWorkloadEnvironment === "local"
                Layout.fillWidth: true
                Layout.margins: 16
                text: qsTr("Save local backend")
                clickedFunc: function() {
                    if (!SettingsController.saveLocalAgentBackendProfile(localAuthField.textField.text,
                                                                         localRouterField.textField.text,
                                                                         localRuntimeField.textField.text)) {
                        PageController.showNotificationMessage(qsTr("Enter all three valid HTTPS endpoints without credentials, query or fragment; Router must end in /v1 and Runtime Gateway must not."))
                        return
                    }
                    PageController.showNotificationMessage(qsTr("Settings saved"))
                }
            }

            SwitcherType {
                Layout.fillWidth: true
                Layout.topMargin: 24
                Layout.rightMargin: 16
                Layout.leftMargin: 16

                text: qsTr("Dev gateway environment")
                checked: SettingsController.isDevGatewayEnv
                onToggled: function() {
                    SettingsController.isDevGatewayEnv = checked
                }
            }
        }
    }
}
