// Validate the frozen handoff only. Does not scan/load/execute vendor binaries.
'use strict';
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto'),assert=require('node:assert/strict');
function verify(root=path.resolve(__dirname,'..'),transform=(name,data)=>data) {
 const read=n=>transform(n,JSON.parse(fs.readFileSync(path.join(root,n),'utf8').replace(/^\uFEFF/,'')));
 const abi=read('frozen/abi-contract.json'),d=read('frozen/frame-dialect.json'),lock=read('frozen/vendor.lock.json');
 const manifest=read('frozen/evidence-manifest.json'), evidence=new Set();
 for(const e of manifest){
  assert.match(e.file,/^evidence\/[A-Za-z0-9_.-]+$/); assert(!evidence.has(e.file)); evidence.add(e.file);
  assert.match(e.sha256,/^[a-f0-9]{64}$/);
  assert.equal(crypto.createHash('sha256').update(fs.readFileSync(path.join(root,'frozen',e.file))).digest('hex'),e.sha256,e.file);
 }
 for(const name of fs.readdirSync(path.join(root,'frozen/evidence')))assert(evidence.has('evidence/'+name),'unlisted evidence: '+name);
 const refs=x=>{if(!x||typeof x!=='object')return;for(const[k,v]of Object.entries(x)){if(k==='evidence'){assert(Array.isArray(v)&&v.length);for(const p of v)assert(evidence.has(p),'unhashed evidence: '+p);}else refs(v);}};
 assert.equal(abi.schemaVersion,1); assert.equal(d.schemaVersion,1);
 assert.equal(abi.vendorId,lock.vendorId); assert.equal(d.vendorId,lock.vendorId);
 for(const n of ['abi-contract','frame-dialect'])assert.deepEqual(read('frozen/'+n+'.json'),read('templates/'+n+'.json'));
 assert.equal(abi.requiredFieldsResolved,true); assert.equal(d.verified,true);
 assert.deepEqual(Object.keys(abi.abis).sort(),['arm64-v8a','armeabi-v7a']);
 const prefix={firstFrame:[0,2],sampleRate:[4,4],sourceBits:[8,2],totalBitrate:[12,4],bitrateCopy:[16,4],channelConfig:[20,4],channelCount:[24,2],objectCount:[26,2],objectBitrate:[28,4],bedBitrate:[32,4],mixedContentType:[36,2],mixedContent:[38,2],lfeFlag:[40,2],decoderFormat:[42,2],option44:[44,2],hoaOrder:[46,2],frameSamples:[48,2],payloadBits:[52,4],neuralCodecType:[56,4],modelType:[60,4]};
 for(const[name,a]of Object.entries(abi.abis)){
  const w=name==='arm64-v8a'?8:4; assert.equal(a.ready,true); assert.equal(a.pointerBytes,w); assert.equal(a.decoderStateBytes,64+25*w);
  assert.equal(a.payloadCapacityBytes,12300);assert.equal(a.bitstreamStateBytes,12304);assert.equal(a.bitCursorOffset,12300);assert.equal(a.bitCursorStorage,'int32');assert.equal(a.callingConventionVerified,true);
  const occupied=new Uint8Array(a.decoderStateBytes);
  for(const f of [...Object.values(a.fields),...a.opaqueRanges]){
   assert(Number.isInteger(f.offset)&&Number.isInteger(f.size)&&f.offset>=0&&f.size>0&&f.offset+f.size<=occupied.length);
   for(let i=f.offset;i<f.offset+f.size;i++){assert.equal(occupied[i],0,'field overlap');occupied[i]=1;}
  }
  assert(occupied.every(x=>x===1),'layout gap');
  for(const[k,[offset,size]]of Object.entries(prefix)){
   const f=a.fields[k];assert.equal(f.offset,offset,k);assert.equal(f.size,size,k);assert.equal(f.storage,'int'+size*8,k);assert.equal(f.callerWrites,k!=='firstFrame',k);
  }
  const pointers=['baseModel','hyperModel','bitstreamPointer','hoaDecoder','mcDecoder','stereoDecoder','monoAux','corePointers','metadata','modelFile'];
  for(let i=0;i<pointers.length;i++){
   const f=a.fields[pointers[i]],slot=i<8?i:i+15;
   assert.equal(f.offset,64+slot*w);assert.equal(f.size,i===7?16*w:w);assert.equal(f.storage,i===7?'pointerArray':'pointer');assert.equal(f.callerWrites,false);
  }
  for(const f of Object.values(a.fields)){assert.equal(f.verified,true);if(f.storage.startsWith('pointer'))assert.equal(f.owner,'SDK');else assert.notEqual(f.initial,undefined);}
  for(const k of abi.requiredFields)assert.equal(a.fields[k].verified,true);
  if(w===4)for(const f of Object.values(a.entryPoints)){assert.equal(Number(f.symbolValue)&1,1);assert.equal(Number(f.symbolValue)-1,Number(f.codeAddress));}
 }
 refs(abi);refs(d);
 const report=read('frozen/evidence/reference-test-report.json');
 assert.equal(report.status,'PASS');assert.equal(report.vendorExecuted,false);assert.equal(report.deviceValidation,'NOT_RUN');
 for(const [name,hash] of Object.entries(report.inputSha256)){
  assert.match(name,/^[A-Za-z0-9_.-]+$/);
  assert.equal(crypto.createHash('sha256').update(fs.readFileSync(path.join(root,'reference-code',name))).digest('hex'),hash,'reference source changed: '+name);
 }
 const extracted=read('frozen/evidence/tables.extracted.json');
 for(const name of Object.keys(abi.abis)){
  const x=extracted[name];
  const binary=lock.files.find(f=>f.abi===name);
  assert.equal(x.sha256,binary.sha256,'extraction does not match vendor lock');
  assert.deepEqual(d.sampleRateIndexTable,x.tables.avs3SamplingRateTable.values);
  assert.deepEqual(d.bitrateIndexTables.mono,x.tables.bitrateTableMono.values);
  assert.deepEqual(d.bitrateIndexTables.stereo,x.tables.bitrateTableStereo.values);
  assert.deepEqual(d.crcRules.table,x.tables.crc16Table.values);
 }
 assert.equal(d.headerBytes,7);assert.equal(d.maxFrameBytes,5120);assert.equal(d.minFrameBytes,11);assert.equal(d.observedFrameSamplesPerChannel,1024);
 assert.equal(d.headerModes.length,1); const h=d.headerModes[0];
 assert.equal(h.profile,0);assert.deepEqual(h.channelConfigurations,[0,1]);assert.deepEqual(h.neuralTypes,[0,1]);assert.deepEqual(h.sourcePrecisionIndices,[1]);
 assert.deepEqual(h.fields.map(f=>[f.name,f.bitOffset,f.width]),[['sync',0,12],['codecId',12,4],['reservedZero',16,1],['neuralType',17,3],['profile',20,3],['sampleRateIndex',23,4],['crcHigh',27,8],['channelConfig',35,7],['precisionIndex',42,2],['bitrateIndex',44,4],['crcLow',48,8]]);
 for(const [name,value] of Object.entries({sync:4095,codecId:2,reservedZero:0,profile:0}))assert.equal(h.fields.find(f=>f.name===name).required,value);
 assert.equal(d.crcRules.poly,4129);assert.equal(d.crcRules.init,65535);assert.equal(d.crcRules.xorout,0);assert.equal(d.crcRules.refin,false);assert.equal(d.crcRules.refout,false);assert.equal(d.crcRules.appendZeroBytes,false);
 assert.equal(d.sdkSafetyPolicy.maxPayloadBits,32767);assert.equal(d.sdkSafetyPolicy.maxAdmittedFrameBytes,4096);
 assert.equal(d.crcRules.check123456789,'a69d');assert.equal(d.crcRules.empty,'ffff');
 for(let i=0;i<256;i++){let c=i<<8;for(let k=0;k<8;k++)c=((c<<1)^((c&32768)?0x1021:0))&65535;assert.equal(d.crcRules.table[i],c);}
 assert.equal(abi.runtimeValidation,'NOT_RUN');assert.equal(d.runtimeValidation.status,'NOT_RUN');assert.deepEqual(d.runtimeValidation.validatedConfigurations,[]);
 assert.equal(d.runtimeValidation.primingSamples,null);assert.equal(d.runtimeValidation.trailingSamples,null);
 // This snapshot intentionally has no legal/device approval. Later approval needs new reviewed evidence, not toggling this test.
 assert.equal(lock.redistributionApproved,false);
 return {status:'PASS',kind:'STATIC_HANDOFF_ONLY',abis:2,evidenceFiles:evidence.size,vendorExecuted:false,deviceValidation:'NOT_RUN',redistributionApproved:false};
}
if(require.main===module){try{console.log(JSON.stringify(verify(process.argv[2]),null,2));}catch(e){console.error('FAIL_STATIC_HANDOFF: '+e.message);process.exitCode=1;}}
module.exports={verify};