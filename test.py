#!/usr/bin/env python3

import os
import sys


def main(i):
    cmd = "ninja -Cbuild"
    print(cmd)
    if os.system(cmd) != 0:
        return

    if os.path.exists("output.txt"):
        os.remove("output.txt")

    cmd = f"./build/driver_c {i} > output.txt"
    print(cmd)

    if os.system(cmd) != 0:
        os.system("bat output.txt -l log")
        return

    cmd = f"delta output.txt {"expected.txt" if i ==
                             "0" else f"./tests/expected_{i}.txt"}"
    print(cmd)
    os.system(cmd)


main("0" if len(sys.argv) == 0 else sys.argv[1])
