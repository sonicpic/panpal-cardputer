import AppKit
import SwiftUI

struct BridgeMenuView: View {
    @ObservedObject var client: AgentClient
    @ObservedObject private var microphoneDriver = MicrophoneDriverManager.shared

    private var connectedDevice: BridgeSnapshot.Device? {
        client.snapshot.devices.first
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            header

            if let pairing = client.snapshot.pairing {
                pairingCard(pairing)
            }

            if let device = connectedDevice {
                deviceCard(device)
            } else {
                emptyDeviceCard
            }

            Divider()
            healthRows
            if !microphoneDriver.isInstalled {
                Button {
                    Task {
                        if await microphoneDriver.install() {
                            client.restartAgent()
                        }
                    }
                } label: {
                    Label("启用 CardBridge 麦克风…", systemImage: "mic.badge.plus")
                        .frame(maxWidth: .infinity)
                }
                .buttonStyle(.borderedProminent)
                .disabled(microphoneDriver.isBusy)
            }
            if !client.snapshot.permissions.accessibility {
                Button {
                    SystemSettings.openAccessibility()
                } label: {
                    Label("授权键盘控制…", systemImage: "hand.raised")
                        .frame(maxWidth: .infinity)
                }
                .buttonStyle(.borderedProminent)
            }
            Divider()
            footer
        }
        .padding(16)
        .frame(width: 360)
    }

    private var header: some View {
        HStack(alignment: .center, spacing: 12) {
            ZStack {
                Circle()
                    .fill(statusColor.opacity(0.14))
                Image(systemName: statusSymbol)
                    .font(.system(size: 18, weight: .semibold))
                    .foregroundStyle(statusColor)
            }
            .frame(width: 42, height: 42)

            VStack(alignment: .leading, spacing: 2) {
                Text("PanPal")
                    .font(.headline)
                Text(statusTitle)
                    .font(.subheadline)
                    .foregroundStyle(.secondary)
            }
            Spacer()
            Text("v\(GeneratedVersion.app)")
                .font(.caption.monospacedDigit())
                .foregroundStyle(.tertiary)
        }
    }

    private func pairingCard(_ pairing: BridgeSnapshot.Pairing) -> some View {
        VStack(alignment: .leading, spacing: 8) {
            Label("M5 请求配对", systemImage: "link.badge.plus")
                .font(.subheadline.weight(.semibold))
            Text(pairing.code)
                .font(.system(size: 30, weight: .bold, design: .monospaced))
                .textSelection(.enabled)
            Text("在 Cardputer 上输入这个六位码")
                .font(.caption)
                .foregroundStyle(.secondary)
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(12)
        .background(.blue.opacity(0.09), in: RoundedRectangle(cornerRadius: 12))
    }

    private func deviceCard(_ device: BridgeSnapshot.Device) -> some View {
        VStack(alignment: .leading, spacing: 10) {
            HStack {
                Label("Cardputer ADV", systemImage: "rectangle.and.hand.point.up.left")
                    .font(.subheadline.weight(.semibold))
                Spacer()
                Text("已连接")
                    .font(.caption.weight(.medium))
                    .foregroundStyle(.green)
            }
            Grid(alignment: .leading, horizontalSpacing: 14, verticalSpacing: 5) {
                GridRow {
                    Text("地址").foregroundStyle(.secondary)
                    Text(device.ip).textSelection(.enabled)
                }
                GridRow {
                    Text("固件").foregroundStyle(.secondary)
                    Text("\(device.firmware) (\(device.firmwareBuild))")
                }
                GridRow {
                    Text("协议").foregroundStyle(.secondary)
                    Text("\(device.protocol.major).\(device.protocol.minor) · \(device.compatibility)")
                }
            }
            .font(.caption)
        }
        .padding(12)
        .background(.primary.opacity(0.055), in: RoundedRectangle(cornerRadius: 12))
    }

    private var emptyDeviceCard: some View {
        HStack(spacing: 10) {
            Image(systemName: "wifi")
                .foregroundStyle(.secondary)
            VStack(alignment: .leading, spacing: 2) {
                Text("等待 M5 连接")
                    .font(.subheadline.weight(.medium))
                Text("确保 Mac 和 Cardputer 位于同一网络")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(12)
        .background(.primary.opacity(0.04), in: RoundedRectangle(cornerRadius: 12))
    }

    private var healthRows: some View {
        VStack(spacing: 8) {
            HealthRow(
                title: L10n.text("本地网络"),
                detail: client.snapshot.agent.issues.contains("network")
                    ? L10n.text("本地网络不可用")
                    : client.snapshot.agent.lanAddress,
                symbol: "network",
                healthy: !client.snapshot.agent.issues.contains("network")
            )
            HealthRow(
                title: L10n.text("键盘控制"),
                detail: client.snapshot.permissions.accessibility
                    ? L10n.text("Accessibility 已授权")
                    : L10n.text("需要授权"),
                symbol: "keyboard",
                healthy: client.snapshot.permissions.accessibility
            )
            HealthRow(
                title: L10n.text("麦克风桥接"),
                detail: client.snapshot.audio.device ?? L10n.text("CardBridge 麦克风不可用"),
                symbol: "waveform",
                healthy: client.snapshot.audio.running
            )
            HealthRow(
                title: L10n.text("Codex 状态"),
                detail: client.snapshot.codex.connected
                    ? L10n.format("%@ 个会话", String(client.snapshot.codex.sessions))
                    : L10n.text("未连接"),
                symbol: "terminal",
                healthy: client.snapshot.codex.connected
            )
        }
    }

    private var footer: some View {
        HStack {
            Button {
                if client.connectionState == .stopped {
                    client.startBridge()
                } else {
                    client.stopBridge()
                }
            } label: {
                Label(
                    client.connectionState == .stopped
                        ? L10n.text("启动桥接")
                        : L10n.text("停止桥接"),
                    systemImage: client.connectionState == .stopped ? "play.fill" : "stop.fill"
                )
            }
            .buttonStyle(.borderless)

            if client.connectionState != .stopped {
                Button {
                    client.restartAgent()
                } label: {
                    Image(systemName: "arrow.clockwise")
                }
                .buttonStyle(.borderless)
                .help("重启桥接")
                .accessibilityLabel("重启桥接")
            }

            Spacer()

            Button("设置…") {
                NSApp.sendAction(Selector(("showSettingsWindow:")), to: nil, from: nil)
            }
            .buttonStyle(.borderless)

            Button("检查更新…") {
                UpdaterController.shared.checkForUpdates()
            }
            .buttonStyle(.borderless)

            Button("退出") {
                NSApp.terminate(nil)
            }
            .buttonStyle(.borderless)
        }
        .font(.caption)
    }

    private var statusTitle: String {
        switch client.connectionState {
        case .connected:
            if connectedDevice != nil { return L10n.text("M5 已连接") }
            return L10n.text("桥接器已就绪")
        case .connecting:
            return L10n.text("正在连接桥接器…")
        case let .incompatible(message), let .failed(message):
            return message
        case .stopped:
            return L10n.text("桥接器未启动")
        }
    }

    private var statusSymbol: String {
        switch client.connectionState {
        case .connected:
            return connectedDevice == nil ? "checkmark" : "link"
        case .connecting:
            return "arrow.triangle.2.circlepath"
        case .incompatible, .failed:
            return "exclamationmark"
        case .stopped:
            return "pause"
        }
    }

    private var statusColor: Color {
        switch client.connectionState {
        case .connected:
            return connectedDevice == nil ? .blue : .green
        case .connecting:
            return .orange
        case .incompatible, .failed:
            return .red
        case .stopped:
            return .secondary
        }
    }
}

private struct HealthRow: View {
    let title: String
    let detail: String
    let symbol: String
    let healthy: Bool

    var body: some View {
        HStack(spacing: 9) {
            Image(systemName: symbol)
                .frame(width: 18)
                .foregroundStyle(.secondary)
            Text(title)
                .font(.caption)
            Spacer()
            Text(detail)
                .font(.caption)
                .foregroundStyle(.secondary)
                .lineLimit(1)
            Image(systemName: healthy ? "checkmark.circle.fill" : "exclamationmark.circle.fill")
                .foregroundStyle(healthy ? .green : .orange)
        }
        .accessibilityElement(children: .ignore)
        .accessibilityLabel(Text(title))
        .accessibilityValue(Text(detail))
    }
}
