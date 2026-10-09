'use strict';
const assert=require('node:assert/strict');const {verify}=require('./verify-contracts.cjs');
assert.equal(verify().status,'PASS');
const tests=[
 ['abi-contract.json',a=>{a.abis['arm64-v8a'].ready=false;}],
 ['abi-contract.json',a=>{a.abis['armeabi-v7a'].pointerBytes=8;}],
 ['abi-contract.json',a=>{a.abis['arm64-v8a'].fields.sampleRate.offset=0;}],
 ['abi-contract.json',a=>{a.abis['arm64-v8a'].payloadCapacityBytes=12304;}],
 ['abi-contract.json',a=>{a.abis['arm64-v8a'].fields.metadata.callerWrites=true;}],
 ['abi-contract.json',a=>{a.abis['armeabi-v7a'].entryPoints.Avs3Decode.symbolValue='0x57b4';}],
 ['abi-contract.json',a=>{a.abis['arm64-v8a'].fields.sourceBits.evidence=['evidence/missing.txt'];}],
 ['frame-dialect.json',d=>{d.headerModes[0].fields[0].required=4094;}],
 ['frame-dialect.json',d=>{d.crcRules.table[255]=0;}],
 ['frame-dialect.json',d=>{d.bitrateIndexTables.mono[10]=160000;}],
 ['frame-dialect.json',d=>{d.sdkSafetyPolicy.maxPayloadBits=40904;}],
 ['frame-dialect.json',d=>{d.runtimeValidation.status='PASS';}],
 ['evidence-manifest.json',m=>{m[0].sha256='0'.repeat(64);}],
 ['evidence-manifest.json',m=>{m.pop();}],
 ['vendor.lock.json',m=>{m.redistributionApproved=true;}]
];
for(const [name,mutate] of tests){
 // Mutate both canonical and template in memory to test actual invariants, not only mirror inequality.
 assert.throws(()=>verify(undefined,(n,data)=>{if(n.endsWith('/'+name))mutate(data);return data;}),name);
}
console.log(`PASS STATIC_HANDOFF_UNIT baseline=1 rejectedMutations=${tests.length}; vendor NOT executed`);