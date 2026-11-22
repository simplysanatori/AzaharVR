# Azahar VR on Windows: trusted certificates

The static LibreSSL has no certificate store of its own, and Windows keeps no PEM file. On Windows
the program writes one per run (`src/common/windows_ca_bundle.cpp`) from `certs/cacert.pem` beside
the program plus the roots in Windows' own store.

| File | From | SHA-256 |
| --- | --- | --- |
| `cacert.pem` | curl's extract of Mozilla's roots, `https://curl.se/ca/cacert-2026-11-05.pem` (118 certificates) | `427c064216761a1e72a8b30f0d954d08cb6e4435a82091fb865afd20c8497285` |

Licence: Mozilla Public License 2.0 (the certificate data is Mozilla's). To update, take the newest
dated file from `https://curl.se/docs/caextract.html` and change the line above.
