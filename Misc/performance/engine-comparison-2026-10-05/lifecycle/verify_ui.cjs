const fs=require('fs'),path=require('path'),assert=require('assert');
const {chromium}=require('C:/Users/Tim Bergeron/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright');
const dir=path.resolve(__dirname,'../../Misc/performance/engine-comparison-2026-10-05');
(async()=>{
 const browser=await chromium.launch({headless:true,executablePath:'C:/Program Files/Google/Chrome/Application/chrome.exe'});
 const page=await browser.newPage({viewport:{width:1365,height:1000},acceptDownloads:true}),errors=[];page.on('pageerror',e=>errors.push(e.message));
 await page.goto('file:///'+path.join(dir,'index.html').replaceAll('\\','/'));await page.waitForSelector('.life-row');
 const d=await page.locator('#data').evaluate(el=>JSON.parse(el.textContent)),f=await page.locator('#fps-data').evaluate(el=>JSON.parse(el.textContent));
 assert.equal(d.raw.results.length,115);assert.equal(d.metrics.length,7);assert.equal(Object.keys(d.raw.engines).length,6);assert.equal(f.raw.samples.length,324);
 for(const m of d.metrics){
  await page.locator(`#life-metrics [data-key="${m.key}"]`).click();assert.equal(await page.locator('#life-title').innerText(),m.title);
  assert.equal(await page.locator('.life-row').count(),6);assert.equal(await page.evaluate(()=>document.activeElement.dataset.key),m.key);
  for(const k of Object.keys(d.raw.engines)){
   await page.locator(`.life-row[data-engine="${k}"]`).click();assert.equal(await page.evaluate(()=>document.activeElement.dataset.engine),k);
   if(m.key==='connect_remote'&&k==='ezquake')assert.equal(await page.locator('#detail tbody tr').count(),0);else assert.equal(await page.locator('#detail tbody').nth(1).locator('tr').count(),5);
   const s=m.stats[k];assert.equal(await page.locator('#detail .advantage').innerText(),s.median===null?'N/A':`${Math.round(s.median).toLocaleString()} ms`);
   const logs=await page.locator('#detail .life-log').evaluateAll(els=>els.map(a=>decodeURIComponent(new URL(a.href).pathname).replace(/^\/([A-Za-z]:)/,'$1')));assert(logs.every(p=>fs.existsSync(p)));
   assert.equal(logs.length,m.key==='connect_remote'&&k==='ezquake'?0:5);
  }
  if(m.key==='connect'){
   assert.deepEqual(await page.locator('.life-group').allTextContents(),['NetQuake · same local server','QuakeWorld · separate compatible server']);
   assert.equal(await page.locator('.life-row').last().getAttribute('data-engine'),'ezquake');
  }
  if(m.key==='connect_remote'){
   assert.deepEqual(await page.locator('.life-group').allTextContents(),['NetQuake · Denver · aerowalk','QuakeWorld · not applicable']);
   await page.locator('.life-row[data-engine="ezquake"]').click();assert((await page.locator('#detail').innerText()).includes('Not attempted'));
   await page.locator('.life-row[data-engine="qss"]').click();
  }
  await page.locator('#unit-s').click();assert.equal(await page.locator('#unit-s').getAttribute('aria-pressed'),'true');
  const value=m.stats.qss.median;assert.equal(await page.locator('#detail .advantage').innerText(),value===null?'N/A':`${(value/1000).toFixed(3)} s`);
  await page.locator('#unit-ms').click();
 }
 await page.locator('#tab-samples').click();assert.equal(await page.locator('#all-samples tbody tr').count(),30);assert(await page.locator('#view-compare').isHidden());
 await page.locator('#tab-samples').press('ArrowLeft');assert.equal(await page.locator('#tab-compare').getAttribute('aria-selected'),'true');assert.equal(await page.evaluate(()=>document.activeElement.id),'tab-compare');
 async function exported(button,file){const event=page.waitForEvent('download');await page.locator(button).click();const dl=await event;await dl.saveAs(path.join(__dirname,file));return fs.readFileSync(path.join(__dirname,file),'utf8');}
 assert.deepEqual(JSON.parse(await exported('#life-export','life-export-check.json')),d.raw);
 const all=JSON.parse(await exported('#export','all-export-check.json'));assert.deepEqual(all,{lifecycle:d.raw,fps:f.raw});
 await page.locator('#tab-samples').click();const csv=await exported('#csv','life-export-check.csv');assert.equal(csv.trim().split(/\r?\n/).length,31);
 const filecsv=fs.readFileSync(path.join(dir,'samples.csv'),'utf8');assert.equal(csv.replaceAll('\r','').trim(),filecsv.replaceAll('\r','').trim());await page.locator('#tab-compare').click();
 await page.locator('#life-metrics [data-key="connect_remote"]').click();await page.locator('.life-row[data-engine="fteqw"]').click();await page.locator('#lifecycle').scrollIntoViewIfNeeded();await page.waitForTimeout(450);await page.screenshot({path:path.join(dir,'lifecycle-desktop-preview.png')});
 await page.locator('#theme').click();assert(await page.locator('body').evaluate(el=>el.classList.contains('light')));await page.screenshot({path:path.join(dir,'lifecycle-light-preview.png')});await page.locator('#theme').click();
 await page.setViewportSize({width:390,height:844});assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth),false);
 await page.locator('#life-metrics [data-key="connect_remote"]').click();await page.locator('#lifecycle').scrollIntoViewIfNeeded();await page.waitForTimeout(450);await page.screenshot({path:path.join(dir,'lifecycle-mobile-preview.png')});
 await page.locator('#fps-maps [data-map="ctf2m8"]').click();assert.equal(await page.locator('#fps-map-title').innerText(),'ctf2m8');assert.equal(await page.locator('.fps-row').count(),6);
 await page.emulateMedia({reducedMotion:'reduce'});assert.equal(await page.locator('html').evaluate(el=>getComputedStyle(el).scrollBehavior),'auto');assert.deepEqual(errors,[]);
 await browser.close();const result={result:'passed',checks:['7 lifecycle tests × 6 selectable engine rows','five native log links per applicable selection; ezQuake Denver explicitly unattempted','30 lifecycle sample rows','keyboard tab navigation and restored selection focus','millisecond/second toggle','exact lifecycle JSON export','exact combined lifecycle + FPS export','CSV export matches all 30 file rows','light/dark themes','390px mobile layout without page overflow','preserved FPS interaction','reduced motion','no JavaScript errors']};fs.writeFileSync(path.join(dir,'lifecycle/ui-verification.json'),JSON.stringify(result,null,2));console.log(JSON.stringify(result,null,2));
})().catch(e=>{console.error(e.stack);process.exit(1)});
