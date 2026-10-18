// SPDX-License-Identifier: GPL-3.0-only
const fs=require('fs'),vm=require('vm'),cp=require('child_process'),crypto=require('crypto');
let seq=0;
async function request(url,opts={}){
 const headerFile=require('path').join(require('os').tmpdir(),'hg-header-'+process.pid+'-'+seq++);
 const args=['-sS','-L','--max-time','25','-D',headerFile,'-w','\n%{http_code}',url];
 for(const [k,v]of Object.entries(opts.headers||{}))args.push('-H',k+': '+v);
 if(opts.body)args.push('--data-binary','@-');
 const r=cp.spawnSync('curl',args,{input:opts.body,maxBuffer:20*1024*1024});
 if(r.status!==0)throw Error('network '+r.status);
 const i=r.stdout.lastIndexOf(10),status=Number(r.stdout.subarray(i+1).toString()),bytes=r.stdout.subarray(0,i),headers={};
 for(const line of fs.readFileSync(headerFile,'utf8').split(/\r?\n/)){const p=line.indexOf(':');if(p>0)headers[line.slice(0,p).toLowerCase()]=line.slice(p+1).trim();}fs.unlinkSync(headerFile);
 if(status<200||status>=300)throw Error('HTTP '+status);
 return {status,headers,data:opts.responseType==='arraybuffer'?bytes:bytes.toString('utf8')};
}
request.post=(u,b,o)=>request(u,{...o,body:b});
const ctx={require,Buffer,URL,URLSearchParams,axios:request,console,nativeCrypto:crypto,SwitchConfig:{source:'phone'}};vm.createContext(ctx);
for(const f of ['hongguo.cjs','phone_source.js','adapter.js'])vm.runInContext(fs.readFileSync('romfs/'+f,'utf8'),ctx);

module.exports={ctx,request};
