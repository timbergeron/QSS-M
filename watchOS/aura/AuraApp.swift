// SPDX-License-Identifier: GPL-2.0-or-later

import SwiftUI
import WatchKit
import Foundation
import Combine
import Security

@main
struct AuraApp: App {
    @StateObject private var model = AuraModel()

    var body: some Scene {
        WindowGroup {
            AuraView(model: model)
        }
    }
}

private struct AuraView: View {
    @ObservedObject var model: AuraModel
    @Environment(\.scenePhase) private var scenePhase

    var body: some View {
        Group {
            if model.needsConfiguration {
                ConfigurationView(model: model)
            } else {
                ConnectedAuraView(model: model)
            }
        }
        .onAppear { model.setSceneAvailable(true) }
        .onChange(of: scenePhase) { phase in
            // A frontmost watch app is commonly .inactive while the wrist is
            // down. Keep the HTTP stream alive through that state so Aura can
            // continue updating the Always On display. Stop only in the
            // background, where watchOS may suspend the process.
            model.setSceneAvailable(phase != .background)
        }
    }
}

private struct ConnectedAuraView: View {
    @ObservedObject var model: AuraModel
    @State private var showingStatus = false
    @State private var hideStatusWorkItem: DispatchWorkItem?

    var body: some View {
        ZStack {
            AuraCanvas(aura: model.aura)
                .ignoresSafeArea()
            if showingStatus || model.connectionState != .connected {
                VStack(spacing: 8) {
                    if model.connectionState != .connected {
                        ProgressView()
                    }
                    Text(model.statusText)
                        .font(.footnote)
                        .multilineTextAlignment(.center)
                    Button {
                        hideStatusWorkItem?.cancel()
                        model.openConfiguration()
                    } label: {
                        Label("Settings", systemImage: "gearshape")
                    }
                }
                .padding(12)
                .background(.black.opacity(0.78), in: RoundedRectangle(cornerRadius: 16))
                .padding(.horizontal, 12)
                .transition(.opacity)
            }
        }
        .contentShape(Rectangle())
        .onTapGesture { revealStatus() }
        .animation(.easeInOut(duration: 0.2), value: showingStatus)
        .animation(.easeInOut(duration: 0.2), value: model.connectionState)
        .onDisappear { hideStatusWorkItem?.cancel() }
    }

    private func revealStatus() {
        showingStatus = true
        hideStatusWorkItem?.cancel()
        guard model.connectionState == .connected else { return }
        let work = DispatchWorkItem {
            showingStatus = false
        }
        hideStatusWorkItem = work
        DispatchQueue.main.asyncAfter(deadline: .now() + 3, execute: work)
    }
}

private struct AuraCanvas: View {
    let aura: AuraState

    var body: some View {
        // TimelineView keeps the static color eligible for refresh while a
        // frontmost app is inactive/Always On; watchOS throttles the cadence
        // automatically to protect battery life.
        TimelineView(.periodic(from: Date(), by: 1.0)) { _ in
            ZStack {
                Color.black
                if let color = aura.color {
                    color
                    .transition(.opacity)
                }
            }
        }
        .animation(.easeInOut(duration: 0.45), value: aura)
        .accessibilityElement(children: .ignore)
        .accessibilityLabel(aura.accessibilityLabel)
    }
}

private struct ConfigurationView: View {
    @ObservedObject var model: AuraModel
    @State private var mode: ConfigurationMode = .settings
    @State private var enteringAddress = false

    var body: some View {
        ScrollView {
            Group {
                switch mode {
                case .settings:
                    settingsView
                case .switchHost:
                    switchHostView
                case .pairHost:
                    pairingView
                }
            }
            .padding(.horizontal, 8)
        }
        .onAppear {
            if model.recentHosts.isEmpty || model.connectionState == .pairingRequired {
                mode = .pairHost
                startSearch()
            }
        }
        .onDisappear { model.stopDiscovery() }
    }

    private var settingsView: some View {
        VStack(spacing: 8) {
            Text("Aura")
                .font(.headline)
            Image(systemName: model.connectionState == .connected
                  ? "checkmark.circle.fill" : "arrow.trianglehead.2.clockwise")
                .font(.title2)
                .foregroundStyle(model.connectionState == .connected ? .green : .secondary)
            Text(model.statusText)
                .font(.footnote)
                .multilineTextAlignment(.center)
            if model.canCloseConfiguration {
                Button("Done") {
                    model.closeConfiguration()
                }
            }
            if model.recentHosts.count > 1 {
                Button("Switch Mac") {
                    mode = .switchHost
                }
            }
            Button("Add Another Mac") {
                mode = .pairHost
                startSearch()
            }
        }
    }

    private var switchHostView: some View {
        VStack(spacing: 8) {
            Text("Switch Mac")
                .font(.headline)
            ForEach(model.recentHosts) { recent in
                Button {
                    model.connect(to: recent)
                } label: {
                    HStack {
                        Text(recent.displayName)
                        Spacer()
                        if model.isCurrent(recent) {
                            Image(systemName: "checkmark")
                        }
                    }
                }
            }
            Button("Back") {
                mode = .settings
            }
        }
    }

    private var pairingView: some View {
        VStack(spacing: 8) {
            Text(model.recentHosts.isEmpty ? "Set Up Aura" : "Add QSS-M")
                .font(.headline)
            if enteringAddress {
                manualPairingView
            } else if model.isSearching {
                ProgressView()
                Text("Looking for QSS-M…")
                    .font(.footnote)
            } else if !model.foundHostName.isEmpty {
                Image(systemName: "checkmark.circle.fill")
                    .font(.title2)
                    .foregroundStyle(.green)
                Text(model.foundHostName)
                    .font(.headline)
                pairingCodeView
                Button("Enter Address Manually") {
                    enteringAddress = true
                }
            } else {
                Image(systemName: "wifi.exclamationmark")
                    .font(.title2)
                    .foregroundStyle(.secondary)
                Text("QSS-M wasn’t found.")
                    .font(.footnote)
                Button("Try Again") { startSearch() }
                Button("Enter Address Manually") {
                    enteringAddress = true
                }
            }
            if !model.connectionMessage.isEmpty {
                Text(model.connectionMessage)
                    .font(.footnote)
                    .multilineTextAlignment(.center)
            }
            if model.canCloseConfiguration {
                Button("Cancel") {
                    model.closeConfiguration()
                }
            }
        }
    }

    private var pairingCodeView: some View {
        VStack(spacing: 8) {
            Text("Run qwatch_pair in QSS-M, then enter its code.")
                .font(.footnote)
                .multilineTextAlignment(.center)
            TextField("6-digit code", text: $model.pairingCode)
                .textContentType(.oneTimeCode)
            Button(model.isPairing ? "Pairing…" : "Pair & Connect") {
                model.pairAndConnect()
            }
            .disabled(model.isPairing)
        }
    }

    private var manualPairingView: some View {
        VStack(spacing: 8) {
            TextField("Mac address", text: $model.pairingHost)
                .textContentType(.URL)
            TextField("Port", text: $model.pairingPort)
            pairingCodeView
            Button("Search Automatically") {
                enteringAddress = false
                startSearch()
            }
        }
    }

    private func startSearch() {
        enteringAddress = false
        model.restartDiscovery()
    }
}

private enum ConfigurationMode {
    case settings
    case switchHost
    case pairHost
}

enum AuraState: Int, Equatable {
    case off = 0
    case red = 1
    case blue = 2
    case purple = 3

    var color: Color? {
        switch self {
        case .off: return nil
        case .red: return .red
        case .blue: return .blue
        case .purple: return .purple
        }
    }

    var accessibilityLabel: String {
        switch self {
        case .off: return "Aura off"
        case .red: return "Pentagram aura"
        case .blue: return "Quad aura"
        case .purple: return "Pentagram and Quad aura"
        }
    }
}

private struct PairingResponse: Decodable {
    let token: String
}

private struct DiscoveryResponse: Decodable {
    let name: String
    let port: Int
}

struct RecentHost: Codable, Identifiable, Equatable {
    let name: String
    let host: String
    let port: Int
    let token: String
    let lastUsed: Date

    var id: String { "\(host.lowercased()):\(port)" }
    var displayName: String { name.isEmpty ? host : name }
}

private struct RecentHostMetadata: Codable {
    let name: String
    let host: String
    let port: Int
    let lastUsed: Date

    init(_ recent: RecentHost) {
        name = recent.name
        host = recent.host
        port = recent.port
        lastUsed = recent.lastUsed
    }
}

private enum TokenKeychain {
    private static let service = "com.qssm.aura.pairing"

    static func load(account: String) -> String? {
        let query: [String: Any] = [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: service,
            kSecAttrAccount as String: account,
            kSecReturnData as String: true,
            kSecMatchLimit as String: kSecMatchLimitOne
        ]
        var result: CFTypeRef?
        guard SecItemCopyMatching(query as CFDictionary, &result) == errSecSuccess,
              let data = result as? Data else { return nil }
        return String(data: data, encoding: .utf8)
    }

    @discardableResult
    static func save(_ token: String, account: String) -> Bool {
        guard let data = token.data(using: .utf8) else { return false }
        let identity: [String: Any] = [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: service,
            kSecAttrAccount as String: account
        ]
        let update: [String: Any] = [kSecValueData as String: data]
        let status = SecItemUpdate(identity as CFDictionary, update as CFDictionary)
        if status == errSecSuccess { return true }
        guard status == errSecItemNotFound else { return false }

        var item = identity
        item[kSecValueData as String] = data
        item[kSecAttrAccessible as String] = kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly
        return SecItemAdd(item as CFDictionary, nil) == errSecSuccess
    }

    static func delete(account: String) {
        let query: [String: Any] = [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: service,
            kSecAttrAccount as String: account
        ]
        SecItemDelete(query as CFDictionary)
    }
}

enum AuraConnectionState: Equatable {
    case disconnected
    case connecting
    case connected
    case reconnecting
    case pairingRequired
}

final class AuraModel: NSObject, ObservableObject, URLSessionDataDelegate {
    @Published var aura: AuraState = .off
    @Published var needsConfiguration = false
    @Published var host: String
    @Published var port: String
    @Published var token: String
    @Published var pairingHost: String
    @Published var pairingPort: String
    @Published var pairingCode = ""
    @Published var connectionMessage = ""
    @Published var isPairing = false
    @Published var discoveryMessage = ""
    @Published var isSearching = false
    @Published private(set) var recentHosts: [RecentHost]
    @Published private(set) var foundHostName = ""
    @Published private(set) var connectionState: AuraConnectionState = .disconnected

    private var session: URLSession?
    private var task: URLSessionDataTask?
    private var configured: Bool
    private var shouldRun = false
    private var sceneAvailable = false
    private var retryWorkItem: DispatchWorkItem?
    private var retryDelay: TimeInterval = 2
    private var hasReceivedInitialAura = false
    private var pairingTask: URLSessionDataTask?
    private var pairingAttempt = 0
    private var discoveryTask: URLSessionDataTask?
    private var discoveryAttempt = 0
    private var discoveredName = ""

    private static let recentHostsKey = "qssm_recent_hosts"

    override init() {
        let defaults = UserDefaults.standard
#if targetEnvironment(simulator)
        let defaultHost = "127.0.0.1"
#else
        let defaultHost = ""
#endif
        var savedHosts = AuraModel.loadRecentHosts(from: defaults)
        if savedHosts.isEmpty,
           let legacyHost = defaults.string(forKey: "qssm_host"),
           !legacyHost.isEmpty,
           let legacyToken = defaults.string(forKey: "qssm_token"),
           AuraModel.tokenLooksValid(legacyToken) {
            let legacyPort = Int(defaults.string(forKey: "qssm_port") ?? "27999") ?? 27999
            let legacy = RecentHost(name: legacyHost, host: legacyHost,
                                    port: legacyPort, token: legacyToken,
                                    lastUsed: Date())
            if TokenKeychain.save(legacyToken, account: legacy.id) {
                savedHosts = [legacy]
            }
        }
        let latest = savedHosts.first
        let initialHost = latest?.host ?? defaultHost
        let initialPort = String(latest?.port ?? 27999)
        let initialToken = latest?.token ?? ""
        host = initialHost
        port = initialPort
        token = initialToken
        pairingHost = initialHost
        pairingPort = initialPort
        recentHosts = savedHosts
        configured = !initialHost.isEmpty && AuraModel.tokenLooksValid(initialToken)
        needsConfiguration = !configured
        super.init()
        connectionState = configured ? .connecting : .disconnected
        persistRecentHosts()
        defaults.removeObject(forKey: "qssm_token")
    }

    func openConfiguration() {
        needsConfiguration = true
    }

    func closeConfiguration() {
        guard configured else { return }
        needsConfiguration = false
        start()
    }

    var canCloseConfiguration: Bool { configured }

    var statusText: String {
        switch connectionState {
        case .disconnected:
            return "Not connected"
        case .connecting:
            return "Connecting to \(currentHostName)…"
        case .connected:
            return aura == .off
                ? "Connected to \(currentHostName) · Aura off"
                : "Connected to \(currentHostName)"
        case .reconnecting:
            return "Reconnecting to \(currentHostName)…"
        case .pairingRequired:
            return "Pair with \(currentHostName) again"
        }
    }

    private var currentHostName: String {
        recentHosts.first(where: isCurrent)?.displayName ??
            host.trimmingCharacters(in: .whitespacesAndNewlines)
    }

    func isCurrent(_ recent: RecentHost) -> Bool {
        recent.host.caseInsensitiveCompare(host.trimmingCharacters(in: .whitespacesAndNewlines)) == .orderedSame &&
            recent.port == (Int(port) ?? 27999)
    }

    func startDiscovery() {
        guard discoveryTask == nil, let url = discoveryEndpoint else { return }

        isSearching = true
        foundHostName = ""
        discoveredName = ""
        pairingHost = ""
        pairingPort = "27999"
        connectionMessage = ""
        discoveryMessage = "Searching for QSS-M…"
        discoveryAttempt += 1
        let attempt = discoveryAttempt
        var request = URLRequest(url: url, timeoutInterval: 8)
        request.httpMethod = "GET"
        discoveryTask = URLSession.shared.dataTask(with: request) { [weak self] data, response, error in
            DispatchQueue.main.async {
                guard let self, self.discoveryAttempt == attempt else { return }
                self.discoveryTask = nil
                self.isSearching = false
                guard error == nil,
                      let http = response as? HTTPURLResponse,
                      http.statusCode == 200,
                      let data,
                      let discovery = try? JSONDecoder().decode(DiscoveryResponse.self, from: data),
                      (1024...65535).contains(discovery.port) else {
                    self.discoveryMessage = "No QSS-M found; enter the Mac address manually."
                    return
                }
                self.pairingHost = self.discoveryHost
                self.pairingPort = String(discovery.port)
                self.discoveredName = discovery.name
                self.foundHostName = discovery.name
                self.discoveryMessage = "Found \(discovery.name)."
            }
        }
        discoveryTask?.resume()
    }

    func restartDiscovery() {
        stopDiscovery()
        startDiscovery()
    }

    func stopDiscovery() {
        discoveryAttempt += 1
        discoveryTask?.cancel()
        discoveryTask = nil
        isSearching = false
    }

    private var discoveryHost: String {
#if targetEnvironment(simulator)
        "127.0.0.1"
#else
        "qssm-aura.local"
#endif
    }

    private var discoveryEndpoint: URL? {
        var components = URLComponents()
        components.scheme = "http"
        components.host = discoveryHost
        components.port = 27999
        components.path = "/v1/discover"
        return components.url
    }

    func pairAndConnect() {
        guard !isPairing else { return }
        guard pairingServerComponents != nil else {
            connectionMessage = "Enter the Mac address running QSS-M."
            return
        }
        guard pairingCode.count == 6,
              pairingCode.allSatisfy({ $0 >= "0" && $0 <= "9" }) else {
            connectionMessage = "Enter the six-digit code from qwatch_pair."
            return
        }
        guard let url = pairingEndpoint else {
            connectionMessage = "Check the host and port."
            return
        }

        isPairing = true
        pairingAttempt += 1
        let attempt = pairingAttempt
        connectionMessage = "Pairing with QSS-M…"
        var request = URLRequest(url: url, timeoutInterval: 10)
        request.httpMethod = "GET"
        pairingTask?.cancel()
        pairingTask = URLSession.shared.dataTask(with: request) { [weak self] data, response, error in
            DispatchQueue.main.async {
                guard let self else { return }
                guard self.pairingAttempt == attempt else { return }
                self.pairingTask = nil
                self.isPairing = false
                if error != nil {
                    self.connectionMessage = "QSS-M could not be reached."
                    return
                }
                guard let http = response as? HTTPURLResponse,
                      http.statusCode == 200,
                      let data,
                      let pairing = try? JSONDecoder().decode(PairingResponse.self, from: data),
                      self.isValidToken(pairing.token) else {
                    self.connectionMessage = "That code is invalid or expired."
                    return
                }
                self.stop()
                self.host = self.pairingHost.trimmingCharacters(in: .whitespacesAndNewlines)
                self.port = self.pairingPort
                self.token = pairing.token
                guard self.rememberCurrentHost(name: self.discoveredName) else {
                    self.configured = false
                    self.needsConfiguration = true
                    self.connectionMessage = "Pairing could not be saved securely. Try again."
                    return
                }
                self.configured = true
                self.needsConfiguration = false
                self.pairingCode = ""
                self.connectionMessage = ""
                self.start()
            }
        }
        pairingTask?.resume()
    }

    func connect(to recent: RecentHost) {
        stop()
        host = recent.host
        port = String(recent.port)
        token = recent.token
        pairingHost = recent.host
        pairingPort = String(recent.port)
        guard rememberCurrentHost(name: recent.name) else {
            connectionMessage = "That saved pairing is unavailable."
            return
        }
        configured = true
        needsConfiguration = false
        connectionMessage = ""
        start()
    }

    func start() {
        shouldRun = true
        retryWorkItem?.cancel()
        retryWorkItem = nil
        guard sceneAvailable, configured, task == nil else { return }
        connectionState = .connecting
        connect()
    }

    func setSceneAvailable(_ available: Bool) {
        sceneAvailable = available
        if available {
            start()
        } else {
            stop()
        }
    }

    private func connect() {
        guard shouldRun, sceneAvailable, configured, let url = endpoint else { return }
        task?.cancel()
        session?.invalidateAndCancel()
        task = nil
        session = nil
        hasReceivedInitialAura = false

        let configuration = URLSessionConfiguration.default
        // Aura is an intentionally idle stream between state changes. Do not
        // treat a quiet game or a map load as a disconnect.
        configuration.timeoutIntervalForRequest = .greatestFiniteMagnitude
        configuration.timeoutIntervalForResource = .greatestFiniteMagnitude
        session = URLSession(configuration: configuration, delegate: self, delegateQueue: .main)

        var request = URLRequest(url: url)
        request.httpMethod = "GET"
        request.setValue("Bearer \(token)", forHTTPHeaderField: "Authorization")
        task = session?.dataTask(with: request)
        task?.resume()
    }

    func stop() {
        shouldRun = false
        retryWorkItem?.cancel()
        retryWorkItem = nil
        task?.cancel()
        task = nil
        session?.invalidateAndCancel()
        session = nil
        pairingAttempt += 1
        pairingTask?.cancel()
        pairingTask = nil
        isPairing = false
        aura = .off
        connectionState = .disconnected
    }

    private var endpoint: URL? {
        guard var components = serverComponents else { return nil }
        components.path = "/v1/aura"
        return components.url
    }

    private var pairingEndpoint: URL? {
        guard var components = pairingServerComponents else { return nil }
        components.path = "/v1/pair"
        components.queryItems = [URLQueryItem(name: "code", value: pairingCode)]
        return components.url
    }

    private var serverComponents: URLComponents? {
        let cleanHost = host.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !cleanHost.isEmpty else { return nil }
        let cleanPort = Int(port) ?? 27999
        guard (1024...65535).contains(cleanPort) else { return nil }
        var components = URLComponents()
        components.scheme = "http"
        components.host = cleanHost
        components.port = cleanPort
        return components
    }

    private var pairingServerComponents: URLComponents? {
        let cleanHost = pairingHost.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !cleanHost.isEmpty else { return nil }
        let cleanPort = Int(pairingPort) ?? 27999
        guard (1024...65535).contains(cleanPort) else { return nil }
        var components = URLComponents()
        components.scheme = "http"
        components.host = cleanHost
        components.port = cleanPort
        return components
    }

    @discardableResult
    private func rememberCurrentHost(name: String) -> Bool {
        let cleanHost = host.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !cleanHost.isEmpty,
              let cleanPort = Int(port),
              (1024...65535).contains(cleanPort),
              isValidToken(token) else { return false }

        let discoveredHostName = cleanHost.caseInsensitiveCompare(discoveryHost) == .orderedSame
            ? name.trimmingCharacters(in: .whitespacesAndNewlines)
            : ""
        let recent = RecentHost(name: discoveredHostName, host: cleanHost,
                                port: cleanPort, token: token,
                                lastUsed: Date())
        guard TokenKeychain.save(token, account: recent.id) else { return false }
        recentHosts.removeAll { $0.id == recent.id }
        recentHosts.insert(recent, at: 0)
        if recentHosts.count > 3 {
            let removed = recentHosts.suffix(from: 3)
            for old in removed where old.id != recent.id {
                TokenKeychain.delete(account: old.id)
            }
            recentHosts.removeLast(recentHosts.count - 3)
        }

        persistRecentHosts()
        let defaults = UserDefaults.standard
        // Keep the original keys current so existing installations migrate
        // cleanly between builds that predate recent-host support.
        defaults.set(cleanHost, forKey: "qssm_host")
        defaults.set(String(cleanPort), forKey: "qssm_port")
        defaults.removeObject(forKey: "qssm_token")
        return true
    }

    private static func loadRecentHosts(from defaults: UserDefaults) -> [RecentHost] {
        guard let data = defaults.data(forKey: recentHostsKey),
              !data.isEmpty else {
            return []
        }
        if let legacy = try? JSONDecoder().decode([RecentHost].self, from: data) {
            return legacy.filter {
                tokenLooksValid($0.token) && TokenKeychain.save($0.token, account: $0.id)
            }
        }
        guard let metadata = try? JSONDecoder().decode([RecentHostMetadata].self, from: data) else {
            return []
        }
        return metadata.compactMap { saved -> RecentHost? in
            let id = "\(saved.host.lowercased()):\(saved.port)"
            guard let token = TokenKeychain.load(account: id), tokenLooksValid(token) else {
                return nil
            }
            return RecentHost(name: saved.name, host: saved.host, port: saved.port,
                              token: token, lastUsed: saved.lastUsed)
        }
            .filter {
                !$0.host.isEmpty &&
                (1024...65535).contains($0.port) &&
                tokenLooksValid($0.token)
            }
            .sorted { $0.lastUsed > $1.lastUsed }
            .prefix(3)
            .map { $0 }
    }

    private func persistRecentHosts() {
        let metadata = recentHosts.map(RecentHostMetadata.init)
        if let data = try? JSONEncoder().encode(metadata) {
            UserDefaults.standard.set(data, forKey: AuraModel.recentHostsKey)
        }
    }

    private static func tokenLooksValid(_ value: String) -> Bool {
        value.count == 64 && value.allSatisfy({ $0.isHexDigit })
    }

    private func isValidToken(_ value: String) -> Bool {
        AuraModel.tokenLooksValid(value)
    }

    func urlSession(_ session: URLSession, dataTask: URLSessionDataTask,
                    didReceive response: URLResponse,
                    completionHandler: @escaping (URLSession.ResponseDisposition) -> Void) {
        guard dataTask === self.task,
              let http = response as? HTTPURLResponse else {
            completionHandler(.cancel)
            return
        }
        if http.statusCode == 401 {
            requirePairing()
            completionHandler(.cancel)
            session.invalidateAndCancel()
            return
        }
        guard
              http.statusCode == 200,
              let contentType = http.value(forHTTPHeaderField: "Content-Type"),
              contentType.lowercased().hasPrefix("application/octet-stream") else {
            completionHandler(.cancel)
            return
        }
        retryDelay = 2
        connectionState = .connected
        completionHandler(.allow)
    }

    private func requirePairing() {
        let failedID = "\(host.lowercased()):\(Int(port) ?? 27999)"
        shouldRun = false
        retryWorkItem?.cancel()
        retryWorkItem = nil
        task = nil
        session = nil
        aura = .off
        configured = false
        connectionState = .pairingRequired
        pairingHost = host
        pairingPort = port
        TokenKeychain.delete(account: failedID)
        recentHosts.removeAll { $0.id == failedID }
        persistRecentHosts()
        connectionMessage = "The saved pairing expired. Pair with QSS-M again."
        needsConfiguration = true
    }

    func urlSession(_ session: URLSession, dataTask: URLSessionDataTask, didReceive data: Data) {
        guard shouldRun, sceneAvailable, configured, dataTask === self.task else { return }
        for value in data {
            guard let next = AuraState(rawValue: Int(value)) else {
                dataTask.cancel()
                return
            }
            let notify = hasReceivedInitialAura
            hasReceivedInitialAura = true
            guard next != aura else { continue }
            aura = next
            if notify {
                WKInterfaceDevice.current().play(next == .off ? .directionDown : .click)
            }
        }
    }

    func urlSession(_ session: URLSession, task: URLSessionTask, didCompleteWithError error: Error?) {
        guard shouldRun, sceneAvailable, configured, task === self.task else { return }
        aura = .off
        connectionState = .reconnecting
        self.session = nil
        self.task = nil
        scheduleRetry()
    }

    private func scheduleRetry() {
        guard shouldRun, sceneAvailable, configured else { return }
        retryWorkItem?.cancel()
        let delay = retryDelay
        retryDelay = min(retryDelay * 2, 60)
        let work = DispatchWorkItem { [weak self] in
            guard let self else { return }
            self.retryWorkItem = nil
            self.connect()
        }
        retryWorkItem = work
        DispatchQueue.main.asyncAfter(deadline: .now() + delay, execute: work)
    }
}
