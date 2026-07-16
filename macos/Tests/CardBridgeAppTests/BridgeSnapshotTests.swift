import AppKit
import XCTest
@testable import CardBridgeApp

final class BridgeSnapshotTests: XCTestCase {
    func testEveryMenuBarStatusSymbolExists() {
        for symbol in MenuBarStatusSymbols.all {
            XCTAssertNotNil(
                NSImage(systemSymbolName: symbol, accessibilityDescription: nil),
                "Missing menu bar SF Symbol: \(symbol)"
            )
        }
    }

    func testAgentHandshakeRequiresExactBundledBuild() throws {
        let matching = AgentHello(
            type: "hello_ok",
            agent: .init(version: GeneratedVersion.agent, build: GeneratedVersion.agentBuild),
            api: .init(major: GeneratedVersion.agentAPIMajor, minor: GeneratedVersion.agentAPIMinor)
        )
        XCTAssertNil(matching.compatibilityError)

        let stale = AgentHello(
            type: "hello_ok",
            agent: .init(version: GeneratedVersion.agent, build: GeneratedVersion.agentBuild + 1),
            api: matching.api
        )
        XCTAssertNotNil(stale.compatibilityError)
    }

    func testDiagnosticsRedactTokensPairCodesAndHomePath() {
        let token = String(repeating: "ab", count: 32)
        let input = "/Users/example/config token \(token) pairing code for m5: 483291"
        let output = DiagnosticExporter.redact(input, home: "/Users/example")
        XCTAssertFalse(output.contains(token))
        XCTAssertFalse(output.contains("483291"))
        XCTAssertFalse(output.contains("/Users/example"))
        XCTAssertTrue(output.contains("<redacted-token>"))
        XCTAssertTrue(output.contains("<redacted-code>"))
    }

    func testDecodesLiveAgentShapeAndRetainsNoTokenField() throws {
        let json = #"""
        {
          "t":"snapshot",
          "seq":7,
          "agent":{"state":"connected","version":"0.2.0","build":1,"api":{"major":1,"minor":0},"pid":42,"started_at_ms":100,"bridge_id":"bridge","mac_name":"Mac","lan_address":"192.168.1.2","tcp_port":7788,"udp_port":7789,"hook_port":7790,"issues":[],"last_error":""},
          "permissions":{"accessibility":true},
          "audio":{"enabled":true,"running":true,"device":"BlackHole 2ch","gain":8.0,"sample_rate":48000,"received":10,"lost":0,"late":0,"resyncs":0},
          "codex":{"enabled":true,"connected":true,"executable":"/usr/bin/codex","hooks_listening":true,"hooks_installed":true,"sessions":3,"quota_available":true},
          "devices":[{"id":"m5","ip":"192.168.1.3","model":"cardputer-adv","firmware":"0.2.0","firmware_build":"1","protocol":{"major":2,"minor":0},"compatibility":"ok","capabilities":["control.keys.v1"],"connected_at_ms":101,"last_seen_ms":102,"audio_packets":9}],
          "paired_devices":[{"id":"m5","name":"Cardputer","paired_at":99}],
          "pairing":null
        }
        """#

        let snapshot = try JSONDecoder().decode(BridgeSnapshot.self, from: Data(json.utf8))
        XCTAssertEqual(snapshot.agent.state, "connected")
        XCTAssertEqual(snapshot.devices.first?.firmwareBuild, "1")
        XCTAssertEqual(snapshot.devices.first?.protocol.major, 2)
        XCTAssertTrue(snapshot.permissions.accessibility)
        XCTAssertFalse(String(data: try JSONEncoder().encode(snapshot), encoding: .utf8)!.contains("token"))
    }
}
