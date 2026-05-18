# Security Policy

## Scope

This policy covers the `cryptnox-sdk-esp32` ESP-IDF component bundle and the
bundled `cryptnox-sdk-cpp` C++ core SDK.  It does not cover the Cryptnox smart
card firmware, the Cryptnox mobile app, or any third-party dependencies
(ESP-IDF, mbedTLS, uECC).

## Threat model

| Actor | Access | Trust |
|-------|--------|-------|
| Host application | Full memory access to ESP32 | Trusted |
| NFC eavesdropper | Passive capture of the RF field | Untrusted |
| Malicious card | Active NFC field (crafted APDUs) | Untrusted |
| Physical attacker | USB cable, JTAG (if not burned) | Untrusted |
| Supply-chain attacker | Tampered firmware image | Untrusted |

The SDK defends against the NFC eavesdropper and malicious card via the
Cryptnox secure channel (ECDH key agreement + AES-CBC-MAC).  Physical and
supply-chain attackers are mitigated by enabling Flash Encryption and Secure
Boot V2 (see the Security section of README.md).

## Supported versions

| Version | Supported |
|---------|-----------|
| `main` branch (latest) | Yes |
| Older release branches | No |

Security fixes are applied to `main` only.  Pin your dependency to the latest
tagged release and update promptly when a new one is published.

## Reporting a vulnerability

**Do not open a public GitHub issue for security vulnerabilities.**

Send a report by e-mail to **security@cryptnox.com** with:

1. A description of the vulnerability and the affected component.
2. Steps to reproduce or a proof-of-concept (if available).
3. Your assessment of the impact and severity.

You will receive an acknowledgement within 5 business days.  We aim to release
a fix within 30 days of confirmation for Critical/High findings and within
90 days for Medium/Low findings.

We follow a coordinated-disclosure model: we ask that you refrain from
publishing details until a patch is released, or until 90 days have elapsed
since the initial report, whichever comes first.

## PGP key

A PGP public key for encrypted submissions is available at:
`https://verify.cryptnox.tech/security/pgp-key.asc`

## Acknowledgements

We thank security researchers who responsibly disclose vulnerabilities.
With your consent, your name or alias will be listed in the release notes of
the fix.
