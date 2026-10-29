// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#include "projects/yamnet/helium/conv3x3_check.h"
#include <cstdio>
int main() {
    if (!yamnet::check_conv3x3()) return 1;
    std::puts("YAMNet convolution layout/stride/tail checks PASS (864 configurations)");
}
