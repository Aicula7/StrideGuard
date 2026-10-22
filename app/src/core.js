/** Processed gait metrics, not raw accelerometer samples. Units are explicit. */
const aliases = {
  cadence: ['cadence', 'cadence_spm'],
  strideTime: ['strideTime', 'stride_time_s'],
  impact: ['impact', 'impact_g'],
  variability: ['variability', 'variability_pct'],
  steps: ['steps', 'step_count'],
};
const limits = { cadence: [0, 400], strideTime: [0, 10], impact: [0, 100], variability: [0, 100], steps: [0, 1e9] };

export function parseMetricLine(line) {
  let value;
  try { value = JSON.parse(line); } catch { return null; }
  if (!value || typeof value !== 'object' || Array.isArray(value)) return null;
  const out = {};
  for (const [key, names] of Object.entries(aliases)) {
    const found = names.find(name => Object.hasOwn(value, name));
    if (!found) continue;
    const raw = value[found];
    if (raw === null || typeof raw === 'boolean' || (typeof raw !== 'number' && typeof raw !== 'string') || String(raw).trim() === '') return null;
    const number = Number(raw);
    const [min, max] = limits[key];
    if (!Number.isFinite(number) || number < min || number > max || (key === 'steps' && !Number.isInteger(number))) return null;
    out[key] = number;
  }
  if (!Object.keys(out).length) return null;
  out.alert = value.alert === true;
  return out;
}

export function createLineBuffer(onLine, maxLength = 32768) {
  let pending = '';
  let dropping = false;
  return {
    push(text) {
      for (const part of text.split(/(\n)/)) {
        if (part === '\n') {
          if (!dropping && pending.trim()) onLine(pending.trim());
          pending = ''; dropping = false;
        } else if (!dropping) {
          pending += part;
          if (pending.length > maxLength) { pending = ''; dropping = true; }
        }
      }
    },
    reset() { pending = ''; dropping = false; },
  };
}

export function demoSample(t, irregular = false) {
  const cadence = 112 + Math.sin(t * 0.27) * 3.2 + Math.sin(t * 0.83) * 1.1 + (irregular ? Math.sin(t * 1.7) * 14 : 0);
  return { cadence, strideTime: 120 / cadence, impact: 1.7 + Math.sin(t * 0.4) * 0.13,
    variability: irregular ? 16 + Math.sin(t) * 2 : 2.8 + Math.sin(t * 0.2) * 0.7, alert: false };
}

export function summarize(session) {
  const mean = key => {
    const values = session.samples.map(x => x[key]).filter(x => typeof x === 'number' && Number.isFinite(x));
    return values.length ? values.reduce((a, b) => a + b, 0) / values.length : null;
  };
  return { cadence: mean('cadence'), strideTime: mean('strideTime'), impact: mean('impact'), variability: mean('variability') };
}

/** Tracks active time, independent of timer throttling and wall-clock changes. */
export function elapsedActive(session, now) {
  return session.elapsedMs + (session.status === 'running' ? Math.max(0, now - session.resumedAt) : 0);
}

/** Subtract counter resets instead of double-counting a cumulative device count. */
export function advanceSteps(previous, counter) {
  if (previous === null) return { previous: counter, delta: 0 };
  return { previous: counter, delta: counter >= previous ? counter - previous : 0 };
}

export const formatDuration = seconds => {
  const s = Math.max(0, Math.floor(seconds));
  return `${String(Math.floor(s / 60)).padStart(2, '0')}:${String(s % 60).padStart(2, '0')}`;
};
