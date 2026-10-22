# Security policy

mxsd processes untrusted XML on resource-constrained targets and compiles
potentially untrusted schemas on the host. Reports involving memory safety,
resource-limit bypasses, validation bypasses, crashes, or unintended file or
network access are treated as security issues.

## Supported versions

Until the project publishes its first stable release, security fixes are made
on the `main` branch and released in the newest version. Older pre-1.0 releases
are not maintained unless explicitly stated in their release notes.

## Reporting a vulnerability

Do not open a public issue for a suspected vulnerability. Use GitHub's
[private vulnerability reporting](https://github.com/Sixtaupi/mxsd/security/advisories/new)
to send the report to the maintainers.

Include, when possible:

- the affected version or commit;
- the schema, XML input, or generated code needed to reproduce the issue;
- the build configuration and target platform;
- the observed impact and expected behavior;
- a minimal reproducer, stack trace, or sanitizer report;
- whether the report may be shared with downstream projects.

The maintainers will acknowledge the report, investigate it privately, and
coordinate disclosure and a fix according to its impact. Please avoid sharing
the issue publicly until a release or agreed disclosure date is available.

## Scope

Security-relevant behavior includes:

- memory corruption or undefined behavior in the C runtime;
- validation acceptance outside the documented support contract;
- bypasses of configured input, depth, occurrence, text, regex, schema, or
  identity-constraint limits;
- unexpected target-side heap allocation in a zero-heap configuration;
- implicit filesystem or network access by `mxsdc`;
- generated C containing attacker-controlled code rather than data;
- denial of service that escapes documented and configured bounds.

Unsupported XSD features that are rejected explicitly, documented resource
limits, and failures caused solely by an undersized application-provided arena
are not vulnerabilities by themselves.
