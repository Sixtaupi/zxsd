Building this documentation
###########################

The documentation combines Sphinx narrative pages with Doxygen XML rendered
through Breathe. Doxygen and Graphviz are required for the Sphinx HTML build.
Install both tools through the system package manager and ensure that the
``doxygen`` and ``dot`` executables are available on ``PATH``.

.. code-block:: console

   $ python3 -m venv .venv-docs
   $ . .venv-docs/bin/activate
   $ python -m pip install -r doc/requirements.txt
   $ make -C doc html

Open ``doc/_build_sphinx/html/index.html``. Treat warnings as errors with
``make -C doc clean html SPHINXOPTS=\"-W --keep-going\"``.

On Windows:

.. code-block:: powershell

   py -m venv .venv-docs
   .\.venv-docs\Scripts\Activate.ps1
   python -m pip install -r doc\requirements.txt
   doc\make.bat html

The build scripts run Doxygen first and stop if API extraction fails.

