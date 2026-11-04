# Copyright 2026 Arm Limited and/or its affiliates.
# SPDX-License-Identifier: Apache-2.0
"""Independent TensorFlow execution of the original checkpoint math.

Uses TensorFlow Keras GRUCell/Dense kernels rather than PyTorch conversion.
The source audit is reference/{expression_generator,synth_params_decoder,
cond_rnn,ddsp_nn}.py. No original training package dependencies are imported.
"""
import numpy as np
import tensorflow as tf
from pathlib import Path


class Reference:
    def __init__(self,checkpoint_dir):
        self.e=tf.train.load_checkpoint(str(Path(checkpoint_dir)/'expression_generator/5000'))
        self.s=tf.train.load_checkpoint(str(Path(checkpoint_dir)/'synthesis_generator/50000'))
        self.cells={}

    @staticmethod
    def w(reader,key):return tf.constant(reader.get_tensor(key+'/.ATTRIBUTES/VARIABLE_VALUE'))

    def dense(self,r,k,x):return tf.linalg.matmul(x,self.w(r,k+'/kernel'))+self.w(r,k+'/bias')

    def norm(self,r,k,x,keras=False):
        axes=[-1] if keras else list(range(1,len(x.shape)))
        mean,var=tf.nn.moments(x,axes,keepdims=True)
        scale=tf.reshape(self.w(r,k+('/gamma' if keras else '/scale')),[-1])
        shift=tf.reshape(self.w(r,k+('/beta' if keras else '/shift')),[-1])
        return (x-mean)*tf.math.rsqrt(var+(0.001 if keras else 1e-5))*scale+shift

    def fc(self,r,k,x,count):
        for i in range(count):
            base=k+f'/stack/layer_with_weights-{i}'
            x=self.dense(r,base+'/layer_with_weights-0',x)
            x=self.norm(r,base+'/layer_with_weights-1',x,True)
            x=tf.nn.leaky_relu(x,alpha=.2)
        return self.dense(r,k+'/dense_out',x)

    def cell(self,r,k,x,h):
        key=(id(r),k)
        if key not in self.cells:
            kernel=self.w(r,k+'/cell/kernel');recurrent=self.w(r,k+'/cell/recurrent_kernel');bias=self.w(r,k+'/cell/bias')
            layer=tf.keras.layers.GRUCell(recurrent.shape[0],reset_after=True)
            layer.build((None,kernel.shape[0]));layer.set_weights([kernel.numpy(),recurrent.numpy(),bias.numpy()]);self.cells[key]=layer
        return self.cells[key](x,[h],training=False)[0]

    def bi(self,r,k,x):
        h=self.w(r,k+'/forward_layer/cell/recurrent_kernel').shape[0]
        a=tf.zeros([x.shape[0],h]);b=tf.zeros_like(a);aa=[];bb=[]
        for i in range(x.shape[1]):
            a=self.cell(r,k+'/forward_layer',x[:,i],a);b=self.cell(r,k+'/backward_layer',x[:,-1-i],b)
            aa.append(a);bb.append(b)
        return tf.concat([tf.stack(aa,1),tf.stack(bb[::-1],1)],-1)

    def expression(self,pitch,duration,instrument):
        r=self.e
        a=tf.gather(self.w(r,'pitch_emb/embeddings'),pitch)
        b=self.dense(r,'duration_emb',duration)
        c=tf.repeat(tf.gather(self.w(r,'instrument_emb/embeddings'),instrument)[:,None],pitch.shape[1],axis=1)
        z=self.bi(r,'birnn',tf.concat([a,b,c],-1))
        h1=tf.zeros([z.shape[0],128]);h2=tf.zeros_like(h1);prev=tf.zeros([z.shape[0],6]);out=[]
        for i in range(z.shape[1]):
            h1=self.cell(r,'rnn1',tf.concat([z[:,i],prev],-1),h1);h2=self.cell(r,'rnn2',h1,h2)
            prev=self.fc(r,'dense_out/dense_out',self.norm(r,'norm',h2[:,None]),2)[:,0]*tf.cast(pitch[:,i:i+1]!=0,tf.float32)
            out.append(prev)
        return tf.stack(out,1)

    def harmonic(self,x):
        r=self.s;k='midi_decoder/decoder/midi_f0_to_harmonic'
        def conv(key,x,d):
            return tf.nn.conv2d(x[:,:,None,:],self.w(r,key+'/kernel'),strides=[1,1,1,1],padding='SAME',dilations=[1,d,1,1])[:,:,0,:]+self.w(r,key+'/bias')
        x=conv(k+'/net/conv_in',x,1)
        for i in range(20):x=x+self.norm(r,k+f'/net/norms/{i}',conv(k+f'/net/layers/{i}/layer_with_weights-0',tf.nn.relu(x),2**(i%5)))
        return self.dense(r,k+'/dense_out',self.norm(r,k+'/norm',x))

    def __call__(self,notes,duration,instrument,mapping,frame,uniform):
        expression=self.expression(notes,duration,instrument)
        expanded=tf.gather(tf.clip_by_value(expression,0.,1.),mapping[0],axis=1)
        features=tf.concat([expanded,frame],-1)
        r=self.s;k='midi_decoder/decoder/midi_to_f0'
        z=self.fc(r,'midi_decoder/z_preconditioning_stack',features,5)
        inst=tf.repeat(tf.gather(self.w(r,'midi_decoder/instrument_emb/embeddings'),instrument)[:,None],z.shape[1],axis=1)
        z=tf.concat([z,inst],-1);context=self.bi(r,k+'/birnn',z)
        h1=tf.zeros([z.shape[0],256]);h2=tf.zeros_like(h1);previous=tf.zeros([z.shape[0],201]);pitches=[];logits_all=[]
        for i in range(z.shape[1]):
            h1=self.cell(r,k+'/rnn1',tf.concat([context[:,i],previous],-1),h1);h2=self.cell(r,k+'/rnn2',h1,h2)
            logits=self.dense(r,k+'/dense_out',self.norm(r,k+'/norm',h2[:,None]))[:,0]
            sorted_logits=tf.sort(logits,direction='DESCENDING',axis=-1)
            probs=tf.nn.softmax(sorted_logits)
            exclusive=tf.cumsum(probs,axis=-1,exclusive=True)
            threshold=tf.reduce_min(tf.where(exclusive<.95,sorted_logits,1000.),axis=-1,keepdims=True)
            probs=tf.nn.softmax(tf.where(logits<threshold,-1e10,logits))
            index=tf.minimum(tf.reduce_sum(tf.cast(tf.cumsum(probs,axis=-1)<uniform[:,i],tf.int32),-1),200)
            previous=tf.one_hot(index,201)
            pitch=features[:,i,6:7]*127.+tf.cast(index[:,None],tf.float32)*.01-1.
            pitches.append(pitch);logits_all.append(logits)
        pitches=tf.stack(pitches,1)
        pitch_z=self.dense(r,'midi_decoder/decoder/q_pitch_emb',pitches/127.)
        raw=self.harmonic(tf.concat([z,pitch_z],-1))
        return raw,pitches,expression,tf.stack(logits_all,1)


def validate(checkpoint_dir,model,output_dir,lengths=(4,17,129)):
    import torch,json
    torch.set_num_threads(1)
    ref=Reference(checkpoint_dir);rng=np.random.default_rng(8711);results=[]
    for inst in range(13):
        for length in lengths:
            notes=np.array([[60,0,64,67]],np.int64)
            duration=np.array([[[.1],[.04],[.2],[.08]]],np.float32)
            mapping=np.minimum(np.arange(length)*4//length,3)[None].astype(np.int64)
            pitch=notes[:,mapping[0],None]/127.
            frame=np.concatenate([pitch,np.zeros((1,length,2)),np.linspace(.1,1,length)[None,:,None]],-1).astype(np.float32)
            frame[0,0,1]=1;frame[0,-1,2]=1
            frame[0,notes[0,mapping[0]]==0,3]=0
            inputs=(notes,duration,np.array([inst],np.int64),mapping,frame,rng.uniform(.01,.99,(1,length,1)).astype(np.float32))
            expected=ref(*[tf.convert_to_tensor(x) for x in inputs])
            with torch.no_grad():actual=model(*[torch.from_numpy(x) for x in inputs])
            errors=[float(np.max(np.abs(a.numpy()-b.numpy()))) for a,b in zip(actual,expected)]
            results.append({'instrument_id':inst,'frames':length,'max_abs_errors':dict(zip(['raw_synthesis','sampled_pitch','expression','pitch_logits'],errors))})
            print('TF parity',inst,length,errors,flush=True)
            if max(errors)>0.003:raise RuntimeError('Float parity failed')
    report={'reference':'TensorFlow Keras GRUCell and TF ops using original checkpoint; original architecture source audited; no upstream package installed',
            'sampling':'Same uniform quantiles through original-order masked top-p CDF; tests avoid categorical RNG backend differences',
            'cases':results,'passed':True}
    (Path(output_dir)/'float_validation.json').write_text(json.dumps(report,indent=2)+'\n')


if __name__=='__main__':
    import argparse
    from model import load_model
    parser=argparse.ArgumentParser();parser.add_argument('--checkpoint-dir',type=Path,required=True);args=parser.parse_args()
    validate(args.checkpoint_dir,load_model(),Path(__file__).resolve().parent)
