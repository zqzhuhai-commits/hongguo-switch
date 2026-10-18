// Test request compatibility without fetching media or modifying encryption logic.
const fs=require('fs'),vm=require('vm'),assert=require('assert/strict');
const source=fs.readFileSync('romfs/hongguo.cjs','utf8');
const functions=source.slice(source.indexOf('async function requestVideoModel'),source.indexOf('function parseFallback'));
let request;
const ctx={VIDEO_API:"https://example.test/novel/player/video_model/v1/",JSON,Object,str:String,jsonBody:x=>typeof x==='string'?JSON.parse(x):x,signedVideoRequest:payload=>({url:'test',body:payload,headers:{}}),axios:{post:async(url,body)=>{request=JSON.parse(body);return {status:200,data:{data:{video_model:'{"fallback_api":"https://example.test/video"}'}}};}}};
vm.createContext(ctx);vm.runInContext(functions,ctx);
(async()=>{const response=await ctx.requestVideoModel('123456789');assert.equal(request.video_id,'123456789');assert.equal(request.mixed_video_id_map,undefined);assert.equal(request.biz_param.disable_video_relate_book,false);assert.equal(request.biz_param.need_all_video_definition,true);assert.equal(ctx.modelForVideo(response,'123456789').fallback_api,'https://example.test/video');assert(source.includes('/novel/player/video_model/v1/'));assert(!source.includes('/novel/player/multi_video_model/v1/'));assert.throws(()=>ctx.modelForVideo({data:{}},'1'));console.log('PASS: build158 single-video endpoint, payload and direct response model');})().catch(e=>{console.error(e);process.exitCode=1;});
