#!/bin/bash -xe

for i in `seq 30`; do python tests/test.py; done
python tests/perf-key-heavy.py
python tests/perf-bit-heavy.py
for i in `seq 30`; do python tests/test.py; done
