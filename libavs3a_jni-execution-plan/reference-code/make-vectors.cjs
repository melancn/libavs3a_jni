// Synthetic transport-only cases. NEVER pass their random payload to the vendor decoder.
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const outDir=path.resolve(process.argv[2]||path.join(__dirname,'.test-build')); fs.mkdirSync(outDir,{recursive:true});
const source=JSON.parse(fs.readFileSync(path.join(__dirname,'../frozen/evidence/tables.extracted.json'),'utf8'));
const a=source['arm64-v8a'].tables,b=source['armeabi-v7a'].tables;
for(const name of Object.keys(a))assert.deepEqual(a[name].values,b[name].values,`${name} ABI mismatch`);
const table=a.crc16Table.values;
for(let i=0;i<256;i++){let c=i<<8;for(let k=0;k<8;k++)c=((c<<1)^((c&0x8000)?0x1021:0))&65535;assert.equal(table[i],c);}
function crc(p){let c=65535;for(const x of p)c=((c<<8)^table[(c>>>8)&255]^x)&65535;return c;}
function write(p,start,n,v){for(let i=0;i<n;i++){let bit=start+i,mask=1<<(7-bit%8);p[bit>>3]=(p[bit>>3]&~mask)|(((v>>>(n-1-i))&1)?mask:0);}}
const rows=[];let maxFrame=0,minFrame=1e9;
for(let cfg=0;cfg<2;cfg++)for(let sr=0;sr<9;sr++)for(let nn=0;nn<2;nn++)for(let br=0;br<16;br++){
 const bitrate=(cfg===0?a.bitrateTableMono:a.bitrateTableStereo).values[br];if(!bitrate)continue;
 const rate=a.avs3SamplingRateTable.values[sr],payloadBits=Math.trunc(Math.fround(Math.fround(bitrate/rate)*1024))-56;
 const payloadBytes=Math.ceil(payloadBits/8),seed=cfg*31+sr*7+br*13+nn*19;
 const payload=Buffer.from(Array.from({length:payloadBytes},(_,i)=>(i*73+seed)&255));
 const h=Buffer.alloc(7);write(h,0,12,4095);write(h,12,4,2);write(h,17,3,nn);write(h,20,3,0);write(h,23,4,sr);
 const c=crc(payload);write(h,27,8,c>>>8);write(h,35,7,cfg);write(h,42,2,1);write(h,44,4,br);write(h,48,8,c&255);
 const fields=[rate,bitrate,cfg+1,nn,cfg,1024,16,7,payloadBits,payloadBytes,7+payloadBytes,c];
 rows.push([h.toString('hex'),seed,...fields].join('\t')); maxFrame=Math.max(maxFrame,7+payloadBytes);minFrame=Math.min(minFrame,7+payloadBytes);
}
fs.writeFileSync(path.join(outDir,'synthetic-header-cases.tsv'),rows.join('\n')+'\n');
const r={cases:rows.length,maxFrameBytes:maxFrame,minFrameBytes:minFrame,crc123456789:crc(Buffer.from('123456789')).toString(16),allTablesEqual:true,crcTableVerifiedAgainstPolynomial:true,realAudioFixtures:false};
fs.writeFileSync(path.join(outDir,'table-and-vector-check.json'),JSON.stringify(r,null,2)+'\n');console.log(r);
