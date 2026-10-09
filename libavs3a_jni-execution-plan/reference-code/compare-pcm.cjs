// Numeric PCM comparison only. Does NOT attest decoder execution, provenance or licenses.
'use strict';
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto'),assert=require('node:assert/strict');
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
function compare(actual,reference,s){
 assert(Buffer.isBuffer(actual)&&Buffer.isBuffer(reference),'expected byte buffers');
 for(const k of ['channels','sampleRateHz','decodedSamplesPerChannel','referenceSamplesPerChannel','primingSamples','trailingSamples'])assert(Number.isSafeInteger(s[k])&&s[k]>=0,'invalid '+k);
 assert(s.channels===1||s.channels===2,'mono/stereo required');assert(s.sampleRateHz>0,'invalid rate');
 assert(s.decodedSamplesPerChannel>0&&s.referenceSamplesPerChannel>0,'empty PCM not an acceptance input');
 for(const k of ['maxAbsErrorLsb','rmsErrorLsb'])assert(typeof s[k]==='number'&&Number.isFinite(s[k])&&s[k]>=0&&s[k]<=65535,'invalid '+k);
 assert(Number.isInteger(s.maxAbsErrorLsb),'maxAbsErrorLsb must be integer');
 assert.match(s.referenceSha256||'',/^[0-9a-f]{64}$/,'missing reference SHA');assert.equal(sha(reference),s.referenceSha256,'reference SHA mismatch');
 assert.equal(actual.length,s.decodedSamplesPerChannel*s.channels*2,'decoded sample count mismatch');
 assert.equal(reference.length,s.referenceSamplesPerChannel*s.channels*2,'reference sample count mismatch');
 assert(s.primingSamples<=s.decodedSamplesPerChannel&&s.trailingSamples<=s.decodedSamplesPerChannel-s.primingSamples,'invalid trimming');
 assert.equal(s.decodedSamplesPerChannel-s.primingSamples-s.trailingSamples,s.referenceSamplesPerChannel,'trim/count mismatch');
 const result=[];let pass=true;
 for(let ch=0;ch<s.channels;ch++){
  let max=0,sum=0,changed=0;
  for(let i=0;i<s.referenceSamplesPerChannel;i++){
   const a=actual.readInt16LE(((i+s.primingSamples)*s.channels+ch)*2),r=reference.readInt16LE((i*s.channels+ch)*2),delta=a-r;
   max=Math.max(max,Math.abs(delta));sum+=delta*delta;if(delta)changed++;
  }
  const rms=Math.sqrt(sum/s.referenceSamplesPerChannel),ok=max<=s.maxAbsErrorLsb&&rms<=s.rmsErrorLsb;pass=pass&&ok;
  result.push({channel:ch,maxAbsErrorLsb:max,rmsErrorLsb:rms,differingSamples:changed,pass:ok});
 }
 return {kind:'PCM_COMPARISON_ONLY',status:pass?'PASS':'FAIL',actualSha256:sha(actual),referenceSha256:sha(reference),sampleRateHz:s.sampleRateHz,channels:s.channels,decodedSamplesPerChannel:s.decodedSamplesPerChannel,referenceSamplesPerChannel:s.referenceSamplesPerChannel,primingSamples:s.primingSamples,trailingSamples:s.trailingSamples,thresholds:{maxAbsErrorLsb:s.maxAbsErrorLsb,rmsErrorLsb:s.rmsErrorLsb},metrics:result,vendorExecutedByThisTool:false,provenanceVerifiedByThisTool:false};
}
function checkedFile(root,relative){
 assert(typeof relative==='string'&&relative.length>0&&!path.isAbsolute(relative)&&!relative.includes(':'),'relative path required');
 assert(relative.split(/[\\/]/).every(s=>s!=='.'&&s!=='..'&&s!==''),'invalid relative path');
 const real=fs.realpathSync(path.resolve(root,relative)),rel=path.relative(root,real);
 assert(rel!==''&&rel!=='..'&&!rel.startsWith('..'+path.sep)&&!path.isAbsolute(rel),'path escapes spec directory');
 assert(fs.statSync(real).isFile(),'not a regular file');return real;
}
function run(specPath){
 assert(specPath,'usage: node compare-pcm.cjs <spec.json>');
 const real=fs.realpathSync(specPath),root=path.dirname(real),bytes=fs.readFileSync(real),s=JSON.parse(bytes.toString('utf8').replace(/^\uFEFF/,''));
 const out=compare(fs.readFileSync(checkedFile(root,s.actualPcm)),fs.readFileSync(checkedFile(root,s.referencePcm)),s);
 out.specSha256=sha(bytes);return out;
}
if(require.main===module){try{const result=run(process.argv[2]);console.log(JSON.stringify(result,null,2));if(result.status!=='PASS')process.exitCode=1;}catch(e){console.error('INVALID_PCM_COMPARISON: '+e.message);process.exitCode=2;}}
module.exports={compare,checkedFile,run};