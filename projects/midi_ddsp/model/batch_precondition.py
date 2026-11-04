# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Re-export independent precondition rows with unchanged saved PTQ parameters.

Candidates stay outside the active model/config until host parity, compiled
memory and coordinated firmware changes have been reviewed. No recalibration.
"""
import argparse
import copy
import hashlib
import json
import os
import shutil
import sys
import tempfile
from pathlib import Path
import numpy as np
import torch
import torchao.quantization.pt2e.quantize_pt2e  # Register saved quantized ops.
import executorch.exir.passes.dim_order_ops_registry

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
from quantized_model import save_quantized_program,load_quantized_program,compile_spec
from model import load_model
from deployment import score
from score_fixtures import demo_score
import yaml


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def quantization_parameters(graph):
    def value(arg):
        return ('node',arg.name) if isinstance(arg,torch.fx.Node) else arg
    return [(str(node.target),[value(arg) for arg in node.args[1:]]) for node in graph.nodes
            if 'quantized_decomposed.' in str(node.target)]


class RowLinear(torch.fx.Interpreter):
    """Evaluate each linear row with the original host GEMV summation order."""
    def call_function(self,target,args,kwargs):
        if target==torch.ops.aten.linear.default:
            return torch.cat([target(row[None],*args[1:],**kwargs) for row in args[0]])
        return super().call_function(target,args,kwargs)


def compile_candidate(destination,batch):
    from project_config import selected_project,DEFAULT_SOLUTION
    from create_ai_layer import export_model
    from inspect_program import inspect
    validation=json.loads((destination/'batch_validation.json').read_text())
    if not validation['passed_row_independence_gate'] or validation['batch']!=batch:
        raise ValueError('Candidate has not passed row independence')
    if (validation['candidate_graph_sha256']!=sha(destination/'precondition_quantized.pt2') or
            validation.get('candidate_safetensors_sha256')!=sha(destination/'precondition_quantized.safetensors')):
        raise ValueError('Candidate graph/weights changed or lack hash validation; rerun validation')
    _,solution,project,_,network=selected_project(DEFAULT_SOLUTION,'midi_ddsp')
    settings={**network,'graph':os.path.relpath(destination/'precondition_quantized.pt2',project.parent),
              'safetensors':os.path.relpath(destination/'precondition_quantized.safetensors',project.parent)}
    spec=compile_spec(solution,project.parent,network)
    program=load_quantized_program(spec.target,project.parent,settings)
    scratch=ROOT/'tmp/midi-ddsp-batch'
    scratch.mkdir(parents=True,exist_ok=True)
    tempfile.tempdir=str(scratch)
    pte,delegates=export_model({'precondition':program},spec,extract_delegate_segments=True)
    path=destination/'model.pte';path.write_bytes(pte)
    info=inspect(path)
    method=info['methods']['precondition']
    method['calls_per_1000frame_block']=(1000+batch-1)//batch
    (destination/'compiler_memory.json').write_text(json.dumps(info,indent=2)+'\n')
    stage_bytes=sum(d['bytes'] for d in method['delegates'])
    report={'batch':batch,'delegates':delegates,'pte_sha256':sha(path),'pte_bytes':len(pte),
            'staged_delegate_bytes':stage_bytes,'planned_bytes':method['planned_buffer_sum'],
            'scratch_max_bytes':method['scratch_max_one_delegate'],
            'fits_existing_stage_buffer':stage_bytes<=1536000,
            'planned_allocator':'regular393216byte method pool; metadata/constant cache additional',
            'planned_bytes_fit_method_pool':method['planned_buffer_sum']<=393216,
            'fits_existing_temp_arena':method['scratch_max_one_delegate']<=1310720,
            'compile_script_sha256':sha(Path(__file__).resolve()),
            'scope':'Isolated candidate using shared ExecuTorch/Vela exporter. Active network, PTE, assets and board unchanged. Actual metadata allocations, compiled output parity and timing still require board validation.'}
    (destination/'compilation.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2),flush=True)


def activate_candidate(destination,project=HERE.parent):
    """Activate validated caches; normal generation/fixtures follow separately."""
    report=json.loads((destination/'batch_validation.json').read_text())
    graph=destination/'precondition_quantized.pt2'
    if (not report['passed_row_independence_gate'] or sha(graph)!=report['candidate_graph_sha256'] or
            sha(destination/'precondition_quantized.safetensors')!=report.get('candidate_safetensors_sha256')):
        raise ValueError('Cannot activate an unvalidated or changed candidate')
    model_dir=project/'model'
    for extension in ('pt2','safetensors'):
        shutil.copyfile(destination/f'precondition_quantized.{extension}',
                        model_dir/f'precondition_quantized.{extension}')
    shutil.copyfile(destination/'batch_validation.json',model_dir/'precondition_batch_validation.json')
    path=model_dir/'network.yml';network=yaml.safe_load(path.read_text())
    network['network']['quantization']['error']['precondition_batching']={
        'batch':report['batch'],'row_independence_bit_exact':True,
        'state_and_quantization_parameters_identical':True,
        'native_host_gemm_tight_gate_passed':report['passed_native_gemm_tight_gate'],
        'native_host_gemm_max_absolute_error':max(c['max_absolute_error'] for c in report['cases']),
        'report':'model/precondition_batch_validation.json',
        'status':'Candidate: compiled board parity, memory and timing required; no gate relaxation.'}
    path.write_text(yaml.safe_dump(network,sort_keys=False))
    print('Activated validated precondition cache; regenerate fixtures and AI layer before programming.',flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--batch',type=int,default=16)
    parser.add_argument('--output-dir',type=Path)
    parser.add_argument('--compile-only',action='store_true',help='Compile an already validated isolated candidate.')
    parser.add_argument('--activate',action='store_true',help='Copy validated candidate caches into this project; does not build or program.')
    args = parser.parse_args()
    if args.batch < 2 or args.batch > 64:
        parser.error('batch must be2..64')
    destination = args.output_dir or ROOT/f'.flow/midi_ddsp/precondition_batch{args.batch}'
    destination.mkdir(parents=True,exist_ok=True)
    torch.set_num_threads(1)
    if args.compile_only:
        compile_candidate(destination,args.batch)
        if args.activate: activate_candidate(destination)
        return
    original = torch.export.load(HERE/'precondition_quantized.pt2')
    exported_module=original.module(check_guards=False)
    single = torch.fx.GraphModule(exported_module,copy.deepcopy(exported_module.graph))
    fixture = np.load(HERE/'benchmark_fixtures.npz')
    x = torch.from_numpy(fixture['precondition_input']).reshape(-1,10)[:1]
    # A plain GraphModule removes the exported module's fixed input guard;
    # re-export then computes fresh shape metadata for the exact same operators.
    graph = torch.fx.GraphModule(single,copy.deepcopy(single.graph))
    candidate = torch.export.export(graph,(x.repeat(args.batch,1),))
    batched = candidate.module(check_guards=False)
    stable = RowLinear(batched)
    if quantization_parameters(original.graph) != quantization_parameters(candidate.graph):
        raise ValueError('Re-export changed quantization parameters')
    if set(original.state_dict) != set(candidate.state_dict):
        raise ValueError('Re-export changed parameter names')
    for name in original.state_dict:
        if not torch.equal(original.state_dict[name],candidate.state_dict[name]):
            raise ValueError(f'Re-export changed parameter {name}')
    cases = []

    def check(name,inputs):
        expected = torch.cat([single(row[None]) for row in inputs])
        pieces = []; stable_pieces = []
        for start in range(0,len(inputs),args.batch):
            rows = inputs[start:start+args.batch]
            padded = torch.zeros(args.batch,10)
            padded[:len(rows)] = rows
            pieces.append(batched(padded)[:len(rows)])
            stable_output=stable.run(padded)
            if isinstance(stable_output,tuple): stable_output=stable_output[0]
            stable_pieces.append(stable_output[:len(rows)])
        actual = torch.cat(pieces)
        error = (actual-expected).abs()
        stable_actual=torch.cat(stable_pieces)
        # Require exact parity with identical linear summation order. Separately
        # retain the initial native-GEMM gate and report its failures honestly;
        # only on-target comparison can accept the new compiled batch shape.
        torch.testing.assert_close(stable_actual,expected,atol=0,rtol=0)
        tight=bool(torch.all(error<=2e-5+2e-5*expected.abs()))
        cases.append({'case':name,'rows':len(inputs),'max_absolute_error':float(error.max()),
                      'rmse':float(error.square().mean().sqrt()),'bit_identical':torch.equal(actual,expected),
                      'native_gemm_tight_gate_passed':tight,'rowwise_linear_bit_identical':True})

    with torch.no_grad():
        model = load_model()
        for instrument in range(13):
            for suite,make in (('four_regions',score),('demo_phrase',demo_score)):
                notes,durations,inst,mapping,features,_ = make(instrument,1000,9100+instrument)
                expression = model.expression(notes,durations,inst).clamp(0.,1.)
                inputs = torch.cat((expression[:,mapping[0]],features),-1)[0]
                # Include transitions, rests and heterogeneous rows. Each full
                # shape is tested separately below, avoiding full-model reruns.
                indices = torch.linspace(0,999,47).long()
                check(f'{suite}_instrument_{instrument}',inputs[indices])
            print('Precondition batch parity instrument',instrument,flush=True)
        generator = torch.Generator().manual_seed(9100)
        for rows in (1,args.batch-1,args.batch,args.batch+1,2*args.batch+3,1000):
            check(f'random_tail_{rows}',torch.rand(rows,10,generator=generator))
        check('held_fixture',x)
    network = yaml.safe_load((HERE/'network.yml').read_text())['network']
    settings = dict(network)
    # Shared saver receives project-relative paths, including a candidate under
    # .flow. All references remain inside this repository.
    settings['graph'] = os.path.relpath(destination/'precondition_quantized.pt2',HERE.parent)
    settings['safetensors'] = os.path.relpath(destination/'precondition_quantized.safetensors',HERE.parent)
    save_quantized_program(candidate,'ethos-u85-256',HERE.parent,settings)
    report = {'passed_row_independence_gate':True,
              'passed_native_gemm_tight_gate':all(c['native_gemm_tight_gate_passed'] for c in cases),
              'native_gemm_gate':{'atol':2e-5,'rtol':2e-5},
              'batch':args.batch,'input_shape':[args.batch,10],
              'output_shape':[args.batch,256],'quantization_parameters_identical':True,
              'state_tensors_identical':True,'cases':cases,
              'source_graph_sha256':sha(HERE/'precondition_quantized.pt2'),
              'candidate_graph_sha256':sha(destination/'precondition_quantized.pt2'),
              'candidate_safetensors_sha256':sha(destination/'precondition_quantized.safetensors'),
              'script_sha256':sha(Path(__file__).resolve()),
              'scope':'Independent rows only; unchanged weights/scales/zero-points and last-axis normalization. Host parity does not prove compiled delegate accuracy, memory or timing. Zero padding only at final partial batch; discard padded output rows.'}
    (destination/'batch_validation.json').write_text(json.dumps(report,indent=2)+'\n')
    print('PASS row independence batch',args.batch,'cases',len(cases),'native tight gate',report['passed_native_gemm_tight_gate'],
          'native maxerror',max(c['max_absolute_error'] for c in cases),flush=True)
    if args.activate: activate_candidate(destination)


if __name__ == '__main__': main()
