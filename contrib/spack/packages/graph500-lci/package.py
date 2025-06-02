# Copyright Spack Project Developers. See COPYRIGHT file for details.
#
# SPDX-License-Identifier: (Apache-2.0 OR MIT)

from spack.package import *

class Graph500Lci(CMakePackage):

    homepage = "https://github.com/JiakunYan/graph500.git"
    url = "https://github.com/JiakunYan/graph500.git"
    git = "https://github.com/JiakunYan/graph500.git"

    maintainers("JiakunYan")

    version("master", branch="master")

    variant(
        "backend",
        default="lci",
        values=("lci", "mpi"),
        multi=False,
        description="Communication backend",
    )

    depends_on("cmake@3.13:", type="build")
    depends_on("lci", when="backend=lci")
    depends_on("mpi")

    def cmake_args(self):
        args = [
            self.define_from_variant("GRAPH500_AML_BACKEND", "backend"),
        ]

        return args
