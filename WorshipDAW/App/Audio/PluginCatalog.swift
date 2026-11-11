import AVFoundation
import AudioToolbox
import WorshipDAWCore

#if os(macOS)
import AppKit
typealias PlatformViewController = NSViewController
#else
import UIKit
typealias PlatformViewController = UIViewController
#endif

/// Un Audio Unit de efecto instalado en el sistema (AUv2 en Mac, AUv3 en Mac y iPhone).
/// Aquí aparecen también los plugins Worship Sounds (Vocal Rack, Guitar, Mastering).
struct AvailablePlugin: Identifiable, Hashable {
    let name: String
    let manufacturerName: String
    let componentType: UInt32
    let componentSubType: UInt32
    let componentManufacturer: UInt32

    var id: String { "\(componentType)-\(componentSubType)-\(componentManufacturer)" }

    var isWorshipSounds: Bool {
        manufacturerName.localizedCaseInsensitiveContains("worship")
            || name.localizedCaseInsensitiveContains("worship")
    }

    func makeRef() -> PluginRef {
        PluginRef(name: name, manufacturerName: manufacturerName, componentType: componentType,
                  componentSubType: componentSubType, componentManufacturer: componentManufacturer)
    }
}

enum PluginError: LocalizedError {
    case instantiationFailed(String)
    var errorDescription: String? {
        switch self {
        case .instantiationFailed(let name): "No se pudo cargar el plugin \(name)."
        }
    }
}

enum PluginCatalog {
    static func availableEffects() -> [AvailablePlugin] {
        let manager = AVAudioUnitComponentManager.shared()
        let types: [OSType] = [kAudioUnitType_Effect, kAudioUnitType_MusicEffect]
        let components = types.flatMap { type in
            manager.components(matching: AudioComponentDescription(
                componentType: type, componentSubType: 0, componentManufacturer: 0,
                componentFlags: 0, componentFlagsMask: 0))
        }
        var seen = Set<String>()
        return components.compactMap { c -> AvailablePlugin? in
            let d = c.audioComponentDescription
            let plugin = AvailablePlugin(name: c.name, manufacturerName: c.manufacturerName,
                                         componentType: d.componentType, componentSubType: d.componentSubType,
                                         componentManufacturer: d.componentManufacturer)
            return seen.insert(plugin.id).inserted ? plugin : nil
        }
        .sorted { lhs, rhs in
            // Worship Sounds primero, después alfabético.
            if lhs.isWorshipSounds != rhs.isWorshipSounds { return lhs.isWorshipSounds }
            return lhs.name.localizedCaseInsensitiveCompare(rhs.name) == .orderedAscending
        }
    }

    static func description(for ref: PluginRef) -> AudioComponentDescription {
        AudioComponentDescription(componentType: ref.componentType, componentSubType: ref.componentSubType,
                                  componentManufacturer: ref.componentManufacturer,
                                  componentFlags: 0, componentFlagsMask: 0)
    }

    static func instantiate(_ ref: PluginRef) async throws -> AVAudioUnit {
        let desc = description(for: ref)
        let unit: AVAudioUnit = try await withCheckedThrowingContinuation { continuation in
            AVAudioUnit.instantiate(with: desc, options: []) { unit, error in
                if let unit {
                    continuation.resume(returning: unit)
                } else {
                    continuation.resume(throwing: error ?? PluginError.instantiationFailed(ref.name))
                }
            }
        }
        if let data = ref.state,
           let state = try? PropertyListSerialization.propertyList(from: data, format: nil) as? [String: Any] {
            unit.auAudioUnit.fullState = state
        }
        unit.auAudioUnit.shouldBypassEffect = ref.isBypassed
        return unit
    }

    static func serializedState(of unit: AVAudioUnit) -> Data? {
        guard let state = unit.auAudioUnit.fullState else { return nil }
        return try? PropertyListSerialization.data(fromPropertyList: state, format: .binary, options: 0)
    }

    static func requestViewController(for unit: AVAudioUnit) async -> PlatformViewController? {
        await withCheckedContinuation { continuation in
            unit.auAudioUnit.requestViewController { vc in
                continuation.resume(returning: vc)
            }
        }
    }
}
