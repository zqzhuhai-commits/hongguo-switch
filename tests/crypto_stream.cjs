// SPDX-License-Identifier: GPL-3.0-only
const fs=require('fs'),vm=require('vm'),crypto=require('crypto'),assert=require('assert/strict');
const ctx={__aes:(algo,key,iv,input)=>{const d=crypto.createDecipheriv(algo,Buffer.from(key,'hex'),Buffer.from(iv,'hex'));d.setAutoPadding(false);return Buffer.concat([d.update(Buffer.from(input,'hex')),d.final()]).toString('hex');}};
vm.createContext(ctx);for(const f of ['deps.js','prelude.js'])vm.runInContext(fs.readFileSync('romfs/'+f,'utf8'),ctx);vm.runInContext('this.cipher=nativeCrypto;this.NativeBuffer=Buffer;',ctx);
for(const iv of [Buffer.alloc(16),Buffer.alloc(16,255)]){
 const key=crypto.randomBytes(16),input=crypto.randomBytes(257),gold=crypto.createDecipheriv('aes-128-ctr',key,iv).update(input);
 const d=ctx.cipher.createDecipheriv('aes-128-ctr',ctx.NativeBuffer.from(key),ctx.NativeBuffer.from(iv));let at=0;const parts=[];
 for(const n of [0,1,14,2,15,17,32,176]){parts.push(Buffer.from(d.update(ctx.NativeBuffer.from(input.subarray(at,at+n)))));at+=n;}
 assert.equal(at,input.length);assert.deepEqual(Buffer.concat(parts),gold);
 const partial=ctx.cipher.createDecipheriv('aes-128-ctr',ctx.NativeBuffer.from(key),ctx.NativeBuffer.from(iv));partial.update(ctx.NativeBuffer.alloc(7));
 const expected=crypto.createDecipheriv('aes-128-ctr',key,iv);expected.update(Buffer.alloc(7));assert.deepEqual(Buffer.from(partial.update(ctx.NativeBuffer.from(input))),expected.update(input));
}
console.log('PASS: native CTR streaming, skipped keystream bytes, unaligned chunks and counter rollover');
