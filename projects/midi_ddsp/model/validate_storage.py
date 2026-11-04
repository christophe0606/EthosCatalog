# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Verify storage-only regeneration retains compiled delegates and CPU program."""
import argparse
import copy
from dataclasses import asdict
import hashlib
import json
from pathlib import Path


def canonical(path):
    from executorch.exir._serialize._program import deserialize_pte_binary
    from executorch.exir.schema import Tensor
    program = deserialize_pte_binary(path.read_bytes()).program
    result = {}
    for plan in program.execution_plan:
        normalized = copy.deepcopy(plan)
        delegate_hashes = []
        for delegate in normalized.delegates:
            data = program.backend_delegate_data[delegate.processed.index].data
            delegate_hashes.append(hashlib.sha256(bytes(data)).hexdigest())
            delegate.processed.index = 0
        constants = {}
        for index,value in enumerate(normalized.values):
            if isinstance(value.val, Tensor) and value.val.data_buffer_idx:
                data = program.constant_buffer[value.val.data_buffer_idx].storage
                constants[index] = hashlib.sha256(bytes(data)).hexdigest()
                value.val.data_buffer_idx = 0
        result[plan.name] = {'plan': asdict(normalized), 'delegates': delegate_hashes,
                             'constants': constants}
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--before', type=Path, required=True)
    parser.add_argument('--after', type=Path, default=Path(__file__).resolve().parents[1]/'ai_layer/model.pte')
    parser.add_argument('--output', type=Path, default=Path(__file__).with_name('storage_validation.json'))
    parser.add_argument('--exclude-method',action='append',default=[],
                        help='Explicit shape-changing method validated separately; reported as excluded.')
    args = parser.parse_args()
    before,after = canonical(args.before),canonical(args.after)
    cases = []
    names=set(before)|set(after)
    if not set(args.exclude_method)<=names:
        raise ValueError('Excluded method is absent from both programs')
    for name in sorted(names-set(args.exclude_method)):
        a,b = before.get(name),after.get(name)
        cases.append({'method':name,'present_in_both':a is not None and b is not None,
                      'compiled_delegates_identical': bool(a and b and a['delegates']==b['delegates']),
                      'cpu_constants_identical': bool(a and b and a['constants']==b['constants']),
                      'normalized_plan_identical': bool(a and b and a['plan']==b['plan'])})
    passed = bool(cases) and all(all(value for key,value in case.items() if key!='method') for case in cases)
    report = {'passed':passed,'cases':cases,
              'excluded_methods':args.exclude_method,
              'before_sha256':hashlib.sha256(args.before.read_bytes()).hexdigest(),
              'after_sha256':hashlib.sha256(args.after.read_bytes()).hexdigest(),
              'scope':'Exact compiled delegate bytes, CPU constant bytes and execution-plan equality after resolving storage indices. Does not replace staged board correctness/timing.'}
    args.output.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2),flush=True)
    if not passed: raise SystemExit(1)


if __name__=='__main__': main()
