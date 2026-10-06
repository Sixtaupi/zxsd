# SPDX-License-Identifier: Apache-2.0
# Minimal makefile for Sphinx documentation
#

# You can set these variables from the command line, and also
# from the environment for the first two.
SPHINXOPTS    ?=
SPHINXBUILD   ?= sphinx-build
DOXYGEN       ?= doxygen
SOURCEDIR     = .
BUILDDIR      = _build_sphinx

doxygen:
	@$(DOXYGEN) Doxyfile

# Put it first so that "make" without argument is like "make help".
help: doxygen
	@$(SPHINXBUILD) -M help "$(SOURCEDIR)" "$(BUILDDIR)" $(SPHINXOPTS) $(O)

.PHONY: help doxygen Makefile

# Catch-all target: route all unknown targets to Sphinx using the new
# "make mode" option.  $(O) is meant as a shortcut for $(SPHINXOPTS).
%: Makefile doxygen
	@$(SPHINXBUILD) -M $@ "$(SOURCEDIR)" "$(BUILDDIR)" $(SPHINXOPTS) $(O)
