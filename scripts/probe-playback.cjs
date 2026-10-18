// SPDX-License-Identifier: GPL-3.0-only
const fs=require('fs'),cp=require('child_process');
const {ctx}=require('./probe-runtime.cjs');
const http=require('http');
(async()=>{let server;try{
 const detail=await ctx.SwitchAPI.detail(process.argv[3]||'7686713195246930968'),id=detail.episodes[Number(process.argv[2]||1)].id;
 const metadata=await ctx.SwitchAPI.open(id,480,true);let delivered=0;
 server=http.createServer(async(req,res)=>{try{
   const match=String(req.headers.range||'bytes=0-').match(/^bytes=(\d+)-(\d*)$/);if(!match){res.writeHead(416);res.end();return;}
   const start=Number(match[1]),end=Math.min(match[2]?Number(match[2]):metadata.total-1,metadata.total-1);
   res.writeHead(206,{'Content-Type':'video/mp4','Accept-Ranges':'bytes','Content-Length':end-start+1,'Content-Range':`bytes ${start}-${end}/${metadata.total}`});
   for(let at=start;at<=end&&!res.destroyed;at+=1048576){const hex=await ctx.SwitchAPI.read(id,at,Math.min(at+1048575,end));const bytes=Buffer.from(hex,'hex');delivered+=bytes.length;if(!res.write(bytes))await new Promise(resolve=>{res.once('drain',resolve);res.once('close',resolve);});}res.end();
 }catch(e){res.destroy(e);}});
 await new Promise(r=>server.listen(0,'127.0.0.1',r));const port=server.address().port;
 const result=await new Promise((resolve,reject)=>{const child=cp.spawn('ffmpeg',['-v','error','-xerror','-i',`http://127.0.0.1:${port}/video.mp4`,'-t','15','-map','0:v:0','-map','0:a:0','-progress','pipe:1','-f','null','-']);let stdout='',stderr='';child.stdout.on('data',b=>stdout+=b);child.stderr.on('data',b=>stderr+=b);child.on('error',reject);child.on('close',code=>resolve({code,stdout,stderr}));});
 const frames=Number(Array.from(result.stdout.matchAll(/^frame=(\d+)/gm)).at(-1)?.[1]||0);
 const decodedSeconds=Number(Array.from(result.stdout.matchAll(/^out_time_us=(\d+)/gm)).at(-1)?.[1]||0)/1000000;const report={series:detail.id,episodeNumber:Number(process.argv[2]||1)+1,episode:id,source:metadata.source,total:metadata.total,delivered,frames,decodedSeconds,audioAndVideo:true,decodePassed:result.code===0&&frames>0&&decodedSeconds>=14,decoderExit:result.code,hardwareTested:false,createdAt:new Date().toISOString(),commit:cp.execFileSync('git',['rev-parse','HEAD'],{encoding:'utf8'}).trim()};fs.mkdirSync('build',{recursive:true});fs.writeFileSync(process.env.HONGGUO_PROBE_OUTPUT||'build/playback-probe.json',JSON.stringify(report,null,2)+'\n');console.log(JSON.stringify(report));
 if(result.code!==0){console.log(result.stderr.replace(/https?:\/\/[^ ]+/g,'[URL]').slice(0,600));process.exitCode=1;}
}catch(e){console.log(String(e.message).replace(/https?:\/\/[^ ]+/g,'[URL]'));process.exitCode=1;}finally{server?.closeAllConnections();server?.close();}})();
