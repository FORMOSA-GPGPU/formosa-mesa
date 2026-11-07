# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
# SPDX-License-Identifier: MIT

import os
from pathlib import Path
import re
import subprocess
import sys
from tempfile import TemporaryDirectory

with TemporaryDirectory() as directory:
    root = Path(directory)
    shader = root / "ballot.comp"
    shader.write_text(
        """#version 450
#extension GL_KHR_shader_subgroup_ballot : require
layout(local_size_x=64) in;
layout(set=0,binding=0,std430) buffer Output { uint counts[]; };
void main() {
    uvec4 mask = subgroupBallot((gl_SubgroupInvocationID & 1u) == 0u);
    counts[gl_GlobalInvocationID.x] = subgroupBallotBitCount(mask);
}
"""
    )
    subprocess.run(
        [
            sys.argv[2],
            "-V",
            "--target-env",
            "vulkan1.1",
            str(shader),
            "-o",
            str(root / "ballot.spv"),
        ],
        check=True,
        stdout=subprocess.DEVNULL,
    )
    subprocess.run(
        [sys.argv[1], str(root / "ballot.spv"), str(root / "ballot.elf")],
        env=dict(os.environ, FSA_COMPUTE_DUMP_LLVM=str(root / "ballot.ll")),
        check=True,
        stdout=subprocess.DEVNULL,
    )
    ir = (root / "ballot.ll").read_text()
    counts = {
        bits: len(re.findall(r"\bcall\b[^\n]*@llvm\.ctpop\.i" + str(bits) + r"\b", ir))
        for bits in (32, 64)
    }
    print("Ballot population-count calls:", counts)
    assert counts == {
        32: 0,
        64: 1,
    }, "ballot count was lowered before target-width conversion"
print("ballot width: PASS")
