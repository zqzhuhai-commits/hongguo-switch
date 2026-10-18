// SPDX-License-Identifier: GPL-3.0-only
const fs=require('fs'),vm=require('vm'),assert=require('assert/strict');
const source=fs.readFileSync('romfs/hongguo.cjs','utf8');
function context(){const ctx={require,Buffer,URLSearchParams,console};vm.createContext(ctx);vm.runInContext(source+'\nthis.api={fetchVideoInfo,modelForVideo,signedVideoRequest};',ctx);return ctx;}
(async()=>{
 for(const first of ['error','empty','bad-fallback']){
  const ctx=context(),requests=[];
  ctx.axios=Object.assign(async()=>{}, {post:async(url,body)=>{
   requests.push({url,body:JSON.parse(body.toString())});
   if(requests.length===1){if(first==='error')throw Error('network');return {status:200,data:{data:{video_model:JSON.stringify({fallback_api:first==='bad-fallback'?'https://example.test/empty':''})}}};}
   return {status:200,data:{data:{'123':{video_model:JSON.stringify({fallback_api:'https://example.test/video'})}}}};
  }});
  const post=ctx.axios.post;ctx.axios.post=async(...args)=>{const r=await post(...args);return {...r,data:JSON.stringify(r.data)};};
  ctx.response=JSON.stringify({video_info:{data:{video_list:{v480:{main_url:'https://example.test/mp4',height:480}}}}});
  vm.runInContext("getText=async url=>url.endsWith('/empty')?'{}':response",ctx);
  const result=await ctx.api.fetchVideoInfo('123');assert.equal(result.rows.length,1);assert.equal(requests.length,2);
  assert(requests[0].url.includes('/video_model/v1/'));assert.equal(requests[0].body.video_id,'123');
  assert(requests[1].url.includes('/multi_video_model/v1/'));assert.deepEqual(requests[1].body.mixed_video_id_map,{'1004':['123']});
  assert.equal(requests[1].body.biz_param.disable_video_relate_book,undefined);
  await ctx.api.fetchVideoInfo('123');assert.equal(requests.length,2,'cache only valid results');
 }
 const ctx=context();assert.throws(()=>ctx.api.modelForVideo({data:{'999':{video_model:{fallback_api:'https://wrong.test'}}}},'123'));
 // Run the complete adapter with real provider functions, mocked only at network.
 const media=Buffer.alloc(16);media.writeUInt32BE(8);media.write('ftyp',4);media.writeUInt32BE(8,8);media.write('moov',12);
 let modelCalls=0,webCalls=0,wrong=false;
 ctx.axios=async(url,options)=>{const [a,b]=options.headers.Range.slice(6).split('-').map(Number),end=Math.min(b,15);return {status:206,headers:{'content-range':`bytes ${a}-${end}/16`},data:media.subarray(a,end+1)};};
 ctx.axios.post=async()=>{modelCalls++;return {status:200,data:JSON.stringify({code:-1,message:'upstream failed'})}};
 ctx.web=()=>{webCalls++;return `<script type="application/ld+json">${JSON.stringify({'@type':'VideoObject',about:{'@id':`https://hongguoduanju.com/player/0/${wrong?'999':'123'}`},contentUrl:'https://example.test/public.mp4'})}</script>`;};
 vm.runInContext("getText=async()=>web();this.nativeCrypto=require('node:crypto');this.SwitchConfig={source:'app158'};",ctx);
 vm.runInContext(fs.readFileSync('romfs/adapter.js','utf8'),ctx);
 assert.equal((await ctx.SwitchAPI.open('123')).source,'web');assert.equal(modelCalls,2);assert.equal(webCalls,1);
 assert.equal(await ctx.SwitchAPI.read('123',0,15),media.toString('hex'));
 wrong=true;await assert.rejects(ctx.SwitchAPI.open('123'),/片源均不可用/);
 await assert.rejects(ctx.SwitchAPI.read('123',0,15),/Range/);
 console.log('PASS: signed single/batch requests; retry on network, empty model and empty resources; exact ID; valid-only cache; default app-to-public fallback; no stale/wrong episode playback');
})().catch(e=>{console.error(e);process.exitCode=1});
