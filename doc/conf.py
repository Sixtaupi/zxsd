# SPDX-License-Identifier: Apache-2.0
"""Sphinx configuration for the mxsd documentation."""

import os
import re
from pathlib import Path

DOC_DIR = Path(__file__).resolve().parent
REPOSITORY_ROOT = DOC_DIR.parent

project = "mxsd"
author = "mxsd contributors"
copyright = "2026, mxsd contributors"

cmake_project = (REPOSITORY_ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
version_match = re.search(r"project\(mxsd VERSION ([0-9.]+)", cmake_project)
if version_match is None:
    raise RuntimeError("Unable to determine the mxsd version from CMakeLists.txt")
release = os.environ.get("DOC_VERSION", version_match.group(1))
version = release

extensions = ["breathe", "myst_parser", "sphinx_design"]
breathe_projects = {"mxsd": str(DOC_DIR / "_build_doxygen" / "xml")}
breathe_default_project = "mxsd"
breathe_default_members = ("members", "undoc-members")
breathe_domain_by_extension = {"h": "c"}

source_suffix = {".rst": "restructuredtext", ".md": "markdown"}
primary_domain = "c"
highlight_language = "c"
exclude_patterns = [
    "_build_sphinx",
    "_build_doxygen",
    "_doxygen",
    "README",
    "Thumbs.db",
    ".DS_Store",
]

html_theme = "sphinx_rtd_theme"
html_title = f"mxsd {release}"
html_static_path = ["_static"]
html_css_files = ["custom.css"]
html_theme_options = {
    "collapse_navigation": False,
    "navigation_depth": 4,
    "sticky_navigation": True,
    "titles_only": False,
}
html_show_sourcelink = False
