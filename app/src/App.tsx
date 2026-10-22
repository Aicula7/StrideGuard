import { useEffect, useRef, useState } from 'react';
import { Activity, ArrowDownToLine, ArrowLeft, ArrowRight, Bell, Check, CheckCircle2, ChevronRight, CircleHelp, Clock3, Footprints, History, Home, Info, Laptop, Link2, MoreHorizontal, Pause, Play, Plus, Radio, Settings2, ShieldCheck, Square, Trash2, Unplug, Usb, UserRound, Watch, Waves, X } from 'lucide-react';
import { advanceSteps, createLineBuffer, demoSample, elapsedActive, formatDuration, parseMetricLine, summarize } from './core.js';

type Page = 'home' | 'devices' | 'history' | 'profile' | 'live' | 'summary';
type Source = 'demo' | 'usb';
type Metric = { cadence?: number; strideTime?: number; impact?: number; variability?: number; steps?: number; alert: boolean };
type Sample = Metric & { t: number };
type Event = { t: number; reason: string };
type Session = { id: string; startedAt: string; activity: string; source: Source; status: 'running' | 'paused' | 'ended'; elapsedMs: number; resumedAt: number; duration: number; steps: number; hasSteps: boolean; samples: Sample[]; events: Event[] };
type Port = { readable: ReadableStream<Uint8Array> | null; open(options: {baudRate:number}): Promise<void>; close():Promise<void>; };
type NavigatorSerial = Navigator & { serial?: { requestPort(): Promise<Port> } };
type Preferences = { name: string; alerts: boolean; threshold: number };

const HISTORY_KEY = 'strideguard.sessions.v1';
const PREFS_KEY = 'strideguard.preferences.v1';
const nav = [{id:'home',label:'Home',icon:Home}, {id:'devices',label:'Devices',icon:Watch}, {id:'history',label:'History',icon:History}, {id:'profile',label:'Me',icon:UserRound}] as const;
const number = (v: number | undefined | null, digits = 0) => v == null ? '—' : v.toLocaleString('en-US', {maximumFractionDigits:digits,minimumFractionDigits:digits});
const shortDate = (date: string) => new Date(date).toLocaleDateString('en-US', { timeZone:'America/New_York',month:'short',day:'numeric' });
function getSaved(): Session[] {
  try { const value=JSON.parse(localStorage.getItem(HISTORY_KEY)||'[]'); return Array.isArray(value) ? value.filter(s=>typeof s.id==='string' && Number.isFinite(s.duration) && s.duration>=0 && Array.isArray(s.samples) && Array.isArray(s.events) && ['demo','usb'].includes(s.source)).slice(0,30) : []; } catch {return [];}
}
function getPrefs():Preferences {
  try {const p=JSON.parse(localStorage.getItem(PREFS_KEY)||'{}'); return {name:typeof p.name==='string'&&p.name.trim()?p.name.slice(0,30):'Ming',alerts:p.alerts!==false,threshold:Number.isFinite(p.threshold)&&p.threshold>=3&&p.threshold<=40?p.threshold:10};} catch {return {name:'Ming',alerts:true,threshold:10};}
}
function download(data:string, name:string, mime='application/json') {
  const a=document.createElement('a'); const url=URL.createObjectURL(new Blob([data],{type:mime})); a.href=url;a.download=name;a.click();setTimeout(()=>URL.revokeObjectURL(url),1000);
}

function RhythmChart({samples, empty='Start a session to see your rhythm', compact=false, metric='cadence'}:{samples:Sample[];empty?:string;compact?:boolean;metric?:'cadence'|'strideTime'}) {
  const data=samples.filter(s=>s[metric]!==undefined).slice(-90);
  const w=720,h=compact?100:210,pad=compact?8:28;
  const stride=metric==='strideTime';
  const low=stride?Math.floor(Math.min(.8,...data.map(s=>s[metric]!))*10)/10:Math.floor(Math.min(90,...data.map(s=>s[metric]!))/20)*20;
  const high=stride?Math.ceil(Math.max(1.3,...data.map(s=>s[metric]!))*10)/10:Math.ceil(Math.max(130,...data.map(s=>s[metric]!))/20)*20;
  const first=data[0]?.t??0,last=data.at(-1)?.t??1;
  const points=data.map(s=>`${pad+(s.t-first)*(w-pad*2)/Math.max(last-first,1)},${h-pad-(s[metric]!-low)/(high-low)*(h-pad*2)}`).join(' ');
  return <div className={`chart ${compact?'compact':''}`}>
    <svg viewBox={`0 0 ${w} ${h}`} role="img" aria-label={data.length?`${stride?'Stride time':'Cadence'} over the last ${data.length} samples`:`${stride?'Stride time':'Cadence'} chart, no data yet`}>
      <defs><linearGradient id={compact?'fill-small':'fill-large'} x1="0" y1="0" x2="0" y2="1"><stop stopColor="#6965ed" stopOpacity=".20"/><stop offset="1" stopColor="#6965ed" stopOpacity="0"/></linearGradient></defs>
      {!compact&&[0,.5,1].map(t=><g key={t}><line x1={pad} x2={w-pad} y1={pad+t*(h-pad*2)} y2={pad+t*(h-pad*2)} stroke="#e9eceb" strokeDasharray="4 6"/><text x={pad} y={pad+t*(h-pad*2)-7} fill="#788578" fontSize="12">{number(high-t*(high-low),stride?2:0)}</text></g>)}
      {data.length>1&&<><polygon points={`${pad},${h-pad} ${points} ${w-pad},${h-pad}`} fill={`url(#${compact?'fill-small':'fill-large'})`}/><polyline points={points} fill="none" stroke="#6661ed" strokeWidth={compact?2:3} strokeLinecap="round" strokeLinejoin="round"/></>}
      {data.length===1&&<circle cx={pad} cy={h-pad-(data[0][metric]!-low)/(high-low)*(h-pad*2)} r="4" fill="#6661ed"/>}
    </svg>
    {!data.length&&<span className="chart-empty"><Waves size={24}/>{empty}</span>}
    {!compact&&<div className="chart-axis"><span>{data.length?`${number(data[0].t)} s`:'Earlier'}</span><span>{data.length?`${number(data[data.length-1].t)} s`:'Now'}</span></div>}
  </div>;
}

export default function App() {
  const [page,setPage]=useState<Page>('home');
  const [source,setSource]=useState<Source>('demo');
  const [prefs,setPrefs]=useState(getPrefs);
  const [saved,setSaved]=useState<Session[]>(getSaved);
  const [activity,setActivity]=useState('Walking');
  const [session,setSession]=useState<Session|null>(null);
  const [selected,setSelected]=useState<Session|null>(null);
  const [metrics,setMetrics]=useState<Metric>({alert:false});
  const [connected,setConnected]=useState(false);
  const [connecting,setConnecting]=useState(false);
  const [baud,setBaud]=useState(115200);
  const [packets,setPackets]=useState(0);
  const [invalid,setInvalid]=useState(0);
  const [lastPacket,setLastPacket]=useState(0);
  const [now,setNow]=useState(performance.now());
  const [scenario,setScenario]=useState(false);
  const [graphMetric,setGraphMetric]=useState<'cadence'|'strideTime'>('cadence');
  const [notice,setNotice]=useState('');
  const [alertOpen,setAlertOpen]=useState(false);
  const [help,setHelp]=useState(false);
  const [confirmDelete,setConfirmDelete]=useState<string|null>(null);
  const [toast,setToast]=useState('');
  const ref=useRef<Session|null>(null);
  const prefsRef=useRef(prefs);prefsRef.current=prefs;
  const portRef=useRef<Port|null>(null);
  const readerRef=useRef<ReadableStreamDefaultReader<Uint8Array>|null>(null);
  const disconnecting=useRef(false);
  const stepCounter=useRef<number|null>(null);
  const demoStepFraction=useRef(0);
  const lastHardwareStore=useRef(-Infinity);
  const lastInputAt=useRef(0);
  const highSince=useRef<number|null>(null);
  const lastAlertAt=useRef(-Infinity);
  const hardwareAlertWasOn=useRef(false);
  const totalSeconds=saved.reduce((a,s)=>a+s.duration,0);
  const last=saved[0];
  const active=!!session&&session.status!=='ended';
  const elapsed=session?elapsedActive(session,now)/1000:0;
  const stale=source==='usb'&&(!connected||!lastPacket||now-lastPacket>3000);
  const freshMetrics=source==='demo'||!stale;
  const current=selected||(session?.status==='ended'?session:null);
  const mean=current?summarize(current):null;

  function updateSession(next:Session|null){ref.current=next;setSession(next);}
  function notify(text:string){setToast(text);}
  useEffect(()=>{const id=setInterval(()=>setNow(performance.now()),250);return()=>clearInterval(id);},[]);
  useEffect(()=>{if(!toast)return;const id=setTimeout(()=>setToast(''),4500);return()=>clearTimeout(id);},[toast]);
  useEffect(()=>{try{localStorage.setItem(PREFS_KEY,JSON.stringify(prefs));}catch{/* the app remains usable without storage */}},[prefs]);
  useEffect(()=>()=>{disconnecting.current=true;void readerRef.current?.cancel().catch(()=>{});},[]);
  useEffect(()=>{
    if(!help&&!alertOpen&&!confirmDelete)return;
    const previous=document.activeElement as HTMLElement|null;
    const onKey=(e:KeyboardEvent)=>{
      if(e.key==='Escape'){setHelp(false);setAlertOpen(false);setConfirmDelete(null);}
      if(e.key==='Tab'){
        const modal=document.querySelector('.modal');
        const controls=modal?.querySelectorAll<HTMLElement>('button:not(:disabled),input:not(:disabled),select,a[href]');
        if(!controls?.length)return;
        const first=controls[0],last=controls[controls.length-1];
        if(e.shiftKey&&(document.activeElement===first||!modal?.contains(document.activeElement))){e.preventDefault();last.focus();}
        else if(!e.shiftKey&&(document.activeElement===last||!modal?.contains(document.activeElement))){e.preventDefault();first.focus();}
      }
    };
    document.addEventListener('keydown',onKey);const overflow=document.body.style.overflow;document.body.style.overflow='hidden';
    return()=>{document.removeEventListener('keydown',onKey);document.body.style.overflow=overflow;previous?.focus();};
  },[help,alertOpen,confirmDelete]);

  function ingest(metric:Metric, kind:Source) {
    const time=performance.now();setMetrics(metric);setLastPacket(time);
    if(kind==='usb')setPackets(n=>n+1);
    const s=ref.current;
    if(!s||s.source!==kind)return;
    if(s.status!=='running'){if(s.status==='paused'&&kind==='usb'&&metric.steps!==undefined)stepCounter.current=metric.steps;return;}
    if(time-lastInputAt.current>3000)highSince.current=null;
    lastInputAt.current=time;
    const t=elapsedActive(s,time)/1000;
    let delta=0; let hasSteps=s.hasSteps;
    if(kind==='demo'&&metric.cadence!==undefined){const seconds=Math.max(0,t-(s.samples.at(-1)?.t??0));demoStepFraction.current+=metric.cadence*seconds/60;delta=Math.floor(demoStepFraction.current);demoStepFraction.current-=delta;hasSteps=true;}
    if(kind==='usb'&&metric.steps!==undefined){const result=advanceSteps(stepCounter.current,metric.steps);stepCounter.current=result.previous;delta=result.delta;hasSteps=true;}
    // Store at most one hardware sample per second; preserve step deltas at any input rate.
    const storeSample=kind==='demo'||time-lastHardwareStore.current>=1000;
    if(kind==='usb'&&storeSample)lastHardwareStore.current=time;
    let next={...s,steps:s.steps+delta,hasSteps,samples:storeSample?[...s.samples,{...metric,t}].slice(-7200):s.samples};
    const above=metric.variability!==undefined&&metric.variability>prefsRef.current.threshold;
    if(above){if(highSince.current===null)highSince.current=time;}else highSince.current=null;
    const thresholdAlert=highSince.current!==null&&time-highSince.current>=5000;
    const deviceAlert=metric.alert&&!hardwareAlertWasOn.current;
    hardwareAlertWasOn.current=metric.alert;
    if((thresholdAlert||deviceAlert)&&time-lastAlertAt.current>=30000){
      lastAlertAt.current=time;
      next={...next,events:[...next.events,{t,reason:deviceAlert?'Sensor reported a timing alert.':`Timing variability remained above ${prefsRef.current.threshold}% for 5 seconds.`}]};
      if(prefsRef.current.alerts)setAlertOpen(true);
    }
    updateSession(next);
  }
  const ingestRef=useRef(ingest);ingestRef.current=ingest;
  useEffect(()=>{
    if(!session||session.status!=='running'||session.source!=='demo')return;
    const id=setInterval(()=>{const s=ref.current;if(s?.status==='running')ingestRef.current(demoSample(elapsedActive(s,performance.now())/1000,scenario),'demo');},1000);
    return()=>clearInterval(id);
  },[session?.id,session?.status,session?.source,scenario]);

  function startSession(){
    if(active){setPage('live');return;}
    if(source==='usb'&&(!connected||stale)){setNotice('Connect your foot sensor and wait for a valid data packet before starting.');setPage('devices');return;}
    const id=globalThis.crypto?.randomUUID?.()??`session-${Date.now()}-${Math.random().toString(36).slice(2)}`;
    const s:Session={id,startedAt:new Date().toISOString(),activity,source,status:'running',elapsedMs:0,resumedAt:performance.now(),duration:0,steps:0,hasSteps:source==='demo',samples:[],events:[]};
    stepCounter.current=source==='usb'?(metrics.steps??null):null;demoStepFraction.current=0;lastHardwareStore.current=-Infinity;lastInputAt.current=0;highSince.current=null;lastAlertAt.current=-Infinity;hardwareAlertWasOn.current=false;
    updateSession(s);setSelected(null);setMetrics({alert:false});setScenario(false);setAlertOpen(false);setPage('live');
  }
  function pauseSession(){const s=ref.current;if(!s||s.status==='ended')return;const time=performance.now();updateSession({...s,status:s.status==='running'?'paused':'running',elapsedMs:elapsedActive(s,time),resumedAt:time});highSince.current=null;setAlertOpen(false);}
  function endSession(){const s=ref.current;if(!s||s.status==='ended')return;const ms=elapsedActive(s,performance.now());const ended:Session={...s,status:'ended',elapsedMs:ms,duration:ms/1000};updateSession(ended);setSelected(ended);setAlertOpen(false);setPage('summary');}
  function persistHistory(next:Session[]){try{localStorage.setItem(HISTORY_KEY,JSON.stringify(next));setSaved(next);return true;}catch{notify('Browser storage is full or unavailable. Export this session before leaving.');return false;}}
  function saveSession(){if(!current)return;if(!saved.some(s=>s.id===current.id)){if(!persistHistory([current,...saved].slice(0,30)))return;}notify('Session saved in this browser.');setPage('history');}
  function deleteSession(id:string){if(persistHistory(saved.filter(s=>s.id!==id))){setConfirmDelete(null);notify('Session deleted.');}}
  function changeSource(next:Source){if(active){notify('End your current session before changing the data source.');return;}setSource(next);setMetrics({alert:false});setLastPacket(0);setNotice('');}
  async function connectSerial(){
    if(connecting)return;
    const api=(navigator as NavigatorSerial).serial;
    if(!api){setNotice('USB serial is unavailable here. Open this page in a standalone Chrome or Edge window on Windows, using HTTPS or localhost.');return;}
    let port:Port|null=null;
    try{
      setConnecting(true);setNotice('');disconnecting.current=false;
      port=await api.requestPort();await port.open({baudRate:baud});portRef.current=port;setConnected(true);setMetrics({alert:false});setLastPacket(0);setPackets(0);setInvalid(0);notify('Port opened. Waiting for gait data…');
      if(!port.readable)throw new Error('The selected port has no readable stream.');
      const reader=port.readable.getReader();readerRef.current=reader;
      const decoder=new TextDecoder();const buffer=createLineBuffer((line:string)=>{const metric=parseMetricLine(line);if(metric)ingestRef.current(metric as Metric,'usb');else setInvalid(n=>n+1);});
      setConnecting(false);
      try{while(true){const {value,done}=await reader.read();if(done)break;if(value)buffer.push(decoder.decode(value,{stream:true}));}}finally{reader.releaseLock();readerRef.current=null;}
      if(!disconnecting.current)setNotice('The sensor connection ended. Reconnect in Devices to resume receiving data.');
    }catch(error){const e=error as Error;if(e.name!=='NotFoundError'&&!disconnecting.current)setNotice(`${e.message} Close Arduino Serial Monitor before connecting.`);}
    finally{if(port)try{await port.close();}catch{}portRef.current=null;setConnecting(false);setConnected(false);}
  }
  async function disconnectSerial(){disconnecting.current=true;try{await readerRef.current?.cancel();}catch{}setConnected(false);setLastPacket(0);notify('Sensor disconnected.');}
  function exportSession(s:Session){download(JSON.stringify(s,null,2),`strideguard-${s.startedAt.slice(0,10)}-${s.id.slice(0,8)}.json`);}
  function exportCSV(s:Session){const rows=['active_seconds,cadence_spm,stride_time_s,impact_g,variability_pct,device_step_count',...s.samples.map(x=>[x.t.toFixed(2),x.cadence??'',x.strideTime??'',x.impact??'',x.variability??'',x.steps??''].join(','))];download(rows.join('\r\n'),`strideguard-${s.id.slice(0,8)}.csv`,'text/csv');}
  function viewRecord(s:Session){setSelected(s);setPage('summary');}
  const go=(p:Page)=>{setPage(p==='home'&&active?'live':p);};
  const initials=prefs.name.split(/\s+/).map(x=>x[0]).join('').slice(0,2).toUpperCase();

  return <div className="app-shell">
    <aside className="sidebar">
      <a className="brand" href="#home" onClick={e=>{e.preventDefault();go('home');}}><span className="brand-symbol"><Activity size={23}/></span><span>StrideGuard<small>FIND YOUR RHYTHM</small></span></a>
      <div className="nav-label">YOUR WORKSPACE</div>
      <nav aria-label="Main navigation">{nav.map(n=><button key={n.id} className={`nav-item ${(n.id==='home'&&['home','live','summary'].includes(page))||page===n.id?'active':''}`} onClick={()=>go(n.id)} aria-current={page===n.id?'page':undefined}><n.icon size={19}/>{n.label}{n.id==='history'&&saved.length>0&&<span className="nav-count">{saved.length}</span>}</button>)}</nav>
      <div className="sidebar-bottom"><div className="sidebar-note"><span className="little-spark">✦</span><strong>A little awareness.<br/>A better rhythm.</strong><p>Make space to notice how you move.</p></div><button className="help-link" onClick={()=>setHelp(true)}><CircleHelp size={17}/> Help & setup <ChevronRight size={15}/></button><div className="sidebar-profile"><span className="avatar small">{initials}</span><div>{prefs.name}<small>Personal workspace</small></div><button aria-label="Open preferences" onClick={()=>setPage('profile')}><Settings2 size={17}/></button></div></div>
    </aside>
    <div className="workspace">
      <header className="topbar"><div className="mobile-brand"><Activity size={22}/> StrideGuard</div><div className="breadcrumb">Workspace <ChevronRight size={13}/><span>{page==='live'?'Live session':page==='summary'?'Session summary':nav.find(n=>n.id===page)?.label}</span></div><div className="topbar-right"><span className={`source-badge ${source==='demo'?'demo':'real'}`}><span className="status-dot"/>{source==='demo'?'Demo mode':connected?'USB connected':'USB disconnected'}</span><button className="icon-button" aria-label="Help and setup" onClick={()=>setHelp(true)}><CircleHelp size={19}/></button><button className="avatar" aria-label="Open your profile" onClick={()=>setPage('profile')}>{initials}</button></div></header>
      <main>
        {page==='home'&&<>
          <div className="page-heading"><div><p className="eyebrow">{new Date().toLocaleDateString('en-US',{timeZone:'America/New_York',weekday:'long',month:'long',day:'numeric'})}</p><h1>Ready to move, {prefs.name}?</h1><p>A little insight into every step.</p></div><span className="heading-mark"><Footprints size={30}/></span></div>
          <div className="home-layout"><section className="start-card card"><div className="orb"><Footprints size={45}/><i/></div><p className="eyebrow">START A SESSION</p><h2>Find your walking rhythm.</h2><p className="start-description">Wear your foot sensor and wristband.<br/>We'll help you notice when your rhythm changes.</p>
            <div className="start-controls"><label className="field soft">ACTIVITY<select value={activity} onChange={e=>setActivity(e.target.value)}><option>Walking</option><option>Running</option></select></label><div className="mini-devices"><div><span className="mini-icon"><Footprints size={20}/></span><span><strong>Foot sensor</strong><small>{source==='demo'?'Simulated data':connected?(stale?'Waiting for data':'Receiving data'):'Not connected'}</small></span><span className={`connection-light ${source==='demo'?'simulated':connected&&!stale?'on':''}`}/></div><div><span className="mini-icon"><Watch size={20}/></span><span><strong>StrideBand</strong><small>{source==='demo'?'Demo preview only':'Not configured'}</small></span><span className="connection-light"/></div></div>
            <button className="button primary full" onClick={startSession}><Play size={17}/>{source==='demo'?'Start demo session':'Start session'}<ArrowRight size={17}/></button><button className="text-button" onClick={()=>setPage('devices')}><Link2 size={14}/> {source==='demo'?'Connect a real foot sensor':'Manage devices'}</button></div>
            <div className="demo-disclosure"><Info size={14}/>{source==='demo'?'Demo uses simulated gait data. No device is connected.':'USB mode uses processed metrics sent by your sensor.'}</div>
          </section><div className="home-side"><section className="card glance-card"><div className="section-title"><h3>Your movement, at a glance</h3><span className="mini-icon mint"><Activity size={18}/></span></div><p className="muted small-text">Saved in this browser</p><div className="glance-numbers"><div><strong>{saved.length}</strong><span>sessions</span></div><div><strong>{number(totalSeconds/60)}</strong><span>active minutes</span></div></div><div className="weekly-chart" aria-label="Saved session duration by day">{Array.from({length:7},(_,i)=>{const day=new Date();day.setDate(day.getDate()-6+i);const secs=saved.filter(s=>new Date(s.startedAt).toDateString()===day.toDateString()).reduce((a,s)=>a+s.duration,0);const max=Math.max(60,...Array.from({length:7},(_,j)=>{const d=new Date();d.setDate(d.getDate()-6+j);return saved.filter(s=>new Date(s.startedAt).toDateString()===d.toDateString()).reduce((a,s)=>a+s.duration,0);}));return <div key={i}><div className="bar-track"><i style={{height:secs?`${Math.max(5,secs/max*100)}%`:'3px'}} className={i===6?'today':''} title={`${number(secs/60,1)} minutes`}/></div><span>{day.toLocaleDateString('en-US',{weekday:'narrow'})}</span></div>;})}</div></section>
            <button className="last-session" onClick={()=>last?viewRecord(last):setPage('history')}><span className="mini-icon mint"><History size={21}/></span><div><p className="eyebrow">{last?'LAST SESSION':'YOUR NEXT CHAPTER'}</p><strong>{last?`${last.activity} · ${shortDate(last.startedAt)}`:'Your first session starts here.'}</strong><small>{last?`${formatDuration(last.duration)} · ${last.source==='demo'?'Demo':'USB sensor'} · ${last.events.length} timing alerts`:'Try a demo, then make it your own.'}</small></div><ChevronRight size={20}/></button>
            <section className="rhythm-note"><span className="note-symbol"><Waves size={23}/></span><h3>Small steps. More awareness.</h3><p>StrideGuard follows your cadence and stride timing, so you can get to know your own rhythm.</p><button className="text-button" onClick={()=>setHelp(true)}>How it works <ArrowRight size={14}/></button></section></div></div>
        </>}

        {page==='live'&&session&&<>
          <div className="page-heading"><div><p className="eyebrow">{session.source==='demo'?'SIMULATED SESSION':'SENSOR SESSION'}</p><h1>{session.activity} session <span className={`live-indicator ${session.status==='paused'?'paused':''}`}>{session.status==='paused'?'Paused':'Live'}</span></h1><p>{session.source==='demo'?'Explore the flow with simulated gait data.':'Metrics received from your foot sensor.'}</p></div><div className="session-clock"><Clock3 size={19}/><strong>{formatDuration(elapsed)}</strong><small>active time</small></div></div>
          {session.source==='usb'&&stale&&<div className="notice warning"><Radio size={18}/><span>Sensor data is unavailable or more than 3 seconds old. Live values are hidden until new data arrives.</span><button onClick={()=>setPage('devices')}>Devices <ArrowRight size={15}/></button></div>}
          <div className="live-layout"><div><section className="card cadence-card"><div className="section-title"><div><p className="eyebrow">YOUR RHYTHM</p><h3>{graphMetric==='cadence'?'Cadence':'Stride time'}</h3></div><div className="graph-tabs" aria-label="Graph metric"><button aria-pressed={graphMetric==='cadence'} onClick={()=>setGraphMetric('cadence')}>Cadence</button><button aria-pressed={graphMetric==='strideTime'} onClick={()=>setGraphMetric('strideTime')}>Stride time</button></div></div><div className="cadence-value">{freshMetrics?number(metrics[graphMetric],graphMetric==='strideTime'?2:0):'—'}<span>{graphMetric==='cadence'?'steps/min':'seconds'}</span></div><div className="rhythm-status"><span className={`status-dot ${(metrics.variability??0)>prefs.threshold?'amber':''}`}/>{session.status==='paused'?'Recording paused':!freshMetrics?'Waiting for sensor data':metrics.variability===undefined?'Waiting for timing data':metrics.variability>prefs.threshold?'Timing variability increased':'Rhythm is steady'}</div><RhythmChart samples={session.samples} metric={graphMetric} empty="Waiting for the first gait sample…"/></section><div className="metric-grid"><MetricCard icon={<Clock3 size={18}/>} label="Stride time" value={freshMetrics?number(metrics.strideTime,2):'—'} unit="seconds" hint="Time between strikes of the same foot"/><MetricCard icon={<Waves size={18}/>} label="Timing variability" value={freshMetrics?number(metrics.variability,1):'—'} unit="%" hint={`Your selected alert threshold: ${prefs.threshold}%`}/><MetricCard icon={<Footprints size={18}/>} label="Impact" value={freshMetrics?number(metrics.impact,2):'—'} unit="g" hint="Sensor-reported impact magnitude"/></div></div>
          <aside className="live-side"><section className="card progress-card"><div className="section-title"><h3>Session so far</h3><Activity size={18}/></div><div className="progress-number">{session.hasSteps?number(session.steps):'—'}<span>{session.source==='demo'?'estimated steps':'recorded steps'}</span></div><div className="detail-line"><span>Activity</span><strong>{session.activity}</strong></div><div className="detail-line"><span>Timing alerts</span><strong>{session.events.length}</strong></div><div className="detail-line"><span>Data source</span><strong>{session.source==='demo'?'Simulation':'USB sensor'}</strong></div><button className="button secondary full" onClick={pauseSession}>{session.status==='paused'?<Play size={17}/>:<Pause size={17}/>} {session.status==='paused'?'Resume session':'Pause session'}</button><button className="button end-button full" onClick={endSession}><Square size={15}/> End session</button></section>
          {session.source==='demo'&&<section className="demo-panel"><span className="eyebrow">DEMO CONTROLS</span><h3>Try a change in rhythm.</h3><p>Switch the simulated pattern to see how a timing alert works.</p><label className="switch-row"><span>Irregular timing</span><input type="checkbox" role="switch" checked={scenario} onChange={e=>setScenario(e.target.checked)} disabled={session.status!=='running'}/></label><small>An alert appears after variability stays above your threshold for 5 seconds.</small></section>}
          <div className="gentle-note"><ShieldCheck size={20}/><p>Feedback is for movement awareness. It is not a medical assessment.</p></div></aside></div>
        </>}

        {page==='devices'&&<>
          <div className="page-heading"><div><p className="eyebrow">STAY CONNECTED</p><h1>Your devices</h1><p>Choose where your movement data comes from.</p></div><span className="heading-mark"><Watch size={28}/></span></div>
          <div className="device-layout"><div><section className="card source-card"><h3>Data source</h3><div className="source-options"><button disabled={active||connected} className={source==='demo'?'selected':''} onClick={()=>changeSource('demo')}><span className="mini-icon"><Play size={20}/></span><strong>Demo mode</strong><small>Explore with simulated data</small>{source==='demo'&&<CheckCircle2 size={18}/>}</button><button disabled={active} className={source==='usb'?'selected':''} onClick={()=>changeSource('usb')}><span className="mini-icon"><Usb size={20}/></span><strong>USB sensor</strong><small>Receive real gait metrics</small>{source==='usb'&&<CheckCircle2 size={18}/>}</button></div>{active&&<p className="small-text muted">End your session before changing the data source.</p>}</section>
          <section className="card device-card"><div className="device-card-head"><span className="large-icon"><Footprints size={26}/></span><div><h3>Foot sensor</h3><p>XIAO ESP32-C3 · gait metrics</p></div><span className={`tag ${source==='demo'?'lavender':connected?'green':''}`}>{source==='demo'?'Simulated':connected?'Port open':'Disconnected'}</span></div>{source==='demo'?<div className="device-content"><p>Demo mode generates sample cadence, stride timing and impact values. No foot sensor is connected.</p><button className="button secondary" disabled={active} onClick={()=>changeSource('usb')}><Usb size={16}/> Use a USB sensor</button></div>:<div className="device-content"><label className="field">Baud rate<select value={baud} disabled={connected||connecting} onChange={e=>setBaud(Number(e.target.value))}><option value="115200">115200</option><option value="9600">9600</option><option value="57600">57600</option><option value="230400">230400</option></select></label><p className="small-text muted">Close Arduino Serial Monitor, connect your ESP32 with a data cable, then select its COM port.</p><div className="device-connect-actions">{connected?<button className="button secondary" onClick={disconnectSerial}><Unplug size={16}/>Disconnect</button>:<button className="button primary" disabled={connecting} onClick={()=>void connectSerial()}><Usb size={16}/>{connecting?'Opening port…':'Connect sensor'}</button>}<span className="small-text muted">{packets} valid packets · {invalid} ignored lines</span></div>{connected&&<p className={`receive-status ${stale?'waiting':''}`}><span className="status-dot"/>{stale?'Port open · waiting for valid gait data':'Receiving gait metrics'}</p>}</div>}</section>
          {notice&&<div className="notice warning" role="alert"><Info size={18}/><span>{notice}</span><button aria-label="Dismiss connection message" onClick={()=>setNotice('')}><X size={16}/></button></div>}
          <section className="card device-card"><div className="device-card-head"><span className="large-icon mint"><Watch size={26}/></span><div><h3>StrideBand</h3><p>Wristband feedback</p></div><span className="tag">Not configured</span></div><div className="device-content"><p>The current build displays timing alerts on screen. Wristband control will be added after the team's communication protocol is confirmed.</p></div></section></div>
          <aside><section className="setup-card"><span className="mini-icon mint"><Laptop size={22}/></span><h3>A simple first connection.</h3><ol><li><span>1</span><div><strong>Connect the foot sensor</strong><p>Use a USB data cable on your Windows computer.</p></div></li><li><span>2</span><div><strong>Open the serial port</strong><p>Use Chrome or Edge. Close other apps using the same COM port.</p></div></li><li><span>3</span><div><strong>Check incoming metrics</strong><p>Send one JSON object per line. Start once valid data arrives.</p></div></li></ol><button className="text-button" onClick={()=>setHelp(true)}>View the data format <ArrowRight size={15}/></button></section></aside></div>
        </>}

        {page==='history'&&<>
          <div className="page-heading"><div><p className="eyebrow">A LITTLE LOOK BACK</p><h1>Your session history</h1><p>Your rhythm, one session at a time.</p></div><button className="button secondary" onClick={()=>{if(saved.length)download(JSON.stringify(saved,null,2),'strideguard-history.json');else notify('Save a session before exporting your history.');}}><ArrowDownToLine size={16}/>Export all</button></div>
          <div className="history-totals"><div><span className="mini-icon"><History size={21}/></span><strong>{saved.length}</strong><span>saved sessions</span></div><div><span className="mini-icon mint"><Clock3 size={21}/></span><strong>{number(totalSeconds/60,1)}</strong><span>active minutes</span></div><div><span className="mini-icon peach"><Footprints size={21}/></span><strong>{number(saved.reduce((a,s)=>a+s.steps,0))}</strong><span>steps · includes demo estimates</span></div></div>
          {!saved.length?<section className="card empty-state"><span className="empty-orb"><History size={35}/></span><h2>A fresh start.</h2><p>Finish and save a session to see it here.<br/>Demo sessions will be clearly marked.</p><button className="button primary" onClick={()=>go('home')}><Plus size={17}/>Start your first session</button></section>:<section className="card history-list"><div className="section-title"><h3>Saved sessions</h3><span className="small-text muted">Stored on this device</span></div>{saved.map(s=><div className="history-row" key={s.id}><span className="mini-icon"><Footprints size={21}/></span><button className="record-main" onClick={()=>viewRecord(s)}><strong>{s.activity} session <span className={`tag ${s.source==='demo'?'lavender':'green'}`}>{s.source==='demo'?'Demo':'USB sensor'}</span></strong><small>{new Date(s.startedAt).toLocaleString('en-US',{timeZone:'America/New_York',month:'short',day:'numeric',hour:'numeric',minute:'2-digit'})} · {formatDuration(s.duration)} · {s.events.length} timing alerts</small></button><span className="history-cadence">{number(summarize(s).cadence)}<small>steps/min avg.</small></span><button className="icon-button" aria-label={`Export ${s.activity} session`} onClick={()=>exportSession(s)}><ArrowDownToLine size={18}/></button><button className="icon-button" aria-label={`Delete ${s.activity} session`} onClick={()=>setConfirmDelete(s.id)}><Trash2 size={17}/></button><button className="icon-button" aria-label="View session details" onClick={()=>viewRecord(s)}><ChevronRight size={19}/></button></div>)}</section>}
          <p className="storage-note"><Info size={14}/>History stays in this browser. Export a copy before clearing browser data. The latest 30 saved sessions are kept.</p>
        </>}

        {page==='summary'&&current&&mean&&<>
          <button className="back-link" onClick={()=>setPage('history')}><ArrowLeft size={16}/> Session history</button><div className="page-heading"><div><p className="eyebrow">EVERY STEP COUNTS</p><h1>A moment to reflect.</h1><p>{current.activity} · {new Date(current.startedAt).toLocaleString('en-US',{timeZone:'America/New_York',month:'long',day:'numeric',hour:'numeric',minute:'2-digit'})} <span className={`tag ${current.source==='demo'?'lavender':'green'}`}>{current.source==='demo'?'Demo session':'USB sensor session'}</span></p></div><span className="heading-mark mint"><Check size={30}/></span></div>
          <div className="summary-layout"><section className="card summary-card"><div className="section-title"><h3>Session overview</h3><span className="unit-pill">{formatDuration(current.duration)}</span></div><div className="summary-hero"><Footprints size={26}/><strong>{current.hasSteps?number(current.steps):'—'}</strong><span>{current.source==='demo'?'estimated steps':'recorded steps'}</span></div><div className="summary-metrics"><div><strong>{number(mean.cadence)}</strong><span>avg. cadence · steps/min</span></div><div><strong>{number(mean.strideTime,2)} s</strong><span>avg. stride time</span></div><div><strong>{number(mean.variability,1)}%</strong><span>avg. timing variability</span></div><div><strong>{current.events.length}</strong><span>timing alerts</span></div></div><RhythmChart samples={current.samples}/><div className="button-row"><button className="button primary" onClick={saveSession}><Check size={17}/>{saved.some(s=>s.id===current.id)?'View in history':'Save session'}</button><button className="button secondary" onClick={()=>exportCSV(current)}><ArrowDownToLine size={16}/>Export CSV</button><button className="button secondary" onClick={()=>exportSession(current)}>JSON</button>{!saved.some(s=>s.id===current.id)&&<button className="text-button" onClick={()=>{setSelected(null);updateSession(null);setPage('home');notify('Session discarded.');}}>Don’t save</button>}</div></section><aside className="card events-card"><div className="section-title"><h3>Timing notes</h3><Bell size={18}/></div>{current.events.length?current.events.map((e,i)=><div className="timing-event" key={i}><span className="event-dot"/><div><strong>{formatDuration(e.t)}</strong><p>{e.reason}</p></div></div>):<div className="no-events"><CheckCircle2 size={29}/><strong>No timing alerts recorded.</strong><p>{current.samples.length?'Your recorded values did not trigger a timing alert.':'No metric samples were recorded in this session.'}</p></div>}<p className="small-text muted">Timing alerts describe recorded changes, not health conditions.</p></aside></div>
        </>}

        {page==='profile'&&<>
          <div className="page-heading"><div><p className="eyebrow">MAKE IT YOURS</p><h1>Your space, your rhythm.</h1><p>A few preferences for your StrideGuard experience.</p></div></div><div className="profile-layout"><section className="card profile-card"><div className="profile-banner"><span className="avatar big">{initials}</span><h2>{prefs.name}</h2><p>Personal workspace</p></div><label className="field">Display name<input value={prefs.name} maxLength={30} onChange={e=>setPrefs({...prefs,name:e.target.value})} onBlur={()=>{if(!prefs.name.trim())setPrefs({...prefs,name:'Ming'});}}/></label><p className="small-text muted">Your preferences are saved automatically in this browser.</p></section><section className="card preferences-card"><h3>Feedback preferences</h3><label className="switch-row"><span><strong>On-screen alerts</strong><small>Show a message when a timing change is detected</small></span><input type="checkbox" role="switch" checked={prefs.alerts} onChange={e=>setPrefs({...prefs,alerts:e.target.checked})}/></label><label className="field threshold-field">Timing variability threshold <span>{prefs.threshold}%</span><input type="range" min="3" max="40" step="1" value={prefs.threshold} onChange={e=>setPrefs({...prefs,threshold:Number(e.target.value)})}/></label><p className="small-text muted">An on-screen alert can appear after reported variability stays above this value for 5 seconds. This is a prototype setting, not a validated clinical threshold.</p><div className="preference-divider"/><h3>About this build</h3><p className="muted">StrideGuard · React development version</p><p className="small-text muted">Demo sessions and USB sensor sessions are labeled separately. Wristband control and Bluetooth integration are not included in this build.</p><button className="button secondary" onClick={()=>setHelp(true)}><CircleHelp size={16}/>Help & setup</button></section></div>
        </>}
        <footer className="workspace-footer"><span>StrideGuard</span><span>Move with a little more awareness.</span><span><span className="status-dot"/> {source==='demo'?'Simulated data':'USB sensor mode'}</span></footer>
      </main>
      <nav className="bottom-nav" aria-label="Mobile navigation">{nav.map(n=><button key={n.id} className={page===n.id||(n.id==='home'&&page==='live')?'active':''} onClick={()=>go(n.id)}><n.icon size={20}/><span>{n.label}</span></button>)}</nav>
    </div>
    {active&&page!=='live'&&<button className="resume-banner" onClick={()=>setPage('live')}><span className="status-dot"/>{session?.status==='paused'?'Session paused':'Session in progress'} · {formatDuration(elapsed)}<ArrowRight size={17}/></button>}
    {toast&&<div className="toast" role="status"><CheckCircle2 size={18}/>{toast}<button aria-label="Dismiss notification" onClick={()=>setToast('')}><X size={16}/></button></div>}
    {alertOpen&&<div className="modal-backdrop"><section className="modal alert-modal" role="alertdialog" aria-modal="true" aria-labelledby="alert-title"><span className="alert-orb"><Waves size={32}/></span><p className="eyebrow">A MOMENT TO CHECK IN</p><h2 id="alert-title">Your timing has changed.</h2><p>{ref.current?.events.at(-1)?.reason} Check the sensor fit and how you're moving.</p><div className="alert-detail"><Clock3 size={16}/>{formatDuration(ref.current?.events.at(-1)?.t??elapsed)} into your {session?.activity.toLowerCase()} session</div><button className="button primary full" autoFocus onClick={()=>setAlertOpen(false)}>Got it, continue <ArrowRight size={16}/></button><button className="button secondary full" onClick={pauseSession}><Pause size={16}/>Pause session</button><small>No wristband vibration was sent.</small></section></div>}
    {help&&<div className="modal-backdrop" onClick={()=>setHelp(false)}><section className="modal help-modal" role="dialog" aria-modal="true" aria-labelledby="help-title" onClick={e=>e.stopPropagation()}><button className="modal-close icon-button" aria-label="Close setup guide" onClick={()=>setHelp(false)} autoFocus><X size={20}/></button><span className="mini-icon"><CircleHelp size={22}/></span><h2 id="help-title">Let's get you moving.</h2><p>Start with a demo, or connect your foot sensor using a USB data cable.</p><h3>For a real sensor</h3><p>Open this page in Chrome or Edge on Windows. Close Arduino Serial Monitor, choose USB sensor in Devices, and select the matching COM port.</p><h3>One JSON object per line</h3><pre>{'{"cadence":112,"strideTime":1.07,\n "impact":1.8,"variability":3.2,\n "steps":42,"alert":false}'}</pre><p className="small-text">Units: cadence in steps/min, strideTime in seconds (same-foot cycle), impact in g, variability in %, steps as a cumulative integer counter. Fields may be omitted; missing values show as a dash. Raw IMU readings are not converted into gait metrics by this website.</p><button className="button secondary" onClick={()=>download('{"cadence":112,"strideTime":1.07,"impact":1.8,"variability":3.2,"steps":42,"alert":false}\n','strideguard-example.jsonl','application/x-ndjson')}><ArrowDownToLine size={16}/>Download example data</button><p className="small-text muted">Bluetooth and wristband commands need your team's protocol and are not connected here. Records are saved only in this browser.</p></section></div>}
    {confirmDelete&&<div className="modal-backdrop"><section className="modal" role="alertdialog" aria-modal="true" aria-labelledby="delete-title"><span className="mini-icon peach"><Trash2 size={24}/></span><h2 id="delete-title">Delete this session?</h2><p>This removes the saved record from this browser. Export it first if you want to keep a copy.</p><div className="button-row"><button className="button secondary" autoFocus onClick={()=>setConfirmDelete(null)}>Keep session</button><button className="button danger" onClick={()=>deleteSession(confirmDelete)}>Delete session</button></div></section></div>}
  </div>;
}

function MetricCard({icon,label,value,unit,hint}:{icon:React.ReactNode;label:string;value:string;unit:string;hint:string}){return <section className="card metric-card"><span className="mini-icon">{icon}</span><p>{label}</p><strong>{value}<small>{unit}</small></strong><span>{hint}</span></section>;}
