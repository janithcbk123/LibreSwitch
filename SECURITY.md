# Security Policy

## Important: this firmware is not production-hardened

LibreSwitch controls **mains-voltage relays** and includes networking, but
several security-critical pieces are deliberately marked as incomplete (see
`REQUIREMENTS_COVERAGE.md`):

- **OTA signature verification is a development integrity check (SHA-256), not
  a production signature.** It detects corruption, not forgery. Do not rely on
  it to authenticate firmware publishers.
- **Device identity / enrollment** (mTLS cert provisioning) requires a backend
  that is not part of this repo. The included enrollment is a development stub.
- The platform's default OTA encryption key must be overridden before any real
  deployment.

Treat this as a hobbyist/educational project unless and until those items are
addressed for your deployment.

## Reporting a vulnerability

If you find a security issue, please **do not open a public issue.** Instead,
use GitHub's private "Report a vulnerability" feature (Security tab) or contact
the maintainer directly. Give it a reasonable window to be addressed before
public disclosure.

Because of the project's status above, the most valuable reports are ones that
identify a flaw a user might *not* expect given the documented limitations.
