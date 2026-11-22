// Generates a dedicated Ed25519 key pair for QWeather JWT authentication.
//
// macOS ships LibreSSL, whose `openssl genpkey` cannot create Ed25519 keys,
// so this uses Apple CryptoKit instead.
//
//   swift tools/qweather_keygen.swift [private-key-path]
//
// The private key is written as PKCS#8 PEM with mode 0600 and is never
// printed. Only the public key (SPKI PEM, the format the QWeather console
// expects) goes to stdout. Never commit, upload, or paste the private key.

import CryptoKit
import Foundation

let defaultPath = FileManager.default.homeDirectoryForCurrentUser
  .appendingPathComponent("qweather-ed25519-private.pem").path
let privatePath = CommandLine.arguments.count > 1
  ? CommandLine.arguments[1] : defaultPath

if FileManager.default.fileExists(atPath: privatePath) {
  FileHandle.standardError.write(
    "Refusing to overwrite existing key: \(privatePath)\n".data(using: .utf8)!)
  exit(1)
}

func pem(_ label: String, _ der: Data) -> String {
  let body = der.base64EncodedString(options: [.lineLength64Characters,
                                               .endLineWithLineFeed])
  return "-----BEGIN \(label)-----\n\(body)\n-----END \(label)-----\n"
}

let key = Curve25519.Signing.PrivateKey()
// RFC 8410 fixed DER prefixes for Ed25519 PKCS#8 private and SPKI public keys.
let pkcs8 = Data([0x30, 0x2e, 0x02, 0x01, 0x00, 0x30, 0x05, 0x06, 0x03, 0x2b,
                  0x65, 0x70, 0x04, 0x22, 0x04, 0x20]) + key.rawRepresentation
let spki = Data([0x30, 0x2a, 0x30, 0x05, 0x06, 0x03, 0x2b, 0x65, 0x70, 0x03,
                 0x21, 0x00]) + key.publicKey.rawRepresentation

// Self-check: a signature made with the private key verifies with the public.
let probe = Data("qweather-keygen-self-test".utf8)
guard let signature = try? key.signature(for: probe),
      key.publicKey.isValidSignature(signature, for: probe) else {
  FileHandle.standardError.write("Ed25519 self-test failed.\n".data(using: .utf8)!)
  exit(1)
}

let created = FileManager.default.createFile(
  atPath: privatePath, contents: pem("PRIVATE KEY", pkcs8).data(using: .utf8),
  attributes: [.posixPermissions: 0o600])
guard created else {
  FileHandle.standardError.write("Cannot write \(privatePath)\n".data(using: .utf8)!)
  exit(1)
}

FileHandle.standardError.write(
  "Private key saved (mode 600): \(privatePath)\nPublic key for the QWeather console:\n\n"
    .data(using: .utf8)!)
print(pem("PUBLIC KEY", spki), terminator: "")
