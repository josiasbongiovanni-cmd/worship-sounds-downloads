// swift-tools-version:5.9
import PackageDescription

let package = Package(
    name: "WorshipDAWCore",
    platforms: [.macOS(.v14), .iOS(.v17)],
    products: [
        .library(name: "WorshipDAWCore", targets: ["WorshipDAWCore"]),
    ],
    targets: [
        .target(name: "WorshipDAWCore"),
        .testTarget(name: "WorshipDAWCoreTests", dependencies: ["WorshipDAWCore"]),
    ]
)
