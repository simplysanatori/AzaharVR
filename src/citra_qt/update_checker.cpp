// Copyright 2025-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

// AzaharVR updatecheck: one HTTPS GET to GitHub's API for this project's latest release. Nothing
// else is sent (no game, user or system information; the headers are the HTTP client's own).

#include <array>
#include <optional>
#include <string>
#include <httplib.h>
#include "citra_qt/update_checker.h"
#include "citra_qt/update_version.h"
#include "common/file_util.h"
#include "common/logging/log.h"
#ifdef _WIN32
#include "common/windows_ca_bundle.h"
#endif

namespace {

// The static LibreSSL has no CA store of its own; the same lookup as the other downloads.
const char* CaBundle() {
#ifdef _WIN32
    return Common::WindowsCaBundle();
#else
    static constexpr std::array<const char*, 4> candidates{
        "/etc/ssl/certs/ca-certificates.crt", "/etc/pki/tls/certs/ca-bundle.crt",
        "/etc/ssl/cert.pem", "/etc/ssl/ca-bundle.pem"};
    for (const char* path : candidates) {
        if (FileUtil::Exists(path)) {
            return path;
        }
    }
    return nullptr;
#endif
}

} // namespace

std::optional<std::string> UpdateChecker::GetLatestRelease() {
    constexpr const char* host = "https://api.github.com";
    constexpr const char* path = "/repos/simplysanatori/AzaharVR/releases/latest";
    constexpr std::size_t max_body = 1024 * 1024;

    const char* ca = CaBundle();
    if (ca == nullptr) {
        LOG_INFO(Frontend, "Update check: no certificate store found");
        return std::nullopt;
    }
    try {
        httplib::Client client(host);
        client.set_connection_timeout(5);
        client.set_read_timeout(10);
        client.set_write_timeout(10);
        client.set_follow_location(false);
        client.set_ca_cert_path(ca);
        client.enable_server_certificate_verification(true);

        bool too_large = false;
        std::string body;
        const auto res = client.Get(
            path, httplib::Headers{}, [](const httplib::Response&) { return true; },
            [&](const char* data, std::size_t len) {
                if (body.size() + len > max_body) {
                    too_large = true;
                    return false;
                }
                body.append(data, len);
                return true;
            });
        if (too_large || !res) {
            LOG_INFO(Frontend, "Update check: no answer");
            return std::nullopt;
        }
        if (res->status != 200) {
            LOG_INFO(Frontend, "Update check: status {}", res->status);
            return std::nullopt;
        }
        return TagFromReleaseJson(body);
    } catch (...) {
        return std::nullopt;
    }
}
