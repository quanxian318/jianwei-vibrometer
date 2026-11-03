import Foundation
import Darwin

/// UDP discovery as in the Android and Windows shells: "VIBMON_DISCOVER" to
/// port 48300, answers whose "service" is "vibmon", one per ip:port, each with
/// the sender's address added as "ip". iOS only allows broadcast with Apple's
/// multicast entitlement, so besides the limited broadcast and the gateway's
/// own address (192.168.4.1) the probe goes unicast to every host of the
/// Wi-Fi subnet (up to /22), which only needs the local-network permission.
enum Discovery {
    static let port: UInt16 = 48300

    static func run(timeoutMs: Int) -> [[String: Any]] {
        let fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)
        guard fd >= 0 else { return [] }
        defer { close(fd) }
        var yes: Int32 = 1
        setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &yes, socklen_t(MemoryLayout<Int32>.size))
        var tv = timeval(tv_sec: 0, tv_usec: 250_000)
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, socklen_t(MemoryLayout<timeval>.size))

        let targets = probeTargets()
        let probe = Array("VIBMON_DISCOVER".utf8)
        let end = Date().addingTimeInterval(Double(max(400, min(6000, timeoutMs))) / 1000)
        var nextProbe = Date.distantPast
        var seen = Set<String>()
        var found: [[String: Any]] = []
        var buf = [UInt8](repeating: 0, count: 2048)
        while Date() < end {
            if Date() >= nextProbe {
                for t in targets {
                    var to = sockaddr_in()
                    to.sin_len = UInt8(MemoryLayout<sockaddr_in>.size)
                    to.sin_family = sa_family_t(AF_INET)
                    to.sin_port = port.bigEndian
                    to.sin_addr.s_addr = t.bigEndian
                    _ = withUnsafePointer(to: &to) {
                        $0.withMemoryRebound(to: sockaddr.self, capacity: 1) {
                            sendto(fd, probe, probe.count, 0, $0, socklen_t(MemoryLayout<sockaddr_in>.size))
                        }
                    }
                }
                nextProbe = Date().addingTimeInterval(0.6)
            }
            var from = sockaddr_in()
            var len = socklen_t(MemoryLayout<sockaddr_in>.size)
            let n = withUnsafeMutablePointer(to: &from) {
                $0.withMemoryRebound(to: sockaddr.self, capacity: 1) { recvfrom(fd, &buf, buf.count, 0, $0, &len) }
            }
            guard n > 0,
                  var o = (try? JSONSerialization.jsonObject(with: Data(buf[0..<n]))) as? [String: Any],
                  o["service"] as? String == "vibmon" else { continue }
            let ip = String(cString: inet_ntoa(from.sin_addr))
            let key = "\(ip):\(o["http_port"] as? Int ?? 80)"
            if !seen.insert(key).inserted { continue }
            o["ip"] = ip
            found.append(o)
        }
        return found
    }

    /// Host-order IPv4 targets.
    static func probeTargets() -> [UInt32] {
        var out: [UInt32] = [0xFFFF_FFFF, 0xC0A8_0401]  // 255.255.255.255, 192.168.4.1
        var ifa: UnsafeMutablePointer<ifaddrs>?
        guard getifaddrs(&ifa) == 0, let first = ifa else { return out }
        defer { freeifaddrs(ifa) }
        for p in sequence(first: first, next: { $0.pointee.ifa_next }) {
            let i = p.pointee
            guard let a = i.ifa_addr, a.pointee.sa_family == UInt8(AF_INET), let m = i.ifa_netmask,
                  (Int32(i.ifa_flags) & IFF_UP) != 0, (Int32(i.ifa_flags) & IFF_LOOPBACK) == 0 else { continue }
            let name = String(cString: i.ifa_name)
            guard name.hasPrefix("en") || name.hasPrefix("bridge") else { continue }  // Wi-Fi / hotspot
            let ip = a.withMemoryRebound(to: sockaddr_in.self, capacity: 1) { UInt32(bigEndian: $0.pointee.sin_addr.s_addr) }
            let mask = m.withMemoryRebound(to: sockaddr_in.self, capacity: 1) { UInt32(bigEndian: $0.pointee.sin_addr.s_addr) }
            let net = ip & mask, bcast = net | ~mask
            out.append(bcast)
            if ~mask <= 1023 {
                var h = net + 1
                while h < bcast { if h != ip { out.append(h) }; h += 1 }
            }
        }
        return out
    }
}
