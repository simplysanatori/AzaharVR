// Copyright Azahar for Frame contributors
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

// AzaharVR: see windows_ca_bundle.h.

#ifdef _WIN32

#include "common/windows_ca_bundle.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincrypt.h>
#include "common/file_util.h"
#include "common/logging/log.h"
#include "common/string_util.h"

namespace Common {

namespace {

namespace fs = std::filesystem;

// The certificates of one of the current user's system stores (which include the machine's), PEM.
std::string StorePem(const wchar_t* name, int& count) {
    std::string pem;
    HCERTSTORE store = CertOpenSystemStoreW(0, name);
    if (store == nullptr) {
        return pem;
    }
    PCCERT_CONTEXT cert = nullptr;
    while ((cert = CertEnumCertificatesInStore(store, cert)) != nullptr) {
        if ((cert->dwCertEncodingType & X509_ASN_ENCODING) == 0) {
            continue;
        }
        const DWORD flags = CRYPT_STRING_BASE64HEADER | CRYPT_STRING_NOCR;
        DWORD size = 0;
        if (!CryptBinaryToStringA(cert->pbCertEncoded, cert->cbCertEncoded, flags, nullptr, &size)) {
            continue;
        }
        std::string text(size, '\0');
        if (!CryptBinaryToStringA(cert->pbCertEncoded, cert->cbCertEncoded, flags, text.data(), &size)) {
            continue;
        }
        text.resize(size);
        while (!text.empty() && text.back() == '\0') {
            text.pop_back();
        }
        pem += text;
        count++;
    }
    CertCloseStore(store, 0);
    return pem;
}

} // namespace

const char* WindowsCaBundle() {
    static std::once_flag once;
    static std::string path;
    std::call_once(once, [] {
        std::string pem;
        const fs::path shipped =
            fs::path(UTF8ToUTF16W(FileUtil::GetExeDirectory())) / L"certs" / L"cacert.pem";
        if (std::ifstream f{shipped, std::ios::binary}) {
            pem.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
            if (!pem.empty() && pem.back() != '\n') {
                pem += '\n';
            }
        }
        const bool have_shipped = !pem.empty();
        int count = 0;
        pem += StorePem(L"ROOT", count);
        if (pem.empty()) {
            LOG_ERROR(Common, "certificates: neither certs/cacert.pem nor Windows' store has any");
            return;
        }
        const std::string dir = FileUtil::GetUserPath(FileUtil::UserPath::CacheDir);
        FileUtil::CreateFullPath(dir);
        const std::string out = dir + "ca-bundle.pem";
        const fs::path out_w(UTF8ToUTF16W(out));
        // A temporary name of this process's own, so two copies starting together do not share it
        const fs::path tmp_w(UTF8ToUTF16W(out + "." + std::to_string(GetCurrentProcessId()) + ".tmp"));
        std::error_code ec;
        {
            std::ofstream f(tmp_w, std::ios::binary | std::ios::trunc);
            f.write(pem.data(), static_cast<std::streamsize>(pem.size()));
            if (!f) {
                ec = std::make_error_code(std::errc::io_error);
            }
        }
        if (!ec) {
            fs::rename(tmp_w, out_w, ec);
        }
        if (ec) {
            std::error_code ignore;
            fs::remove(tmp_w, ignore);
            // Another copy of the program holds it open, or wrote it a moment ago: use that one
            if (fs::is_regular_file(out_w, ignore) && fs::file_size(out_w, ignore) > 0) {
                LOG_WARNING(Common, "certificates: could not write {} ({}); using the one there",
                            out, ec.message());
                path = out;
            } else {
                LOG_ERROR(Common, "certificates: could not write {} ({})", out, ec.message());
            }
            return;
        }
        LOG_INFO(Common, "certificates: {} ({}the Mozilla roots, {} from Windows' store)", out,
                 have_shipped ? "" : "not ", count);
        path = out;
    });
    return path.empty() ? nullptr : path.c_str();
}

} // namespace Common

#endif
