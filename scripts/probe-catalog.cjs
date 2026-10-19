// SPDX-License-Identifier: GPL-3.0-only
const {ctx}=require('./probe-runtime.cjs'),fs=require('fs'),cp=require('child_process');
(async()=>{const report={commit:cp.execFileSync('git',['rev-parse','HEAD'],{encoding:'utf8'}).trim(),passed:false,pages:[],hardwareTested:false};try{
 const seen=new Set();for(let page=1;page<=3;page++){
  const r=await ctx.SwitchAPI.home(page,'latest');if(!Array.isArray(r.items)||typeof r.hasMore!=='boolean')throw Error('Invalid catalog');
  let added=0;for(const row of r.items)if(row.id&&!seen.has(row.id)){seen.add(row.id);added++;}
  report.pages.push({page,items:r.items.length,added,hasMore:r.hasMore});
  if(page===1&&r.items.length===0)throw Error('Latest is empty');if(page>1&&added===0)throw Error('Catalog did not advance');if(!r.hasMore)break;
 }
 report.passed=true;
}catch(e){report.error=String(e.message).replace(/https?:\/\/\S+/g,'[URL]');process.exitCode=1;}finally{fs.mkdirSync('build',{recursive:true});fs.writeFileSync('build/catalog-probe.json',JSON.stringify(report,null,2)+'\n');console.log(JSON.stringify(report));}})();
