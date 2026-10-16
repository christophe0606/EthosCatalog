# SPDX-License-Identifier: Apache-2.0
def sample_count(value):
    if isinstance(value, bool) or not isinstance(value, int) or value <= 0:
        raise ValueError('Sample counts must be positive integers')
    return value

class ProjectNode:
    folder = "../../../common/nodes_impl/c"
    params = {}
    needsHardware = False

    @property
    def typeName(self):
        return type(self).__name__

    def parameters(self):
        self.addVariableArg(f"params->{self.nodeName}")
