const test = require('node:test');
const assert = require('node:assert/strict');
const vm = require('node:vm');
const fs = require('node:fs');
const path = require('node:path');
const tick = () => new Promise(resolve => setImmediate(resolve));
function harness() {
  const ids = ['camera','status','caption','person','results','overlay','cw','ch','connect','disconnect'];
  const elements = Object.fromEntries(ids.map(id => [id, {textContent:'',value:'',checked:true,disabled:false}]));
  const drawn = [], writes = [];
  const ctx = {clearRect(){drawn.length=0;},drawImage(img){drawn.push(img.src);},strokeRect(){},fillText(){}};
  elements.camera.getContext = () => ctx;
  let controller, closed=false, now=1000, interval;
  const port = {
    async open(){}, async close(){closed=true;},
    readable: new ReadableStream({start(c){controller=c;}}),
    writable: new WritableStream({write(bytes){writes.push(new TextDecoder().decode(bytes));}})
  };
  class FakeImage {
    constructor(){this.naturalWidth=320;this.naturalHeight=240;}
    async decode(){if(this.src.endsWith('bad'))throw Error('bad JPEG');}
  }
  const context=vm.createContext({document:{getElementById:id=>elements[id]},
    navigator:{serial:{requestPort:async()=>port}},TextDecoder,TextEncoder,Image:FakeImage,
    Date:{now:()=>now},setInterval(fn){interval=fn;}});
  for(const file of ['protocol.js','viewer.js']) vm.runInContext(fs.readFileSync(path.join(__dirname,'../viewer',file),'utf8'),context);
  return {elements,drawn,writes,closed:()=>closed,
    send(message){const text=JSON.stringify(message)+'\r\n';for(let i=0;i<text.length;i+=13)controller.enqueue(new TextEncoder().encode(text.slice(i,i+13)));},
    stale(){now+=21000;interval();}};
}
const frame=(sequence,image='jpeg',boxes=[[100,80,40,20,90,0]])=>({kind:'frame',sequence,person:boxes.length>0,person_class:0,threshold:60,data:{image,boxes,perf:[1,2,3]}});
test('viewer renders paired capture, clears absent image and disconnects cleanly', async()=>{
  const h=harness(); await h.elements.connect.onclick();
  h.send(frame(1)); await tick(); await tick();
  assert.match(h.elements.caption.textContent,/Capture 1/);assert.equal(h.drawn.length,1);
  assert.match(h.elements.person.textContent,/ON/);assert.deepEqual(h.writes,['n','n']);
  h.send(frame(2,'',[]));await tick();await tick();
  assert.equal(h.drawn.length,0);assert.match(h.elements.status.textContent,/without a camera image/);
  assert.match(h.elements.person.textContent,/OFF/);
  await h.elements.disconnect.onclick();await tick();
  assert.equal(h.closed(),true);assert.equal(h.elements.connect.disabled,false);
  assert.match(h.elements.person.textContent,/unknown/);
});
test('bad JPEG, module error and stale timeout never leave an old image visible',async()=>{
  const h=harness();await h.elements.connect.onclick();h.send(frame(1));await tick();await tick();
  h.send(frame(2,'bad'));await tick();await tick();assert.equal(h.drawn.length,0);
  assert.match(h.elements.status.textContent,/could not be decoded/);
  h.send(frame(3));await tick();await tick();h.stale();assert.equal(h.drawn.length,0);
  h.send({kind:'error',message:'Camera timed out'});await tick();await tick();
  assert.equal(h.elements.status.textContent,'Camera timed out');assert.equal(h.drawn.length,0);
  await h.elements.disconnect.onclick();await tick();
});
test('repeated Grove connection errors stay visible, and ready resumes capture',async()=>{
  const h=harness();await h.elements.connect.onclick();
  const err={kind:'error',message:'Cannot connect to Grove Vision AI V2. Retrying every 3 s; check I2C wiring/power.'};
  h.send(err);await tick();await tick();
  for(let i=0;i<10;i++){h.stale();h.send(err);await tick();await tick();}
  assert.match(h.elements.status.textContent,/Cannot connect to Grove/);
  h.send({kind:'ready'});await tick();await tick();
  assert.match(h.elements.status.textContent,/connected/);
  const before=h.writes.length;h.send(frame(1));await tick();await tick();
  assert.match(h.elements.caption.textContent,/Capture 1/);assert.equal(h.writes.length,before+1);
  await h.elements.disconnect.onclick();await tick();
});
