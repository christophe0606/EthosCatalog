# SPDX-License-Identifier: Apache-2.0
class ProjectNode:
    folder = "../../../common/stream/nodes"
    params = {}
    needsHardware = False

    @property
    def typeName(self):
        return type(self).__name__

    def parameters(self):
        self.addVariableArg(f"params->{self.nodeName}")
