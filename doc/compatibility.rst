API, ABI, and migration
#######################

This is the compatibility contract for the public C headers, the runtime, and
``mxsdc`` output. It does not extend the XSD support contract or claim complete
W3C conformance. The project remains experimental.

Release policy
**************

* In ``0.MINOR.PATCH``, patch releases preserve existing public C source APIs
  and the three compatibility epochs below. A minor release may introduce
  incompatible changes, documented in ``CHANGELOG.md`` with migration steps.
* During 0.x, rebuild the runtime, generated objects, and application together
  when upgrading. There is no promise of drop-in replacement of a separately
  built library, even when release numbers or epochs match.
* From 1.0, incompatible public API/ABI changes will require a major release.
  This is a release rule, not a claim that 1.x compatibility is already tested.
* Correctness and security fixes may reject previously accepted invalid XML or
  XSD. This is not an API break. Validity remains subject to the documented XSD
  subset and configured resource limits.
* Private headers, internal functions, descriptor addresses, generated C text,
  diagnostic wording, and performance measurements are not stable interfaces.
  Within a build, generation remains deterministic.

The installed CMake package accepts compatible requests from the same minor
line before 1.0, and the same major line afterwards. Its shared-library ABI
identifier follows the same rule (currently ``0.20``, not just ``0``).
An exact package version can always be requested. This selection rule does
not check compiler flags, memory limits, or the separately installed compiler.

Compatibility epochs
********************

``include/mxsd_version.h`` defines independent, monotonically increasing integer
epochs. Their initial value is **1**; they are not release numbers.

.. list-table::
   :header-rows: 1
   :widths: 32 68

   * - Identifier
     - Covered contract; increment when incompatible
   * - ``MXSD_RUNTIME_ABI_VERSION``
     - Public function signatures, calling interface, caller-owned state layouts,
       configuration stamp format, and runtime entry points required by generated code.
   * - ``MXSD_DESCRIPTOR_ABI_VERSION``
     - Schema descriptor layouts, enum values, and their interpretation by the runtime.
   * - ``MXSD_BINDING_ABI_VERSION``
     - Binding descriptors, generic binding state, scalar/storage conventions,
       and the generator/runtime representation contract, including streaming bindings.

A change can require more than one increment. Do not recycle epochs or export
an old epoch guard unless the corresponding compatibility adapter is actually
implemented and tested. A compatible implementation fix does not require an
epoch change. An incompatible epoch change cannot be shipped in a 0.x patch.

``mxsdc --version`` prints the compiler release; ``mxsdc --abi-info`` prints its
release and required epochs as JSON. ``MXSD_VERSION`` identifies the including
headers and ``mxsd_version()`` identifies the linked runtime. Release strings
are diagnostic metadata, not compatibility predicates.

Generated ``.mxsd.[ch]`` files check the runtime and descriptor epochs at
compilation. Generated ``.bind.[ch]`` files additionally check the binding epoch.
Checks are present in **both** headers and sources: replacing just a generated
header cannot make a stale source compatible. Entry points also reference
epoch-specific symbols such as ``mxsd_descriptor_abi_1``. An already compiled
object therefore fails to link against a runtime exporting only another epoch.
With dynamic libraries, symbol resolution can happen at load time or first use.
Do not suppress these errors or edit the checks to force a build through.

These guards are not a schema checksum or a binary loader. Two different XSDs
can have the same epochs but incompatible application structures. Regenerate
each schema's complete output set together and do not mix different revisions
of its headers, sources, or application objects. Never persist the raw C
descriptors as a portable flash, disk, or network format: they contain native
pointers, sizes, and alignment.

.. important::

   Output produced before epoch stamping has no such checks. Regenerate all
   legacy validators and bindings once with the matching runtime/compiler.
   Matching epochs cannot retroactively certify old, unversioned objects.

Memory configuration and binary boundaries
******************************************

Epochs do not encode the architecture or configured capacities. In particular,
``mxsd_stream_t``, ``mxsd_xml_reader_t``, and ``mxsd_bind_stream_t`` contain arrays
sized by public resource macros. All participating translation units must use
the same public limits, ``MXSD_NO_HEAP`` mode, architecture, calling convention,
compiler ABI, packing, and enum/alignment options. Rebuild all consumers after
any change. The selected XML backend is private; switching it still requires
rebuilding the runtime and rerunning the relevant tests.

Zephyr propagates Kconfig-derived definitions globally. With CMake, link
``mxsd::mxsd`` to inherit its usage requirements; application-specific capacity
overrides must also be applied to the runtime and every generated source, not
only the application. Generated ``.limits.h`` checks prove minimum capacities,
not that two translation units have identical layouts.

For a separately built runtime, check the application's stamp once **before**
initializing any context or calling a validation/binding API:

.. code-block:: c

   #include <mxsd_bind.h>

   static const mxsd_bind_runtime_config_t expected = MXSD_BIND_RUNTIME_CONFIG_INIT;

   mxsd_result_t result = mxsd_bind_check_runtime(&expected);
   if (result.code != MXSD_OK) {
       /* Refuse to use this runtime. No XML context has been touched. */
       return 1;
   }

Without bindings, use ``mxsd_runtime_config_t``, ``MXSD_RUNTIME_CONFIG_INIT``,
and ``mxsd_check_runtime()`` instead. A mismatch returns
``MXSD_ERR_INCOMPATIBLE_ABI``. The checks allocate nothing and compare exact
epochs, all public resource limit values, heap mode, and recorded public sizes;
the binding check also compares binding sizes and its epoch.

This is an **explicit startup check**, not an automatic check on every runtime
call. Matching stamps are necessary, not sufficient: sizes cannot prove identical
member offsets, calling conventions, or all toolchain settings. The stamp itself
requires the same basic C ABI and runtime epoch. It is not a safe bridge between
arbitrary architectures or incompatible versions of the check function.

Supported upgrade paths
***********************

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - Change
     - Required action
   * - Same epochs, compatible release
     - Existing stamped generated sources can be recompiled. Regeneration is
       recommended to receive generator fixes; rebuild all objects during 0.x.
   * - Any epoch changes or legacy output
     - Upgrade the compiler and runtime together, regenerate all output, adapt
       application code according to the changelog, then rebuild everything.
   * - XSD, imports/includes, or generator options change
     - Regenerate the schema's complete output set, review changed application
       types and validation behavior, and rebuild all consumers.
   * - Limits, Kconfig, ABI flags, or target change
     - Reconfigure and rebuild the runtime, generated objects, and application.
       Review arenas, stacks, and schema minima; regeneration alone is insufficient.

Prefer a compiler and runtime from the same release, especially for an upgrade
from unversioned output. Different releases with matching epochs can share the
generated format; this does not promise that generator fixes or supported XSD
features are identical. New generated dependencies require a runtime epoch
change if older runtimes with that epoch cannot provide them.

Migration checklist
*******************

#. Record the old runtime/compiler releases, XSD dependency graph, binding roots,
   ``--stream-bind`` paths, and configured limits. Read the target release's
   ``CHANGELOG.md`` before upgrading.
#. Install the chosen compiler and runtime together. Check ``--abi-info`` and
   the runtime headers; do not accidentally use another ``mxsdc`` from ``PATH``.
#. Regenerate all ``.mxsd.[ch]``, ``.bind.[ch]``, and, when requested, ``.conf``
   and ``.limits.h`` files with the original explicit options. Review the diff.
#. Adapt application types and calls. Merge reviewed configuration recommendations
   before Zephyr configuration, keeping application budgets explicit.
#. Use a fresh out-of-tree build with consistent definitions. Run the startup
   configuration check when crossing a separately built library boundary.
#. Run host and no-heap tests, the installed-package consumer, and Zephyr Twister
   tests for relevant targets. Rerun application-specific XML fixtures, resource
   measurements, and selected optional W3C/HIL gates as appropriate.

Compatibility regression tests are ``mxsd_compatibility_test`` (also shared with
Twister) and ``generated-compatibility``. They test matching/mismatched stamps,
header/source rejection, precompiled-object link rejection, release metadata,
and CMake package selection. These establish the new baseline, not compatibility
with historical releases whose output was unversioned.

On Windows, the generated-code test initializes the developer environment using
``vcvarsall.bat`` from the configured compiler's Visual Studio installation and
toolset, with matching host/target architectures. Its direct compiler/linker
probes therefore have the SDK's ``INCLUDE``, ``LIB``, and ``PATH`` even when
CTest is launched from an ordinary PowerShell prompt after a Visual Studio
CMake build. Environment changes remain local to the test's child processes;
compilation/link artifacts stay in its temporary directory. The
``compatibility-helpers`` tests exercise this setup and error handling offline.
