# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Report serialized ExecuTorch planned buffers and actual Vela scratch sizes."""
import argparse
import json
import struct
from pathlib import Path


def blocks(blob):
    offset = 0
    result = {}
    while offset + 32 <= len(blob):
        name = blob[offset:offset + 16].split(b'\0')[0].decode('ascii')
        size = struct.unpack_from('<I', blob, offset + 16)[0]
        result[name] = blob[offset + 32:offset + 32 + size]
        offset += 32 + ((size + 15) // 16) * 16
    return result


def inspect(path):
    from executorch.exir._serialize._program import _get_extended_header
    from executorch.exir._serialize._flatbuffer_program import _flatbuffer_to_program
    from executorch.exir.schema import DataLocation
    serialized = path.read_bytes()
    # The convenience deserializer restores external delegates to inline data;
    # inspect the original flatbuffer to retain their real on-disk locations.
    header = _get_extended_header(serialized)
    if header and not header.is_valid():
        raise ValueError('Invalid PTE extended header')
    program_size = header.program_size if header else len(serialized)
    program = _flatbuffer_to_program(serialized[:program_size])
    report = {'pte_bytes': len(serialized), 'program_bytes': program_size,
              'segment_base_offset': header.segment_base_offset if header else 0,
              'methods': {}}
    report['segments'] = []
    for index, segment in enumerate(program.segments):
        if not header or not header.segment_base_offset:
            raise ValueError('PTE segments without segment header')
        offset = header.segment_base_offset + segment.offset
        if offset < program_size or segment.size < 0 or offset+segment.size > len(serialized):
            raise ValueError('PTE segment exceeds serialized bounds')
        report['segments'].append({'index': index, 'file_offset': offset, 'bytes': segment.size})
    constants = program.constant_segment
    report['constant_segment'] = ({**report['segments'][constants.segment_index],
                                   'tensor_offsets': constants.offsets}
                                  if constants.offsets else None)
    for plan in program.execution_plan:
        batch = (int(plan.values[plan.inputs[0]].val.sizes[0])
                 if plan.name=='precondition' else 1)
        delegates = []
        for delegate in plan.delegates:
            index = delegate.processed.index
            if delegate.processed.location == DataLocation.INLINE:
                data = program.backend_delegate_data[index].data
                storage = {'location': 'inline', 'segment_index': None, 'file_offset': None}
            elif delegate.processed.location == DataLocation.SEGMENT:
                if not header or not header.segment_base_offset:
                    raise ValueError('Delegate segment without segment header')
                segment = program.segments[index]
                offset = header.segment_base_offset + segment.offset
                if offset < program_size or segment.size < 0 or offset + segment.size > len(serialized):
                    raise ValueError('Delegate segment exceeds serialized PTE bounds')
                data = serialized[offset:offset+segment.size]
                storage = {'location': 'segment', 'segment_index': index, 'file_offset': offset}
            else:
                raise ValueError(f'Unsupported delegate location {delegate.processed.location}')
            values = blocks(data)
            delegates.append({'backend': delegate.id, 'bytes': len(data), **storage,
                              'scratch_bytes': struct.unpack('<I', values['scratch_size'])[0],
                              'weight_bytes': len(values['weight_data']),
                              'command_bytes': len(values['cmd_data'])})
        report['methods'][plan.name] = {
            'non_const_buffer_sizes': plan.non_const_buffer_sizes,
            'planned_buffer_sum': sum(plan.non_const_buffer_sizes[1:]),
            'delegates': delegates,
            'scratch_sum_if_all_delegates_loaded': sum(d['scratch_bytes'] for d in delegates),
            'scratch_max_one_delegate': max((d['scratch_bytes'] for d in delegates), default=0),
            'delegate_weight_bytes_total': sum(d['weight_bytes'] for d in delegates),
            'delegate_command_bytes_total': sum(d['command_bytes'] for d in delegates),
            'calls_per_1000frame_block': 'N score notes' if plan.name.startswith('expression_') else (1 if plan.name == 'harmonic' else ((1000+batch-1)//batch if plan.name=='precondition' else 1000)),
            'input_batch':batch,
            'operators': [op.name for op in plan.operators]}
    report['limits'] = ('Compiler/serialized allocation evidence only; does not include Method metadata, '
                        'backend initialization allocations, score/context buffers, DSP or firmware. '
                        'Do not substitute maximum scratch for sum unless runtime reuse is verified.')
    report['runtime_scratch_reuse_audit'] = (
        'Pinned ExecuTorch1.4.1 EthosUBackend.cpp execute allocates scratch from '
        'BackendExecutionContext.get_temp_allocator(), not persistent init memory. '
        'runtime/executor/method.cpp resets the temporary allocator after delegate '
        'execution. For serial method execution this justifies maximum delegate '
        'scratch rather than sum; other temporary kernel allocations and metadata '
        'still need their own bound. Imported runtime files were only read.')
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pte', type=Path, default=Path(__file__).resolve().parents[1]/'ai_layer/model.pte')
    parser.add_argument('--output', type=Path, default=Path(__file__).with_name('compiler_memory.json'))
    args = parser.parse_args()
    report = inspect(args.pte)
    args.output.write_text(json.dumps(report, indent=2)+'\n')
    for name, info in report['methods'].items():
        print(name, 'planned', info['planned_buffer_sum'], 'delegate scratch sum', info['scratch_sum_if_all_delegates_loaded'])
