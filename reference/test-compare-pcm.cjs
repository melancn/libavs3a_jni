// Synthetic PCM tests for the comparator, NOT vendor/fixture validation.
'use strict';
const assert=require('node:assert/strict'),crypto=require('node:crypto');
const {compare,checkedFile}=require('./compare-pcm.cjs');
const pcm=xs=>{const b=Buffer.alloc(xs.length*2);xs.forEach((v,i)=>b.writeInt16LE(v,i*2));return b;};
const ref=pcm([-32768,32767,0,-1,1234,-4321]);
const actual=pcm([19,20,-32768,32767,0,-1,1234,-4321,21,22]);
const s={channels:2,sampleRateHz:48000,decodedSamplesPerChannel:5,referenceSamplesPerChannel:3,primingSamples:1,trailingSamples:1,maxAbsErrorLsb:0,rmsErrorLsb:0,referenceSha256:crypto.createHash('sha256').update(ref).digest('hex')};
let checks=0;
const ok=(f)=>{f();checks++;};
ok(()=>assert.equal(compare(actual,ref,s).status,'PASS'));
ok(()=>assert.deepEqual(compare(actual,ref,s).metrics.map(m=>m.maxAbsErrorLsb),[0,0]));
const different=Buffer.from(actual);different.writeInt16LE(1,8);
ok(()=>assert.equal(compare(different,ref,s).status,'FAIL'));
ok(()=>assert.equal(compare(different,ref,{...s,maxAbsErrorLsb:1,rmsErrorLsb:1}).status,'PASS'));
ok(()=>assert.equal(compare(different,ref,{...s,maxAbsErrorLsb:1,rmsErrorLsb:0}).status,'FAIL'));
const swapped=Buffer.from(actual);for(let i=0;i<swapped.length;i+=4){const x=swapped.readInt16LE(i);swapped.writeInt16LE(swapped.readInt16LE(i+2),i);swapped.writeInt16LE(x,i+2);}
ok(()=>assert.equal(compare(swapped,ref,s).status,'FAIL'));
for(const k of ['channels','sampleRateHz','decodedSamplesPerChannel','referenceSamplesPerChannel','primingSamples','trailingSamples','maxAbsErrorLsb','rmsErrorLsb'])for(const value of [null,-1,NaN,Infinity,'1'])ok(()=>assert.throws(()=>compare(actual,ref,{...s,[k]:value})));
for(const patch of [{referenceSha256:'0'.repeat(64)},{primingSamples:0},{trailingSamples:3},{channels:3},{decodedSamplesPerChannel:4},{referenceSamplesPerChannel:0},{maxAbsErrorLsb:0.5}])ok(()=>assert.throws(()=>compare(actual,ref,{...s,...patch})));
ok(()=>assert.throws(()=>compare(actual.subarray(1),ref,s)));
ok(()=>assert.throws(()=>compare(actual,ref.subarray(2),s)));
for(const p of ['../outside.pcm','/outside.pcm','C:\\outside.pcm','a/../../outside.pcm','a//bad.pcm'])ok(()=>assert.throws(()=>checkedFile(__dirname,p)));
const mono=pcm([-32768,0,32767]);
ok(()=>assert.equal(compare(mono,mono,{...s,channels:1,decodedSamplesPerChannel:3,primingSamples:0,trailingSamples:0,referenceSha256:crypto.createHash('sha256').update(mono).digest('hex')}).status,'PASS'));
console.log(`PASS PCM_COMPARATOR_UNIT checks=${checks}; synthetic PCM only; vendor NOT executed`);