// SPDX-License-Identifier: GPL-3.0-only
const fs=require('fs'),vm=require('vm'),assert=require('assert/strict'),crypto=require('crypto');
const ctx={require,Buffer,URLSearchParams,nativeCrypto:crypto,console};vm.createContext(ctx);
vm.runInContext(fs.readFileSync('romfs/hongguo.cjs','utf8')+'\n'+fs.readFileSync('romfs/phone_source.js','utf8')+'\nthis.phone=PhoneSource;',ctx);
const p=ctx.phone,key=Buffer.from('00112233445566778899aabbccddeeff','hex');
function spade(){const decoded=Buffer.concat([Buffer.from('1'+key.toString('hex')),Buffer.from([0])]),raw=Buffer.alloc(decoded.length+2);let even=250,odd=85;for(let i=0;i<decoded.length;i++){const v=((decoded[i]+21+(i.toString(2).split('1').length-1))&255)^(i%2?odd:even);raw[i+1]=v;if(i%2)odd=v;else even=v;}raw[0]=49^raw[1]^raw[2];raw[raw.length-1]=120^raw[raw.length-3]^raw[raw.length-2];return raw.toString('base64');}
function envelope(data){const material=Buffer.from(Array.from({length:32},(_,i)=>i)),mask=[104,64,70,166,190,168,143,130,225,254,251,217,196,34,45,60,29,20,103,105],encoded=Buffer.alloc(32);for(let i=0;i<32;i++){const slot=i%20,x=material[i]^(i?encoded[i-1]:109)^mask[slot]^((90+13*slot)&255)^85;encoded[i]=(((x>>>3)|(x<<5))-215+11*i)&255;}const cipher=crypto.createCipheriv('aes-128-cbc',material.subarray(0,16),material.subarray(16));return 'v2.abcd'+encoded.toString('hex')+'.'+Buffer.concat([cipher.update(JSON.stringify(data)),cipher.final()]).toString('base64');}
(async()=>{
 const shim={console,__hash:(algo,hex)=>crypto.createHash(algo).update(Buffer.from(hex,'hex')).digest('hex'),__aes:(algo,keyHex,ivHex,input)=>{const d=crypto.createDecipheriv(algo,Buffer.from(keyHex,'hex'),Buffer.from(ivHex,'hex'));d.setAutoPadding(false);return Buffer.concat([d.update(Buffer.from(input,'hex')),d.final()]).toString('hex');}};
 vm.createContext(shim);for(const file of ['deps.js','prelude.js','hongguo.cjs','phone_source.js'])vm.runInContext(fs.readFileSync('romfs/'+file,'utf8'),shim);vm.runInContext('this.phone=PhoneSource;',shim);
 assert.equal(shim.phone.contentKey(spade()).toString('hex'),key.toString('hex'));assert.equal(shim.phone.decode(envelope({parse:0})).parse,0);
 assert.equal(shim.phone.sign('a=1','{}',1792263994000)['X-Gorgon'],p.sign('a=1','{}',1792263994000)['X-Gorgon']);
 assert.equal(p.contentKey(spade()).toString('hex'),key.toString('hex'));assert.throws(()=>p.contentKey('bad'));assert.throws(()=>p.decode('v2.bad.eA=='));
 assert.equal(p.decode(envelope({parse:0,key_urls:[]})).parse,0);assert.equal(p.decode('{"parse":false}').parse,false);
 const html='_ROUTER_DATA='+JSON.stringify({loaderData:{player_page:{vid:'123',series_id:'456',video_player_info:{main_url:'https://example.test/public'}}}});
 assert.equal(p.publicAddresses(html,'456','123')[0],'https://example.test/public');assert.equal(p.publicAddresses(html,'456','999').length,0);
 const variant={video_meta:{codec_type:'h264',vheight:1280,vwidth:720},main_url:Buffer.from('https://example.test/video').toString('base64'),encrypt_info:{encrypt:true,spade_a:spade()}};
 const rows=p.rowsFromModel({video_list:[variant,{...variant,video_meta:{codec_type:'bytevc2'}}]});assert.equal(rows.rows.length,1);assert.equal(rows.rows[0].quality,'720');assert.equal(rows.rows[0].item.phone_key.toString('hex'),key.toString('hex'));
 let request,calls=0;ctx.axios=Object.assign(async()=>({status:200,data:envelope({parse:0,jx:'0',key_urls:[{name:'720P',src:'https://example.test/media',kid:'0'.repeat(32),spade_a:spade()}]})}),{post:async(url,body,options)=>{calls++;request={url,body:JSON.parse(body),options};return {status:200,data:JSON.stringify({code:0,data:{video_model:JSON.stringify({video_list:[variant]})}})};}});
 assert.equal((await p.videoInfo('123')).rows.length,1);assert.equal(request.body.content_type,1);assert.equal(request.body.biz_param.video_platform,3);assert(request.url.includes('version_code=73532'));assert(request.url.includes('resolution=1280%2A2772'));assert.equal(request.options.headers['X-Gorgon'],undefined);
 await p.videoInfo('123');assert.equal(calls,1);p.clear('123');await p.videoInfo('123');assert.equal(calls,2);
 assert.equal((await p.backup('456','123')).rows[0].item.phone_key.toString('hex'),key.toString('hex'));
 await assert.rejects(p.backup(undefined,'123'),/剧集 ID/);
 ctx.axios=async()=>({status:200,data:JSON.stringify({parse:1,key_urls:[]})});await assert.rejects(p.backup('456','123'),/直接媒体/);
 const callsByPath=[];ctx.axios={post:async(url,body)=>{const request=JSON.parse(body);callsByPath.push({url,body:request});if(url.includes('/video_model/'))return {status:200,data:JSON.stringify({data:{video_model:{video_list:[variant]}}})};if(url.includes('/video_detail/'))return {status:200,data:'{"code":0,"data":{"video_data":{"series_id":7686713195246930968,"series_title":"Test","episode_cnt":2,"content_type":1004,"video_list":[{"vid":7686715217652223038,"vid_index":1},{"vid":7686715221565508670,"vid_index":2,"content_type":2}]}}}'};return {status:200,data:JSON.stringify({code:0,data:{video_data:[{series_id_str:'123',series_title:'Test',episode_cnt:2}],has_more:true,next_offset:request.offset+18,session_id:'session'}})};}};
 const complete=await p.detail('7686713195246930968');assert.equal(complete.episodes[0].id,'7686715217652223038');assert.equal(complete.episodes[1].number,2);
 await p.videoInfo(complete.episodes[0].id);assert.equal(callsByPath.at(-1).body.content_type,1004);await p.videoInfo(complete.episodes[1].id);assert.equal(callsByPath.at(-1).body.content_type,2);
 await p.catalog(1,'comic');await p.catalog(2,'comic');assert.equal(callsByPath.at(-1).body.offset,18);assert.equal(callsByPath.at(-1).body.session_id,'session');assert.equal(callsByPath.at(-1).body.select_items.genre[0],'comic_series');
 await assert.rejects(p.catalog(9,'ai'),/分页已过期/);
 ctx.axios.post=async()=>({status:200,data:JSON.stringify({data:{video_data:{series_id:'123',video_list:[{vid:'1',vid_index:2}]}}})});await assert.rejects(p.detail('123'),/不完整/);
 ctx.axios=async()=>({status:200,data:'_ROUTER_DATA='+JSON.stringify({loaderData:{'search_(keyword)/page':{isSuccess:true,query:'Test',searchList:[{video_data:{series_id_str:'123',series_title:'Test'}}]}}})});
 assert.equal((await p.search('Test',1)).items[0].id,'123');await assert.rejects(p.search('Other',1),/有效结果/);
 console.log('PASS: phone request version/platform/signature, direct video_list, base64 URLs, codec filtering, spade key, v2 envelope, backup flags and cache invalidation');
})().catch(e=>{console.error(e);process.exitCode=1});
