const api={categoryPage,itemToCard,routerData,getText,appSearchPage,appSearchParams,signedAppGet,appSearchCells,appSearchVod,fetchVideoInfo,qualityNumber,qualityLabel,decryptSpadeUrl,deriveContentKey,remoteRange,prepareCencHeader,decryptPartial,streamCache,videoInfoCache,pageCache,appSearchCache};
// Retry the same byte range before ending playback. Validate the offset as well
// as its length: a CDN may return a full file or a truncated final range.
api.remoteRange=async(stream,start,end)=>{
  let error;
  for(let attempt=0;attempt<2;attempt++)try{
    const resp=await axios(stream.url,{headers:{'User-Agent':VIDEO_UA,Referer:'https://novel.snssdk.com/',...stream.headers,Range:`bytes=${start}-${end}`,'Accept-Encoding':'identity'},responseType:'arraybuffer'});
    let data=Buffer.from(resp.data),total;
    if(resp.status===206){
      const match=String(resp.headers['content-range']||'').match(/^bytes (\d+)-(\d+)\/(\d+)$/i);
      if(!match)throw new Error('媒体 Range 响应缺少位置');
      const a=Number(match[1]),b=Number(match[2]);total=Number(match[3]);
      if(!Number.isSafeInteger(total)||total<=0||a!==start||b!==Math.min(end,total-1)||data.length!==b-a+1)throw new Error('媒体 Range 位置异常');
    }else if(resp.status===200){
      total=Number(resp.headers['content-length']||data.length);
      if(!Number.isSafeInteger(total)||total!==data.length)throw new Error('媒体完整响应长度异常');
      data=data.subarray(start,Math.min(end+1,total));
    }else throw new Error('媒体响应异常');
    if(start>=total||data.length!==Math.min(end,total-1)-start+1)throw new Error('媒体 Range 长度异常');
    if(stream.total&&total!==stream.total)throw new Error('媒体文件长度发生变化');
    stream.total=total;return {data,total};
  }catch(e){error=e;}
  throw error;
};
const validId=id=>typeof id==='string'&&/^\d{1,30}$/.test(id);
const card=row=>({id:String(row.url),title:String(row.title||''),cover:String(row.img||''),description:String(row.content||''),badge:String(row.desc||''),tags:String(row.tname||'')});
const cards=rows=>[...new Map(Array.from(rows,card).filter(r=>validId(r.id)&&r.title).map(r=>[r.id,r])).values()];
const episodeSeries = new Map();
const firstEpisodes = new Set();
async function publicComicPage(page) {
  const html=await getText(`${SITE}/rank/hot-comic-drama?page=${page}`,{headers:{'User-Agent':'Mozilla/5.0','Referer':SITE+'/'}});
  for(const match of html.matchAll(/<script\b[^>]*data-fn-name="([^"]+)"[^>]*data-fn-args="([^"]+)"[^>]*>/g)) {
    let args;try{args=JSON.parse(decodeHtml(match[2]));}catch(_){continue;}
    if(args[0]!=='rank_hot-comic-drama/page')continue;
    let value;
    if(match[1]==='r' && args[1]==='content')value=args[2];
    if(match[1]==='mergeLoaderData' && Array.isArray(args[1]))value=args[1].find(v=>v.key==='content' && v.routerDataFnName==='p')?.routerDataFnArgs?.[0];
    if(typeof value==='string')try{value=JSON.parse(value);}catch(_){continue;}
    if(!value?.isSuccess || !Array.isArray(value.rankList))continue;
    const pages=Math.min(Number(value.pagination?.totalPages || 0),100);
    if(pages<1 || Number(value.pagination.pageNum || page)!==page)throw new Error('漫剧分页信息异常');
    return {items:cards(value.rankList.map(rankItemToCard)),page,hasMore:page<pages};
  }
  // Some streamed responses render cards before resolving the loader payload.
  const route=extractAssignedJson(html)?.loaderData?.['rank_hot-comic-drama/page'];
  if(route?.rankKey!=='comic' || Number(route.pageNum)!==page)throw new Error('漫剧分页信息异常');
  const text=value=>decodeHtml(String(value||'').replace(/<[^>]*>/g,'')).trim();
  const items=[];
  for(const match of html.matchAll(/<article\b[^>]*aria-labelledby="rank-title-(\d{1,30})"[^>]*>([\s\S]*?)<\/article>/g)) {
    const body=match[2],title=body.match(new RegExp('<h[1-6]\\b[^>]*id="rank-title-'+match[1]+'"[^>]*>([\\s\\S]*?)<\\/h[1-6]>'));
    const cover=body.match(/<img\b[^>]*\bsrc="([^"]+)"/);
    if(title)items.push({id:match[1],title:text(title[1]),cover:cover?decodeHtml(cover[1]):'',badge:'漫剧',description:'',tags:''});
  }
  const nav=html.match(/<nav\b[^>]*aria-label="榜单分页"[^>]*>([\s\S]*?)<\/nav>/)?.[1] || '';
  const current=nav.match(/aria-current="page"[^>]*>(\d+)</)?.[1];
  if(!items.length || Number(current)!==page)throw new Error('漫剧榜单数据暂不可用');
  return {items:[...new Map(items.map(row=>[row.id,row])).values()],page,hasMore:page<100 && /rel="next"/.test(nav)};
}
async function publicWebStream(id) {
  const sid = episodeSeries.get(id) || '_';
  const pageUrl=`${SITE}/player/${sid}${firstEpisodes.has(id)?'':'/'+id}`;
  const html = await api.getText(pageUrl,{headers:{'User-Agent':'Mozilla/5.0','Referer':SITE+'/','Cache-Control':'no-cache'}});
  // The requested episode's public VideoObject is server rendered. Never take
  // a URL from an unrelated recommendation or an arbitrary HTML substring.
  for (const match of html.matchAll(/<script\b[^>]*type=["']application\/ld\+json["'][^>]*>([\s\S]*?)<\/script>/gi)) {
    let node; try { node=JSON.parse(match[1]); } catch (_) { continue; }
    for (const item of Array.isArray(node) ? node : node['@graph'] || [node]) {
      const episodeId=String(item.about?.['@id'] || item['@id'] || '').split(/[?#]/)[0].split('/').filter(Boolean).at(-1);
      const url = String(item.contentUrl || '').replace(/&amp;/g,'&');
      // First episodes use a canonical /player/<series> schema @id. Their
      // /video/public/<episode>/... URL is the authoritative episode identity.
      const publicEpisode=url.startsWith(`${SITE}/video/public/${id}/`)&&/\.mp4(?:[?#]|$)/i.test(url);
      if (item['@type'] !== 'VideoObject' || (episodeId!==id&&!publicEpisode)) continue;
      if (!/^https:\/\//.test(url)) continue;
      const stream = {url,key:null,quality:'官网',codec:'',source:'web',headers:{'User-Agent':'Mozilla/5.0',Referer:pageUrl}};
      await prepare(stream);
      return stream;
    }
  }
  if(typeof PhoneSource!=='undefined'&&validId(sid)){
    let urls=[];try{urls=PhoneSource.publicAddresses(html,sid,id);}catch(_){}
    for(const url of urls)try{
      const stream={url,key:null,quality:'官网',codec:'',source:'web',headers:{'User-Agent':'Mozilla/5.0',Referer:pageUrl}};
      await prepare(stream);return stream;
    }catch(_){}
  }
  throw new Error('官网暂未提供本集的公开视频');
}
function repairVideoEntries(raw, header) {
  let pos = 0;
  while ((pos = raw.indexOf(Buffer.from('encv'), pos)) >= 0) {
    if (pos >= 4) {
      const end = pos - 4 + raw.readUInt32BE(pos - 4);
      if (end > pos + 8 && end <= raw.length) {
        const frma = raw.indexOf(Buffer.from('frma'),pos + 4);
        const declared = frma >= 0 && frma + 8 <= end ? raw.toString('ascii',frma+4,frma+8) : '';
        const avc = raw.indexOf(Buffer.from('avcC'),pos+4);
        const format = ['avc1','avc3','hvc1','hev1'].includes(declared) ? declared : avc >= 0 && avc < end ? 'avc1' : 'hvc1';
        header.write(format,pos,'ascii');
      }
    }
    pos += 4;
  }
  return header;
}

async function prepare(stream) {
  if (stream.prepared) return stream.prepared;
  if (!stream.preparePromise) stream.preparePromise = (async () => {
    const first = stream.initial || await api.remoteRange(stream,0,256*1024-1);
    delete stream.initial;
    stream.prefix=Buffer.from(first.data.subarray(0,256*1024));
    let head = Buffer.from(first.data); const total = first.total;
    if (!Number.isSafeInteger(total) || total < head.length || head.length < 16 || head.toString('ascii',4,8) !== 'ftyp') throw new Error('视频头格式异常');
    if(!stream.key){
      // Plain MP4 files can place free/mdat before moov or put the index at EOF.
      // Walk top-level boxes using small range reads, without downloading mdat.
      let offset=0,moov;
      for(let count=0;offset<total&&count<128;count++){
        const prefix=offset+16<=head.length?head.subarray(offset,offset+16):Buffer.from((await api.remoteRange(stream,offset,Math.min(offset+15,total-1))).data);
        if(prefix.length<8)throw new Error('MP4 box 头不完整');
        let size=prefix.readUInt32BE(0);const type=prefix.toString('ascii',4,8);
        if(size===1){if(prefix.length<16)throw new Error('MP4 扩展 box 头不完整');size=Number(prefix.readBigUInt64BE(8));}
        else if(size===0)size=total-offset;
        if(!Number.isSafeInteger(size)||size<(prefix.readUInt32BE(0)===1?16:8)||offset+size>total)throw new Error('MP4 box 长度异常');
        if(type==='moov'){
          if(size>8*1024*1024)throw new Error('视频索引超出限制');
          moov=offset+size<=head.length?head.subarray(offset,offset+size):Buffer.from((await api.remoteRange(stream,offset,offset+size-1)).data);break;
        }
        offset+=size;
      }
      if(!moov)throw new Error('MP4 缺少视频索引');
      if(moov.indexOf(Buffer.from('encv'))>=0||moov.indexOf(Buffer.from('enca'))>=0)throw new Error('视频不是明文 MP4');
      return stream.prepared={total,header:Buffer.alloc(0),samples:[]};
    }
    const ftypSize = head.readUInt32BE(0);
    if (ftypSize < 8 || ftypSize + 8 > head.length || head.toString('ascii',ftypSize+4,ftypSize+8) !== 'moov') throw new Error('视频索引格式异常');
    const moovSize = head.readUInt32BE(ftypSize), end = ftypSize + moovSize;
    if (moovSize < 8 || end > total || end > 8*1024*1024) throw new Error('视频索引超出限制');
    if (end > head.length) head = Buffer.concat([head,Buffer.from((await api.remoteRange(stream,head.length,end-1)).data)]);
    head = head.subarray(0,end);
    const p = stream.key ? api.prepareCencHeader(head,stream.key) : {header:head,samples:[]};
    if (p.samples.length > 300000) throw new Error('视频索引过大');
    repairVideoEntries(head,p.header);
    return stream.prepared = {total,header:p.header,samples:p.samples};
  })();
  try {return await stream.preparePromise;} finally {delete stream.preparePromise;}
}

async function appMedia(value,quality,tier) {
  const {info,rows}=value;let stream=null;

      const compatible = Array.from(rows).filter(row => !['bytevc2','bvc2'].includes(String(row.item.codec_type || '').toLowerCase()));
      const avc = compatible.filter(row => /^(avc|h264|h\.264|avc1)$/i.test(String(row.item.codec_type || '')));
      const pool = avc.length ? avc : compatible;
      const preferred=pool.filter(row=>api.qualityNumber(row.quality)<=quality);
      const candidates=tier?compatible.filter(row=>String(row.key)===tier):[...preferred,...pool.filter(row=>!preferred.includes(row))];
      if (!candidates.length) throw new Error('没有兼容的播放资源');
      const seed = Buffer.from(String(info.key_seed || ''),'base64');
      let lastError;
      for(const selected of candidates.slice(0,3)){
        for(const encoded of [...new Set([selected.item.main_url,selected.item.backup_url].filter(v=>typeof v==='string'&&v))])try{
          let url=encoded;if(seed.length)url=api.decryptSpadeUrl(url,seed);
          if(!/^https?:\/\//.test(url))throw new Error('播放地址无效');
          const value={url,key:selected.item.phone ? selected.item.phone_key : selected.item.spade_a ? api.deriveContentKey(selected.item.spade_a) : null,quality:api.qualityLabel(selected.quality),codec:String(selected.item.codec_type || ''),source:selected.item.phone?'phone':'app'};
          await prepare(value);stream={time:Date.now(),value};break;
        }catch(error){lastError=error;}
        if(stream)break;
      }
      if(!stream)throw lastError || new Error('没有兼容的播放资源');
  return stream;
}

const provider = {
  async home(page = 1, type = 'comic') {
    if(globalThis.SwitchConfig?.source==='phone'&&['short','human','comic','ai','latest'].includes(type))try{return await PhoneSource.catalog(page,type);}catch(_){}

    if(type==='comic'){
      try {return await publicComicPage(page);}
      catch (_) {const r=await appSearchPage('热门',19,page,24);return {items:cards(r.list),page,hasMore:r.hasMore};}
    }
    const keywords={human:'短剧',ai:'AI剧',drama:'剧情',ancient:'古装',comedy:'喜剧',fantasy:'奇幻'};
    if(keywords[type]){const r=await appSearchPage(keywords[type],11,page,24);return {items:cards(r.list),page,hasMore:r.hasMore};}
    const data = await api.categoryPage(type==='latest'?'tab=1&sort_type=2':'tab=1&sort_type=1',page);
    const section = data?.loaderData?.category_page || data?.loaderData?.['category_$'];
    const rows = section?.recommendList || section?.categoryData?.recommendList;
    if (!Array.isArray(rows)) throw new Error('首页数据结构已变化');
    const items = cards(rows.map(api.itemToCard));
    const totalPages = Math.min(Number(section.pagination?.totalPages || 100),100);
    return {items,page,totalPages,hasMore:items.length>0 && page<totalPages};
  },
  async search(keyword, page = 1, type = 'comic') {
    if(globalThis.SwitchConfig?.source==='phone')try{return await PhoneSource.search(keyword,page);}catch(_){}

    const tabType = type === 'comic' ? 19 : 11;
    // Inspect the response rather than treating a missing search tab as an empty success.
    let cursor = {};
    const key = `${tabType}|${keyword}|${page}`;
    if (page === 1) for (const k of api.appSearchCache.keys()) if (k.startsWith(`${tabType}|${keyword}|`)) api.appSearchCache.delete(k);
    const cached = api.appSearchCache.get(key);
    if (cached && Date.now()-cached.time<300000) return {items:cards(cached.value.list),page,hasMore:cached.value.hasMore};
    if (page > 1) {
      const previous = api.appSearchCache.get(`${tabType}|${keyword}|${page-1}`);
      if (!previous || Date.now()-previous.time>=300000) throw new Error('搜索结果已过期，请从第 1 页重新搜索');
      cursor = previous.value.cursor;
    }
    try {
      const data = await api.signedAppGet(api.appSearchParams(keyword,tabType,20,cursor,page));
      const tab = (data.search_tabs || data.data?.search_tabs || []).find(row => Number(row.tab_type) === tabType);
      if (!tab || !Array.isArray(tab.data)) throw new Error('搜索接口暂不可用');
      const list = Array.from(tab.data).flatMap(cell => Array.from(api.appSearchCells(cell))).map(api.appSearchVod).filter(Boolean);
      const value = {list,hasMore:!!tab.has_more,cursor:{search_id:String(tab.search_id || ''),passback:String(tab.passback || ''),next_offset:String(tab.next_offset || '')}};
      api.appSearchCache.set(key,{time:Date.now(),value});
      return {items:cards(list),page,hasMore:value.hasMore && page<100};
    } catch (error) {
      if (type === 'comic') throw error;
      const data = await api.routerData(`${SITE}/search/${encodeURIComponent(keyword)}?page=${page}`);
      const section = data?.loaderData?.['search_(keyword)/page'] || data?.loaderData?.search_page;
      if (!Array.isArray(section?.searchList)) throw new Error('搜索数据不可用');
      const items = cards(section.searchList.map(api.itemToCard));
      return {items,page,hasMore:items.length>0 && page<100};
    }
  },
  async detail(id) {
    if (!validId(id)) throw new Error('剧集 ID 无效');
    if(globalThis.SwitchConfig?.source==='phone')try{
      const detail=await PhoneSource.detail(id);for(const episode of detail.episodes)episodeSeries.set(episode.id,id);firstEpisodes.add(detail.episodes[0].id);return detail;
    }catch(_){}
    const data = await api.routerData(`${SITE}/detail?series_id=${id}`);
    const series = data?.loaderData?.detail_page?.seriesDetail;
    if (!series || !Array.isArray(series.vid_list) || !series.vid_list.length || series.vid_list.length>2000) throw new Error('分集暂不可用');
    const episodes = Array.from(series.vid_list,(vid,index) => ({id:String(vid),number:index+1,title:`第 ${index+1} 集`}));
    if (episodes.some(e => !validId(e.id)) || new Set(episodes.map(e => e.id)).size !== episodes.length) throw new Error('分集数据异常');
    for (const episode of episodes) episodeSeries.set(episode.id,id);
    firstEpisodes.add(episodes[0].id);
    while(episodeSeries.size>6000){const expired=episodeSeries.keys().next().value;episodeSeries.delete(expired);firstEpisodes.delete(expired);}
    return {...card(api.itemToCard(series)),id,episodes};
  },
  async stream(id, quality = 720, fresh = false, tier = "") {
    if (!validId(id)) throw new Error('分集 ID 无效');
    if(typeof tier!=="string"||tier==="."||tier===".."||tier.length>64||!/^[a-zA-Z0-9_.-]*$/.test(tier))throw new Error('清晰度参数无效');
    const key = `${id}|desktop-${quality}${tier?'|tier:'+tier:''}`;
    if (fresh) {api.streamCache.delete(key); api.videoInfoCache.delete(id);if(typeof PhoneSource!=='undefined')PhoneSource.clear(id);}
    let stream = api.streamCache.get(key);
    if (!stream || Date.now()-stream.time>=240000) {
      stream=null;
      let web;
      if (globalThis.SwitchConfig?.source === 'web') try { web=await publicWebStream(id); } catch (_) {}
      if (web) {
        stream={time:Date.now(),value:web};
      } else {
      try {
      stream=await appMedia(await (globalThis.SwitchConfig?.source==='phone'?PhoneSource.videoInfo(id):api.fetchVideoInfo(id)),quality,tier);
      } catch (appError) {
        // Explicit web mode already tried the website above. In the default
        // mode, recover only with a public URL verified for this exact episode.
        if (tier || globalThis.SwitchConfig?.source === 'web') throw appError;
        try { stream={time:Date.now(),value:await publicWebStream(id)}; }
        catch (_) {
          if(globalThis.SwitchConfig?.source==='phone')try{stream=await appMedia(await PhoneSource.backup(episodeSeries.get(id),id),quality,tier);}catch(_){}
          if(!stream)throw new Error('App、官网及备用片源均不可用，请稍后重试或选择其他剧集');
        }
      }
      }
      api.streamCache.set(key,stream);
      while(api.streamCache.size>4)api.streamCache.delete(api.streamCache.keys().next().value);
    }
    const value = stream.value, prepared = await prepare(value);
    return {total:prepared.total,quality:value.quality,codec:value.codec,source:value.source || 'app',
      chunk:async (start,end) => {
        let data;
        const last=Math.min(end,prepared.total-1),prefix=value.prefix;
        if(prefix&&start<prefix.length){const until=Math.min(last+1,prefix.length);data=prefix.subarray(start,until);if(last>=prefix.length)data=Buffer.concat([data,(await api.remoteRange(value,prefix.length,last)).data]);}
        else data=(await api.remoteRange(value,start,last)).data;
        return api.decryptPartial(data,start,value.key,prepared);
      }};
  }
};


const active=new Map();globalThis.SwitchAPI={downloadSlice:async(id,start)=>{
 if(!validId(id)||!Number.isSafeInteger(start)||start<0)throw new Error('下载分段无效');
 const stream=await provider.stream(id,480,false);if(start>=stream.total)throw new Error('下载范围无效');
 return {total:stream.total,hex:Buffer.from(await stream.chunk(start,Math.min(start+1048575,stream.total-1))).toString('hex')};
},qualities:async(id)=>{
 if(!validId(id))throw new Error('分集 ID 无效');if(globalThis.SwitchConfig?.source==='web')return [];
 const {rows}=await (globalThis.SwitchConfig?.source==='phone'?PhoneSource.videoInfo(id):api.fetchVideoInfo(id));return Array.from(rows).filter(r=>r.item.main_url&&!['bytevc2','bvc2'].includes(String(r.item.codec_type||'').toLowerCase())).filter(r=>/^[a-zA-Z0-9_.-]{1,64}$/.test(String(r.key))).map(r=>({key:String(r.key),label:api.qualityLabel(r.quality).slice(0,32),codec:String(r.item.codec_type||'').slice(0,16)}));
},prefetch:async(id,q=480,tier="")=>{try{await provider.stream(id,q,false,tier);return {ready:true};}catch(_){return {ready:false};}},clearCache:()=>{const count=active.size+api.streamCache.size+api.videoInfoCache.size;active.clear();api.streamCache.clear();api.videoInfoCache.clear();if(typeof PhoneSource!=='undefined')PhoneSource.clear();return {entries:count};},home:provider.home,search:provider.search,detail:provider.detail,open:async(id,q=480,fresh=true,tier="")=>{active.delete(id);const s=await provider.stream(id,q,fresh,tier);active.set(id,s);while(active.size>2)active.delete(active.keys().next().value);return {total:s.total,quality:s.quality,codec:s.codec,source:s.source};},read:async(id,a,b)=>{const s=active.get(id);if(!s||a<0||b<a||b-a>=1048576)throw new Error('Range');return Buffer.from(await s.chunk(a,Math.min(b,s.total-1))).toString('hex');},selftest:()=>({protocol:protocolVector(),ctr:nativeCrypto.createDecipheriv('aes-128-ctr',Buffer.from('2b7e151628aed2a6abf7158809cf4f3c','hex'),Buffer.from('f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff','hex')).update(Buffer.from('874d6191b620e3261bef6864990db6ce','hex')).toString('hex'),cbc:nativeCrypto.createDecipheriv('aes-128-cbc',Buffer.from('2b7e151628aed2a6abf7158809cf4f3c','hex'),Buffer.from('000102030405060708090a0b0c0d0e0f','hex')).setAutoPadding(false).update(Buffer.from('7649abac8119b246cee98e9b12e9197d','hex')).toString('hex'),sm3:nativeCrypto.createHash('sm3').update('abc').digest('hex'),md5:nativeCrypto.createHash('md5').update('abc').digest('hex'),sha512:nativeCrypto.createHash('sha512').update('abc').digest('hex')})};

globalThis.protocolVector=()=>{const d=Date.now,r=Math.random;Date.now=()=>1791875000123;Math.random=()=>0.123456789;try{return nativeCrypto.createHash('md5').update(JSON.stringify(signedVideoRequest(JSON.stringify({mixed_video_id_map:{'1004':['1234567890123456789']}})).headers)).digest('hex');}finally{Date.now=d;Math.random=r;}};
