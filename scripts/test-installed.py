# SPDX-License-Identifier: Apache-2.0
"""Install exact producer/consumer wheels and test outside the source directory."""
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile
import venv


def run(*args,cwd):
    subprocess.run([str(part) for part in args],cwd=cwd,check=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--geometry-wheel",required=True,type=Path)
    parser.add_argument("--interchange-wheel",required=True,type=Path)
    options = parser.parse_args(); root = Path(__file__).resolve().parents[1]
    with tempfile.TemporaryDirectory(prefix="meshvale-interchange-installed-") as scratch:
        directory = Path(scratch); environment = directory/"env"
        venv.EnvBuilder(with_pip=True).create(environment)
        python = environment/("Scripts/python.exe" if sys.platform == "win32" else "bin/python")
        run(python,"-m","pip","install","--no-index",options.geometry_wheel.resolve(),options.interchange_wheel.resolve(),cwd=directory)
        run(python,"-I","-m","unittest","discover","-s",root/"tests/python","-v",cwd=directory)
        run(python,"-I",root/"examples/python/obj_bundle.py",cwd=directory)
        for order in ["geometry-first","interchange-first"]:
            code = ("import meshvale_geometry; import meshvale_interchange" if order == "geometry-first" else
                    "import meshvale_interchange; import meshvale_geometry")
            triangle = repr("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n")
            code += "; assert meshvale_interchange.Mesh is meshvale_geometry.Mesh; assert meshvale_interchange.read_obj("+triangle+").document.mesh.face_count == 1; print('Installed import order passed: "+order+"')"
            run(python,"-I","-c",code,cwd=directory)
        run(python,"-m","pip","check",cwd=directory)
