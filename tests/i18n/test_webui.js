// The Russian layer of the settings page (the "i18n" block at the end of
// src/webui.html). English is the source text of the page and RU maps it to
// Russian by the text itself, so an English string that is edited later
// silently loses its translation. This fails when that happens: every key of
// RU must still be text of the page. No browser needed.
const assert=require('node:assert/strict');
const fs=require('node:fs');
const vm=require('node:vm');
const html=fs.readFileSync(process.env.WEBUI_HTML||'src/webui.html','utf8');
const between=(a,b)=>{const i=html.indexOf(a),k=html.indexOf(b,i);assert.ok(i>=0&&k>i,'page text not found: '+a);return html.slice(i,k)};

// The dictionary, as the page defines it.
const ctx={};vm.createContext(ctx);
vm.runInContext(between('// i18n dict begin','// i18n dict end'),ctx);
const RU=ctx.RU;
assert.ok(RU&&Object.keys(RU).length>200,'RU dictionary not found or too small');

// The page without the i18n block, as one line of plain text: entities decoded,
// the escapes of the code's string literals removed, links as a bare <a>.
const ENT={amp:'&',lt:'<',gt:'>',nbsp:' ',quot:'"',mdash:'—',times:'×',deg:'°'};
const plain=s=>s.replace(/<a [^>]*>/g,'<a>').replace(/\\(['"])/g,'$1')
 .replace(/&([a-z]+);/g,(m,n)=>n in ENT?ENT[n]:m).replace(/\s+/g,' ').trim();
const i=html.indexOf('// i18n begin'),k=html.indexOf('// i18n end');
assert.ok(i>0&&k>i,'markers "// i18n begin" / "// i18n end" not found');
const page=plain(html.slice(0,i)+html.slice(k));

const lost=[],bad=[];
for(const key of Object.keys(RU)){
 const val=RU[key];
 if(typeof val!=='string'||!val.trim())bad.push('empty translation: '+key);
 if(key!==key.replace(/\s+/g,' ').trim())bad.push('key has stray spaces: '+key);
 // {0}, {1}... are the parts the code fills in; the text between them must be on the page
 const parts=plain(key).split(/\{\d\}/).map(p=>p.trim()).filter(Boolean);
 if(!parts.some(p=>/[A-Za-z가-힣]/.test(p)))bad.push('key has no text: '+key);
 for(const p of parts)if(!page.includes(p)){lost.push(key+(parts.length>1?'   [part: '+p+']':''));break}
 // the translation uses each placeholder of the key, and no other
 const want=[...new Set(key.match(/\{\d\}/g)||[])].sort().join(),got=[...new Set(val.match(/\{\d\}/g)||[])].sort().join();
 if(want!==got)bad.push('placeholders differ: '+key);
 // a hint translated as HTML keeps its tags
 const tags=s=>(s.match(/<\/?[a-z]+>/g)||[]).sort().join();
 if(tags(key)!==tags(val))bad.push('tags differ: '+key);
}
assert.deepStrictEqual(bad,[],'broken dictionary entries');
assert.deepStrictEqual(lost,[],'these keys are no longer text of the page (was the English edited?)');

// The language list: English and Russian, switched on the spot.
const sel=/<select id="lang"[^>]*>([\s\S]*?)<\/select>/.exec(html);
assert.ok(sel,'the language select is gone');
assert.deepStrictEqual([...sel[1].matchAll(/<option value="([^"]*)"/g)].map(m=>m[1]),['en','ru']);
assert.match(sel[0],/onchange="setLang\(this\.value\)"/,'the select must switch the page');
assert.match(between('function loadConfig(){','\n})}'),/setLang\(/,'loadConfig applies the stored language');
assert.match(between('function collect(){','function saveAll(){'),/lang:gv\('lang'\)\|\|'en'/,'the language is saved with the form');

// The lookup itself, run on the page's own code.
const code=between('// i18n dict end','function i18nHtml(');
const c2={RU};vm.createContext(c2);vm.runInContext(code,c2);
assert.strictEqual(c2.t('Save settings'),'Save settings','English is left alone');
vm.runInContext("LANG='ru'",c2);
assert.strictEqual(c2.t('Save settings'),RU['Save settings']);
assert.strictEqual(c2.t('  Brightness: '),'  '+RU['Brightness:']+' ','spaces around the text are kept');
assert.strictEqual(c2.t('Save failed: Request failed'),RU['Save failed: {0}'].replace('{0}',RU['Request failed']),'a filled-in part is translated too');
assert.strictEqual(c2.t('3d 2h 5m'),'3 д 2 ч 5 мин');
assert.strictEqual(c2.t('Tunnel up, last handshake 45s ago.'),'Туннель работает, последнее рукопожатие 45 с назад.');
assert.strictEqual(c2.t('5 aircraft, HTTP 200, tried 3s ago, last ok 3s ago'),
 'самолётов: 5, HTTP 200, последняя попытка 3 с назад, последний успешный запрос 3 с назад');
assert.strictEqual(c2.t('smalltv-desk'),'smalltv-desk','unknown text passes through');
console.log('i18n webui tests passed ('+Object.keys(RU).length+' strings)');
