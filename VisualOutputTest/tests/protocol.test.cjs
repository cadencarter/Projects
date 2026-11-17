const test = require('node:test');
const assert = require('node:assert/strict');
const { Lines, boxes, rectangle, MAX_LINE } = require('../viewer/protocol.js');
test('split USB chunks and multiple CRLF records stay paired', () => {
  const out = [], errors = [], p = new Lines(x => out.push(x), e => errors.push(e));
  const frames = [{kind:'frame',sequence:1,data:{image:'abc',boxes:[[10,20,4,8,90,0]]}}, {kind:'frame',sequence:2,data:{image:'def',boxes:[]}}];
  const wire = frames.map(x => JSON.stringify(x)+'\r\n').join('');
  for (let i=0;i<wire.length;i+=7) p.push(wire.slice(i,i+7));
  assert.deepEqual(out, frames); assert.deepEqual(errors, []);
});
test('bad and oversized lines recover at the next newline', () => {
  const out=[],errors=[],p=new Lines(x=>out.push(x),e=>errors.push(e));
  p.push('bad\n'+'a'.repeat(MAX_LINE+1)+'\n{"kind":"error"}\n');
  assert.equal(errors.length,2); assert.deepEqual(out,[{kind:'error'}]);
});
test('center coordinates and independent X/Y scaling',()=>{
  assert.deepEqual(rectangle([100,80,40,20,90,0],640,480,320,240),[160,140,80,40]);
});
test('empty/missing detections never retain prior boxes; malformed boxes rejected',()=>{
  assert.deepEqual(boxes({}),[]); assert.deepEqual(boxes({boxes:[]}),[]);
  assert.deepEqual(boxes({boxes:[[1,2,-1,4,90,0],['1',2,3,4,90,0],[1,2,3,4,90,0]]}),[[1,2,3,4,90,0]]);
});
