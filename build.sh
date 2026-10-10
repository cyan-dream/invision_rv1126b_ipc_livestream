#!/bin/bash
rm build/* -r
cd build && cmake ..
make -j$(nproc)
cd ..
