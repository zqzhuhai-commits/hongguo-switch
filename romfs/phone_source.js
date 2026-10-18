// SPDX-License-Identifier: GPL-3.0-only
// Protocol compatibility reference: qq294716498/HongguoTV-Phone-TV-NoAds,
// native/core/provider_hongguo_{app,sign,native_media,playback}.go (app-v0.1.17-42).
const PhoneSource = (() => {
  const ua='com.phoenix.read/73532 (Linux; U; Android 16; zh_CN; 25053RT47C; Build/BP2A.250605.031.A3; Cronet/TTNetVersion:04657795 2026-01-23 QuicVersion:c67e9834 2025-09-08)';
  const identity=()=>String(1000000000000000000n+BigInt(Math.floor(Math.random()*Number.MAX_SAFE_INTEGER))*1000n%8000000000000000000n);
  const deviceId=identity(),installId=identity();
  const valid=id=>typeof id==='string'&&/^\d{1,30}$/.test(id);
  const httpUrl=value=>typeof value==='string'&&value.length<=8192&&/^https?:\/\//.test(value);
  function sign(query,body,now){
    const hash=value=>nativeCrypto.createHash('md5').update(value).digest();
    const payload=Buffer.alloc(20);hash(query).copy(payload,0,0,4);hash(body).copy(payload,4,0,4);
    Buffer.from([0,6,11,28]).copy(payload,12);payload.writeUInt32BE(Math.floor(now/1000),16);
    const key=Buffer.from('44b9b9d9a4aef9fca493aa757ca3c2c4a496938f','hex');
    for(let i=0;i<20;i++)payload[i]^=key[i];
    for(let i=0;i<20;i++){const x=(((payload[i]<<4)|(payload[i]>>4))&255)^payload[(i+1)%20];let reverse=0;for(let bit=0;bit<8;bit++)reverse|=((x>>bit)&1)<<(7-bit);payload[i]=reverse^255^20;}
    return {'X-Khronos':String(Math.floor(now/1000)),'X-Gorgon':'8404401c0000'+payload.toString('hex'),'X-SS-Req-Ticket':String(now),'X-SS-STUB':hash(body).toString('hex').toUpperCase()};
  }
  function contentKey(value){
    if(typeof value!=='string'||value.length>1024)throw Error('媒体密钥编码无效');
    const raw=Buffer.from(value,'base64');if(raw.length<3)throw Error('媒体密钥编码无效');
    const tagLength=(raw[0]^raw[1]^raw[2])-48,n=raw.length-tagLength-1;
    if(tagLength<1||n<33||n>=raw.length)throw Error('媒体密钥结构无效');
    const seed=raw[raw.length-tagLength-2]^raw[raw.length-tagLength-1];
    const tag=Buffer.from(raw.subarray(raw.length-tagLength).map(x=>x^seed)).toString();
    if(tag==='app_v2'||tag==='web_v2')throw Error('媒体密钥版本暂不支持');
    const out=Buffer.alloc(n);let even=250,odd=85;
    for(let i=0;i<n;i++){const c=raw[i+1],previous=i%2?odd:even;if(i%2)odd=c;else even=c;const pop=i.toString(2).split('1').length-1;out[i]=((previous^c)-21-pop)&255;}
    if(n-parseInt(out.toString('ascii',0,1),36)-1!==32||!/[0-9a-fA-F]{32}/.test(out.toString('ascii',1,33)))throw Error('媒体密钥内容无效');
    return Buffer.from(out.toString('ascii',1,33),'hex');
  }
  function decode(body){
    const text=String(body).trim();if(!text.startsWith('v2.'))return JSON.parse(text);
    const parts=text.split('.');if(parts.length!==3||parts[1].length<=4||parts[1].length>1028||!/^[0-9a-fA-F]+$/.test(parts[1].slice(4))||parts[1].length%2)throw Error('备用接口响应密钥无效');
    const encoded=Buffer.from(parts[1].slice(4),'hex');if(encoded.length<32)throw Error('备用接口响应密钥无效');
    const mask=[104,64,70,166,190,168,143,130,225,254,251,217,196,34,45,60,29,20,103,105],material=Buffer.alloc(encoded.length);
    for(let i=0;i<encoded.length;i++){const slot=i%20,x=(encoded[i]+215-11*i)&255;material[i]=(i?encoded[i-1]:109)^mask[slot]^((90+13*slot)&255)^85^(((x<<3)|(x>>5))&255);}
    const ciphertext=Buffer.from(parts[2],'base64');if(!ciphertext.length||ciphertext.length%16)throw Error('备用接口加密响应无效');
    const decipher=nativeCrypto.createDecipheriv('aes-128-cbc',material.subarray(0,16),material.subarray(16,32));
    return JSON.parse(Buffer.concat([decipher.update(ciphertext),decipher.final()]).toString());
  }
  function addresses(row){
    const result=[];const add=value=>{if(Array.isArray(value)){value.forEach(add);return;}if(typeof value!=='string')return;let url=value.trim();if(!httpUrl(url))url=Buffer.from(url,'base64').toString().trim();if(httpUrl(url)&&!result.includes(url))result.push(url);};
    for(const k of ['main_url','backup_url','backup_url_1','backup_url_2','backup_urls','url_list'])add(row[k]);return result;
  }
  function rowsFromModel(model){
    const rows=[];
    for(const [index,row] of Object.entries(model?.video_list||{})){
      if(!row||typeof row!=='object')continue;const meta=row.video_meta||{},codec=String(meta.codec_type||'');
      if(/bytevc2|bvc2/i.test(codec+' '+String(row.gear_des_key||'')))continue;
      const urls=addresses(row);if(!urls.length)continue;
      const encryption=row.encrypt_info||{};let key=null;
      try{if(encryption.spade_a||encryption.encrypt===true||encryption.encryption_method==='cenc-aes-ctr')key=contentKey(encryption.spade_a);}catch(_){continue;}
      const dims=[Number(meta.vheight),Number(meta.vwidth)].filter(n=>n>0);
      const quality=qualityNumber(meta.definition)|| (dims.length?Math.min(...dims):0);
      rows.push({key:'phone-'+index,quality:String(quality),item:{main_url:urls[0],backup_url:urls[1],codec_type:codec,phone_key:key,phone:true}});
    }
    if(!rows.length)throw Error('新版 App 未返回兼容片源');
    return {info:{},rows:rows.sort((a,b)=>Number(b.quality)-Number(a.quality))};
  }
  function publicAddresses(html,series,id){
    const route=extractAssignedJson(html),loaders=route?.loaderData||{};
    const page=Object.entries(loaders).find(([name])=>name==='player_page'||name.startsWith('player_'))?.[1];
    if(String(page?.vid)!==id||String(page?.series_id)!==series)return [];
    return addresses(page.video_player_info||{});
  }
  const cache=new Map(),contentTypes=new Map();
  const parseJSON=text=>JSON.parse(String(text).replace(/"(?:\\.|[^"\\])*"|(-?\d{16,})(?=[,}\]\s])/g,(token,integer)=>integer?'"'+integer+'"':token));
  async function appRequest(path,payload){
    const now=Date.now(),values={aid:'8662',app_name:'novelread',version_code:'73532',version_name:'7.3.5.32',manifest_version_code:'73532',update_version_code:'73532',channel:'update_64',device_platform:'android',os:'android',ssmix:'a',device_type:'25053RT47C',device_brand:'Redmi',language:'zh',os_api:'36',os_version:'16',resolution:'1280*2772',dpi:'520',ac:'wifi',device_id:deviceId,iid:installId,_rticket:String(now)};
    const query=pythonUrlEncode(Object.fromEntries(Object.entries(values).sort(([a],[b])=>a<b?-1:a>b?1:0))).replace(/\*/g,'%2A');
    const body=JSON.stringify(payload);
    // Released client omits the legacy signature on the media endpoint: sending it
    // suppresses the response body (HTTP 200, zero bytes). Other APIs still sign.
    const response=await axios.post('https://api5-normal-sinfonlineb.fqnovel.com'+path+'?'+query,body,{headers:{'User-Agent':ua,Accept:'application/json','X-XS-From-Web':'0','Sdk-Version':'2','Content-Type':'application/json; charset=utf-8',...(path==='/novel/player/video_model/v1/'?{}:sign(query,body,now))},responseType:'text'});
    const data=parseJSON(response.data);if(response.status!==200||Number(data.code??data.Code??data.status_code??data.BaseResp?.StatusCode??0)!==0)throw Error('新版 App 播放接口暂不可用');
    return data;
  }
  async function videoInfo(id){
    if(!valid(id))throw Error('分集 ID 无效');const hit=cache.get(id);if(hit&&Date.now()-hit.time<240000)return hit.value;
    const data=await appRequest('/novel/player/video_model/v1/',{biz_param:{need_all_video_definition:true,video_platform:3},content_type:contentTypes.get(id)||1,video_id:id});
    const raw=data.data?.video_model,model=typeof raw==='string'?parseJSON(raw):raw,value=rowsFromModel(model);
    cache.set(id,{time:Date.now(),value});while(cache.size>4)cache.delete(cache.keys().next().value);return value;
  }
  function card(raw){const row=raw?.video_data||raw||{},id=String(row.series_id_str||row.series_id||'');return {id,title:String(row.series_title||row.series_name||row.title||''),cover:String(row.series_cover||row.cover||''),description:String(row.series_intro||''),badge:row.episode_cnt?'共 '+row.episode_cnt+' 集':String(row.episode_right_text||''),tags:Array.isArray(row.tags)?row.tags.map(x=>typeof x==='string'?x:x?.name||'').filter(Boolean).join(' / '):''};}
  const catalogCursors=new Map();
  async function catalog(page,type){
    const genres={comic:['comic_series','comic_series'],ai:['ai_series','ai_series']},[genre,scene]=genres[type]||['short_play','default'];
    const cursorKey=type+'|'+page,previous=catalogCursors.get(type+'|'+(page-1));
    if(page>1&&!previous)throw Error('分类分页已过期，请刷新');
    const offset=page>1?previous.offset:0;
    const body={req_scene:scene,offset,limit:18,req_type:'only_content',need_selector_panel:false,client_req_type:offset?2:3,session_id:page>1?previous.session:'',filter_ids:'',select_items:{genre:[genre],sort:type==='latest'?['online_time']:[],gender:[],category_dim_theme:[],category_dim_role:[],category_dim_epoch:[],online_time:[],creation_status:[]}};
    const data=(await appRequest('/reading/distribution/category/landpage/v/',body)).data;
    if(!Array.isArray(data?.video_data)||typeof data.has_more!=='boolean')throw Error('新版分类数据格式异常');
    const items=data.video_data.map(card).filter(x=>valid(x.id)&&x.title),next=Number(data.next_offset);
    if(data.has_more&&(!Number.isSafeInteger(next)||next<=offset||!items.length))throw Error('分类分页未前进');
    const session=String(data.session_id||'');if(session.length>4096||/[\r\n\0]/.test(session))throw Error('分类会话无效');
    catalogCursors.set(cursorKey,{offset:next,session});while(catalogCursors.size>64)catalogCursors.delete(catalogCursors.keys().next().value);
    return {items,page,hasMore:data.has_more};
  }
  async function detail(id){
    if(!valid(id))throw Error('剧集 ID 无效');
    const row=(await appRequest('/novel/player/video_detail/v1/',{series_id:id})).data?.video_data;
    if(String(row?.series_id_str||row?.series_id)!==id||!Array.isArray(row?.video_list))throw Error('新版详情未返回所请求的剧集');
    const episodes=row.video_list.map(v=>({contentType:Number(v.content_type??row.content_type),id:String(v.vid),number:Number(v.vid_index),title:'第 '+v.vid_index+' 集',series:String(v.series_id||id)})).sort((a,b)=>a.number-b.number);
    if(!episodes.length||episodes.length>2000||Number(row.episode_cnt)>episodes.length||new Set(episodes.map(v=>v.id)).size!==episodes.length||episodes.some((v,i)=>!valid(v.id)||v.number!==i+1||v.series!==id))throw Error('新版详情分集不完整');
    for(const episode of episodes){const type=episode.contentType;if(Number.isInteger(type)&&((type>=1&&type<=7)||(type>=1000&&type<=1011)))contentTypes.set(episode.id,type);}
    while(contentTypes.size>6000)contentTypes.delete(contentTypes.keys().next().value);
    return {...card(row),episodes:episodes.map(({series,contentType,...episode})=>episode)};
  }
  async function search(keyword,page){
    if(typeof keyword!=='string'||!keyword.trim()||keyword.length>160||/[\x00-\x1f]/.test(keyword))throw Error('搜索词无效');
    const html=await getText('https://hongguoduanju.com/search/'+encodeURIComponent(keyword)+(page>1?'?page='+page:''),{headers:{'User-Agent':'Mozilla/5.0',Referer:'https://hongguoduanju.com/'}});
    const loaders=extractAssignedJson(html)?.loaderData||{},data=loaders['search_(keyword)/page']||Object.entries(loaders).find(([k])=>k.startsWith('search_'))?.[1];
    if(data?.isSuccess!==true||String(data.query)!==keyword||!Array.isArray(data.searchList))throw Error('新版搜索没有有效结果');
    const items=data.searchList.map(card).filter(x=>valid(x.id)&&x.title);return {items,page,hasMore:items.length>0&&Number(data.pagination?.totalPages||1)>page};
  }
  async function backup(series,id){
    if(!valid(series)||!valid(id))throw Error('备用取流缺少剧集 ID');
    const reference={content_type:1004,from_video_id:'',series_id:series,vid:id,video_platform:3};
    const query=new URLSearchParams({id:Buffer.from(JSON.stringify(reference)).toString('base64')});
    const response=await axios('https://djapi.999888456.xyz/api/hongguo/play?'+query,{headers:{'User-Agent':'Mozilla/5.0',Referer:'https://hongguoduanju.com/'},responseType:'text'});
    const data=decode(response.data);for(const flag of [data.parse,data.jx])if(![undefined,null,false,0,'0',''].includes(flag))throw Error('备用接口未返回直接媒体');
    const rows=[];
    for(const [i,row]of (Array.isArray(data.key_urls)?data.key_urls:[]).entries())try{
      if(!httpUrl(row.src)||!/^[0-9a-fA-F]{32}$/.test(String(row.kid)))continue;
      rows.push({key:'backup-'+i,quality:String(qualityNumber(row.name)),item:{main_url:row.src,phone_key:contentKey(row.spade_a),phone:true,codec_type:''}});
    }catch(_){}
    if(!rows.length)throw Error('备用接口没有可用片源');return {info:{},rows:rows.sort((a,b)=>Number(b.quality)-Number(a.quality))};
  }
  return {videoInfo,backup,catalog,detail,search,publicAddresses,clear:id=>id?cache.delete(id):cache.clear(),sign,decode,contentKey,rowsFromModel};
})();
