import test from 'node:test';
import assert from 'node:assert/strict';
import { parseMetricLine, createLineBuffer, elapsedActive, advanceSteps, summarize, demoSample } from '../src/core.js';

test('processed metrics accept documented aliases and preserve absent fields', () => {
  assert.deepEqual(parseMetricLine('{"cadence_spm":112,"stride_time_s":1.07,"step_count":42}'), {cadence:112, strideTime:1.07, steps:42, alert:false});
  assert.deepEqual(parseMetricLine('{"cadence":0}'), {cadence:0, alert:false});
});
test('raw IMU/debug text and invalid numeric values cannot masquerade as gait data', () => {
  for (const line of ['Booting...', '{"ax":1,"ay":2}', '{"cadence":null}', '{"cadence":""}', '{"cadence":true}', '{"cadence":999}', '{"steps":1.5}', '[]']) assert.equal(parseMetricLine(line), null);
});
test('stream handles split Unicode, CRLF, multiple packets and oversized-line recovery', () => {
  const lines=[]; const buffer=createLineBuffer(line=>lines.push(line),40);
  buffer.push('{"cad'); buffer.push('ence":112}\r\n{"steps":2}\n');
  buffer.push('x'.repeat(41)); buffer.push('tail\n{"cadence":100}\n');
  assert.deepEqual(lines, ['{"cadence":112}', '{"steps":2}', '{"cadence":100}']);
});
test('paused time is excluded and elapsed time cannot go backwards', () => {
  assert.equal(elapsedActive({elapsedMs:2000,status:'running',resumedAt:1000},4000),5000);
  assert.equal(elapsedActive({elapsedMs:2000,status:'paused',resumedAt:1000},4000),2000);
  assert.equal(elapsedActive({elapsedMs:2000,status:'running',resumedAt:5000},4000),2000);
});
test('hardware step baseline and reset never count pre-session steps', () => {
  assert.deepEqual(advanceSteps(null,1000), {previous:1000,delta:0});
  assert.deepEqual(advanceSteps(1000,1003), {previous:1003,delta:3});
  assert.deepEqual(advanceSteps(1003,0), {previous:0,delta:0});
});
test('summary averages only reported values, including valid zeros', () => {
  assert.deepEqual(summarize({samples:[{cadence:100},{cadence:0,strideTime:1.2}]}),{cadence:50,strideTime:1.2,impact:null,variability:null});
  assert.equal(summarize({samples:[]}).cadence,null);
  assert.ok(Math.abs(demoSample(5).cadence*demoSample(5).strideTime-120)<1e-9);
});
