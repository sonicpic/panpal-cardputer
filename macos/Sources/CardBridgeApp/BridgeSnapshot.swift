import Foundation

struct AgentHello: Codable, Equatable, Sendable {
    struct Agent: Codable, Equatable, Sendable {
        let version: String
        let build: Int
    }

    struct API: Codable, Equatable, Sendable {
        let major: Int
        let minor: Int
    }

    let type: String
    let agent: Agent
    let api: API

    enum CodingKeys: String, CodingKey {
        case type = "t"
        case agent, api
    }

    var compatibilityError: String? {
        guard agent.version == GeneratedVersion.agent,
              agent.build == GeneratedVersion.agentBuild else {
            return L10n.format(
                "App %@ (%@) 与 Agent %@ (%@) 不匹配",
                GeneratedVersion.app,
                String(GeneratedVersion.appBuild),
                agent.version,
                String(agent.build)
            )
        }
        guard api.major == GeneratedVersion.agentAPIMajor else {
            return L10n.format("Agent API %@ 不兼容", "\(api.major).\(api.minor)")
        }
        return nil
    }
}

struct BridgeSnapshot: Codable, Equatable, Sendable {
    struct Agent: Codable, Equatable, Sendable {
        struct API: Codable, Equatable, Sendable {
            let major: Int
            let minor: Int
        }

        let state: String
        let version: String
        let build: Int
        let api: API
        let pid: Int
        let startedAtMS: Int64
        let bridgeID: String
        let macName: String
        let lanAddress: String
        let tcpPort: Int
        let udpPort: Int
        let hookPort: Int?
        let issues: [String]
        let lastError: String

        enum CodingKeys: String, CodingKey {
            case state, version, build, api, pid, issues
            case startedAtMS = "started_at_ms"
            case bridgeID = "bridge_id"
            case macName = "mac_name"
            case lanAddress = "lan_address"
            case tcpPort = "tcp_port"
            case udpPort = "udp_port"
            case hookPort = "hook_port"
            case lastError = "last_error"
        }
    }

    struct Permissions: Codable, Equatable, Sendable {
        let accessibility: Bool
    }

    struct Audio: Codable, Equatable, Sendable {
        let enabled: Bool
        let running: Bool
        let device: String?
        let gain: Double
        let sampleRate: Double?
        let received: Int
        let lost: Int
        let late: Int
        let resyncs: Int

        enum CodingKeys: String, CodingKey {
            case enabled, running, device, gain, received, lost, late, resyncs
            case sampleRate = "sample_rate"
        }
    }

    struct Codex: Codable, Equatable, Sendable {
        let enabled: Bool
        let connected: Bool
        let executable: String?
        let hooksListening: Bool
        let hooksInstalled: Bool
        let sessions: Int
        let quotaAvailable: Bool

        enum CodingKeys: String, CodingKey {
            case enabled, connected, executable, sessions
            case hooksListening = "hooks_listening"
            case hooksInstalled = "hooks_installed"
            case quotaAvailable = "quota_available"
        }
    }

    struct Device: Codable, Equatable, Identifiable, Sendable {
        struct ProtocolVersion: Codable, Equatable, Sendable {
            let major: Int
            let minor: Int
        }

        let id: String
        let ip: String
        let model: String
        let firmware: String
        let firmwareBuild: String
        let `protocol`: ProtocolVersion
        let compatibility: String
        let capabilities: [String]
        let connectedAtMS: Int64
        let lastSeenMS: Int64
        let audioPackets: Int

        enum CodingKeys: String, CodingKey {
            case id, ip, model, firmware, `protocol`, compatibility, capabilities
            case firmwareBuild = "firmware_build"
            case connectedAtMS = "connected_at_ms"
            case lastSeenMS = "last_seen_ms"
            case audioPackets = "audio_packets"
        }
    }

    struct PairedDevice: Codable, Equatable, Identifiable, Sendable {
        let id: String
        let name: String
        let pairedAt: Int64

        enum CodingKeys: String, CodingKey {
            case id, name
            case pairedAt = "paired_at"
        }
    }

    struct Pairing: Codable, Equatable, Sendable {
        let deviceID: String
        let code: String
        let createdAtMS: Int64

        enum CodingKeys: String, CodingKey {
            case code
            case deviceID = "device_id"
            case createdAtMS = "created_at_ms"
        }
    }

    let type: String
    let sequence: Int
    let agent: Agent
    let permissions: Permissions
    let audio: Audio
    let codex: Codex
    let devices: [Device]
    let pairedDevices: [PairedDevice]
    let pairing: Pairing?

    enum CodingKeys: String, CodingKey {
        case type = "t"
        case sequence = "seq"
        case agent, permissions, audio, codex, devices, pairing
        case pairedDevices = "paired_devices"
    }
}

extension BridgeSnapshot {
    static let empty = BridgeSnapshot(
        type: "snapshot",
        sequence: 0,
        agent: Agent(
            state: "offline",
            version: GeneratedVersion.agent,
            build: GeneratedVersion.agentBuild,
            api: .init(
                major: GeneratedVersion.agentAPIMajor,
                minor: GeneratedVersion.agentAPIMinor
            ),
            pid: 0,
            startedAtMS: 0,
            bridgeID: "",
            macName: "",
            lanAddress: "",
            tcpPort: 7788,
            udpPort: 7789,
            hookPort: nil,
            issues: [],
            lastError: ""
        ),
        permissions: Permissions(accessibility: false),
        audio: Audio(
            enabled: true,
            running: false,
            device: nil,
            gain: 1,
            sampleRate: nil,
            received: 0,
            lost: 0,
            late: 0,
            resyncs: 0
        ),
        codex: Codex(
            enabled: true,
            connected: false,
            executable: nil,
            hooksListening: false,
            hooksInstalled: false,
            sessions: 0,
            quotaAvailable: false
        ),
        devices: [],
        pairedDevices: [],
        pairing: nil
    )
}
