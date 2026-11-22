// Copyright Azahar for Frame contributors
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

// AzaharVR: the certificates OpenSSL trusts on Windows. The static LibreSSL has no store of its own
// and Windows keeps no PEM file, so one is written per run: the Mozilla roots beside the program
// (certs/cacert.pem, curl's extract) and the roots of Windows' own store (which also holds what a
// school or company added). Windows only; elsewhere the callers find the distribution's file.

#pragma once

#ifdef _WIN32

namespace Common {

/// The PEM file's path, or nullptr when no certificate at all could be had. Made once per run
/// (any thread); <cache dir>/ca-bundle.pem.
const char* WindowsCaBundle();

} // namespace Common

#endif
