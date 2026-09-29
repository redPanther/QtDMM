# Security Policy

## Supported Versions

Security fixes go into `master` and into the latest stable release.
Older releases and release candidates do not get security updates.

| Version               | Supported          |
| --------------------- | ------------------ |
| master                | :white_check_mark: |
| latest stable release | :white_check_mark: |
| older releases        | :x:                |

## Reporting a Vulnerability

**Low severity** (for example a crash on a malformed file, or a problem that
needs local access and does no real harm): open a normal
[issue](https://github.com/qtdmm/QtDMM/issues).

**Everything else:** please do not open a public issue. Report it privately
through GitHub instead:
[Report a vulnerability](https://github.com/qtdmm/QtDMM/security/advisories/new)
(also under the repository's **Security** tab). Include what is affected
(version, platform, connection type), how to reproduce it, and what an
attacker could do with it.

Private reports are fixed as soon as possible. The fix goes into `master`
and the latest stable release, and we tell you in the report when it's
released. If we don't treat a report as a security problem, we tell you
why, and it continues as a normal issue.
