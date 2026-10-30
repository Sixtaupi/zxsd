Contributing to zxsd

Thank you for your interest in contributing to zxsd!

Contributions are welcome, including bug fixes, new features, documentation improvements, and test coverage.

Getting Started

1. Fork the repository and create a branch from main.
2. Make your changes, following the existing coding conventions.
3. Add or update tests when applicable.
4. Ensure all relevant tests pass.
5. Submit a Pull Request describing your changes.

For significant changes, please open an issue first to discuss the proposed approach.

Coding Guidelines

* Follow the C99 standard.
* Keep the code portable and suitable for embedded systems.
* Avoid unnecessary dependencies and dynamic memory allocations.
* Follow MISRA C:2023 guidelines where practical. (target release 1.0.0)
* Maintain compatibility with supported platforms.
* Use clang-format and EditorConfig for C language.
* Use black for python language.

Testing Policy

All significant changes must include appropriate tests.

* New features must include tests covering expected behavior and edge cases.
* Bug fixes should include regression tests whenever practical.
* Changes to XML Schema validation should include both valid and invalid XML test cases.
* Security fixes should include regression tests whenever possible.
* All existing tests must pass before merging.

If tests are not applicable, explain why in the Pull Request.

Code Quality

Contributions should pass the project’s automated checks, including:

* Build and unit tests.
* Static analysis (Cppcheck, CodeQL). (target release 0.30.0)
* MISRA C checks where applicable. (target release 1.0.0)

Security

Please do not report security vulnerabilities through public issues.

Follow the responsible disclosure process described in SECURITY.md.

License

By contributing, you agree that your contributions will be distributed under the project’s Apache-2.0 license.