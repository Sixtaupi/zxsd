#!/usr/bin/env python3
"""Download and safely extract the pinned W3C XML Schema 1.0 test suite."""

import argparse
import hashlib
import pathlib
import shutil
import tarfile
import tempfile
import urllib.request

URL = "https://www.w3.org/XML/2004/xml-schema-test-suite/xmlschema2006-11-06/xsts-2007-06-20.tar.gz"
SHA256 = "902176b25e4111cf96b08663107521a4992e8ea67aad6b815592a6a5b4b9ea06"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("destination", type=pathlib.Path)
    args = parser.parse_args()
    args.destination.mkdir(parents=True, exist_ok=True)
    archive = args.destination / "xsts-2007-06-20.tar.gz"
    if not archive.exists() or hashlib.sha256(archive.read_bytes()).hexdigest() != SHA256:
        with urllib.request.urlopen(URL) as response, tempfile.NamedTemporaryFile(
            delete=False
        ) as output:
            shutil.copyfileobj(response, output)
            temporary = pathlib.Path(output.name)
        if hashlib.sha256(temporary.read_bytes()).hexdigest() != SHA256:
            temporary.unlink()
            raise SystemExit("W3C archive SHA-256 mismatch")
        temporary.replace(archive)
    root = args.destination.resolve()
    with tarfile.open(archive) as bundle:
        for member in bundle.getmembers():
            target = (root / member.name).resolve()
            if (root != target and root not in target.parents) or member.issym() or member.islnk():
                raise SystemExit("unsafe archive member: " + member.name)
        bundle.extractall(root)
    print(root / "xmlschema2006-11-06")


if __name__ == "__main__":
    main()
