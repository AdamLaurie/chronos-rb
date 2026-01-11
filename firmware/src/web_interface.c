/**
 * CHRONOS-Rb Web Interface Module
 * 
 * Provides a web-based configuration and monitoring interface.
 * 
 * Copyright (c) 2025 - Open Source Hardware Project
 * License: MIT
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "lwip/tcp.h"

#include "chronos_rb.h"
#include "config.h"
#include "ac_freq_monitor.h"
#include "pulse_output.h"
#include "cli.h"
#include "log_buffer.h"
#include "ota_update.h"
#include "radio_timecode.h"
#include "nmea_output.h"
#include "gnss_input.h"

/*============================================================================
 * HTTP CONSTANTS
 *============================================================================*/

#define HTTP_RESPONSE_HEADER \
    "HTTP/1.1 200 OK\r\n" \
    "Content-Type: text/html; charset=utf-8\r\n" \
    "Connection: close\r\n" \
    "Cache-Control: no-cache\r\n" \
    "\r\n"

#define HTTP_JSON_HEADER \
    "HTTP/1.1 200 OK\r\n" \
    "Content-Type: application/json\r\n" \
    "Connection: close\r\n" \
    "Access-Control-Allow-Origin: *\r\n" \
    "\r\n"

#define HTTP_404_RESPONSE \
    "HTTP/1.1 404 Not Found\r\n" \
    "Content-Type: text/plain\r\n" \
    "Connection: close\r\n" \
    "\r\n" \
    "404 Not Found"

#define HTTP_REDIRECT_RESPONSE \
    "HTTP/1.1 303 See Other\r\n" \
    "Location: /config\r\n" \
    "Connection: close\r\n" \
    "\r\n"

#define HTTP_OK_TEXT \
    "HTTP/1.1 200 OK\r\n" \
    "Content-Type: text/plain\r\n" \
    "Connection: close\r\n" \
    "\r\n"

#define HTTP_400_RESPONSE \
    "HTTP/1.1 400 Bad Request\r\n" \
    "Content-Type: text/plain\r\n" \
    "Connection: close\r\n" \
    "\r\n"

/*============================================================================
 * HTML CONTENT
 *============================================================================*/

static const char HTML_PAGE[] =
"<!DOCTYPE html>"
"<html><head>"
"<title>⚛ CHRONOS-Rb Time Server</title>"
"<link rel='icon' href=\"data:image/svg+xml,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 100 100'><text y='.9em' font-size='90'>⚛</text></svg>\">"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<style>"
"*{box-sizing:border-box;margin:0;padding:0}"
"body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;"
"background:linear-gradient(135deg,#1a1a2e 0%%,#16213e 100%%);color:#eee;min-height:100vh;padding:20px}"
".container{max-width:1200px;margin:0 auto}"
"h1{text-align:center;margin-bottom:8px;font-size:1.8em}"
".logo-ok{color:#4ade80;text-shadow:0 0 20px rgba(74,222,128,0.5)}"
".logo-error{color:#f87171;text-shadow:0 0 20px rgba(248,113,113,0.5)}"
".time-display{text-align:center;font-size:2.5em;font-family:'Courier New',monospace;margin-bottom:15px;min-width:280px}"
".time-valid{color:#4ade80;text-shadow:0 0 20px rgba(74,222,128,0.5)}"
".time-invalid{color:#f87171;text-shadow:0 0 20px rgba(248,113,113,0.5)}"
".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(320px,1fr));gap:15px}"
".card{background:rgba(255,255,255,0.05);border-radius:12px;padding:15px;"
"backdrop-filter:blur(10px);border:1px solid rgba(255,255,255,0.1)}"
".card h2{color:#e94560;margin-bottom:10px;font-size:1.1em}"
".stat{display:flex;justify-content:space-between;padding:6px 0;border-bottom:1px solid rgba(255,255,255,0.05)}"
".stat:last-child{border-bottom:none}"
".stat-label{color:#888;font-size:0.9em}"
".stat-value{font-weight:bold;font-family:'Courier New',monospace;font-size:0.9em;text-align:right}"
".status-locked{color:#4ade80}"
".status-syncing{color:#fbbf24}"
".status-error{color:#f87171}"
".led{display:inline-block;width:8px;height:8px;border-radius:50%%;margin-right:6px}"
".led-green{background:#4ade80;box-shadow:0 0 8px #4ade80}"
".led-yellow{background:#fbbf24;box-shadow:0 0 8px #fbbf24}"
".led-red{background:#f87171;box-shadow:0 0 8px #f87171}"
".nav{text-align:center;margin-bottom:15px}"
".nav a{color:#e94560;text-decoration:none;margin:0 8px;padding:4px 12px;border:1px solid #e94560;border-radius:4px;font-size:0.85em}"
".nav a:hover{background:#e94560;color:#fff}"
"footer{text-align:center;margin-top:20px;color:#555;font-size:0.8em}"
"</style>"
"</head><body>"
"<div class='container'>"
"<h1 class='%s'>&#9883; CHRONOS-Rb</h1>"
"<div id='time' class='time-display %s'>%s</div>"
"<div class='nav'><a href='/'>Status</a> <a href='/acfreq'>AC Freq</a> <a href='/config'>Config</a> <a href='/ota'>OTA</a></div>"
"<div class='grid'>"
"<div class='card'>"
"<h2>System Status</h2>"
"<div class='stat'><span class='stat-label'>Sync State</span>"
"<span class='stat-value %s'><span class='led %s'></span>%s</span></div>"
"<div class='stat'><span class='stat-label'>Rubidium Lock</span>"
"<span class='stat-value'>%s</span></div>"
"<div class='stat'><span class='stat-label'>Time Valid</span>"
"<span class='stat-value'>%s</span></div>"
"<div class='stat'><span class='stat-label'>Uptime</span>"
"<span class='stat-value'>%s</span></div>"
"</div>"
"<div class='card'>"
"<h2>Time Discipline</h2>"
"<div class='stat'><span class='stat-label'>Offset</span>"
"<span class='stat-value'>%lld ns</span></div>"
"<div class='stat'><span class='stat-label'>Freq Offset</span>"
"<span class='stat-value'>%.3f ppb</span></div>"
"<div class='stat'><span class='stat-label'>PPS Count</span>"
"<span class='stat-value'>%lu</span></div>"
"<div class='stat'><span class='stat-label'>Freq Count</span>"
"<span class='stat-value'>%lu Hz</span></div>"
"</div>"
"<div class='card'>"
"<h2>Network</h2>"
"<div class='stat'><span class='stat-label'>IP Address</span>"
"<span class='stat-value'>%s</span></div>"
"<div class='stat'><span class='stat-label'>NTP Server</span>"
"<span class='stat-value'>Port %d (S%d)</span></div>"
"<div class='stat'><span class='stat-label'>NTP Requests</span>"
"<span class='stat-value'>%lu</span></div>"
"<div class='stat'><span class='stat-label'>PTP Syncs</span>"
"<span class='stat-value'>%lu</span></div>"
"</div>"
"<div class='card'>"
"<h2>AC Mains</h2>"
"<div class='stat'><span class='stat-label'>Signal</span>"
"<span class='stat-value'>%s</span></div>"
"<div class='stat'><span class='stat-label'>Frequency</span>"
"<span class='stat-value'>%.3f Hz</span></div>"
"<div class='stat'><span class='stat-label'>Average</span>"
"<span class='stat-value'>%.3f Hz</span></div>"
"<div class='stat'><span class='stat-label'>Range</span>"
"<span class='stat-value'>%.3f - %.3f Hz</span></div>"
"</div>"
"<div class='card'>"
"<h2>RF Outputs</h2>"
"<div class='stat'><span class='stat-label'>DCF77 (77.5kHz)</span>"
"<span class='stat-value'>%s</span></div>"
"<div class='stat'><span class='stat-label'>WWVB (60kHz)</span>"
"<span class='stat-value'>%s</span></div>"
"<div class='stat'><span class='stat-label'>JJY40 (40kHz)</span>"
"<span class='stat-value'>%s</span></div>"
"<div class='stat'><span class='stat-label'>JJY60 (60kHz)</span>"
"<span class='stat-value'>%s</span></div>"
"<div class='stat'><span class='stat-label'>NMEA Serial</span>"
"<span class='stat-value'>%s</span></div>"
"</div>"
"<div class='card'>"
"<h2>GNSS Receiver</h2>"
"<div class='stat'><span class='stat-label'>Status</span>"
"<span class='stat-value'>%s</span></div>"
"<div class='stat'><span class='stat-label'>Fix</span>"
"<span class='stat-value'>%s</span></div>"
"<div class='stat'><span class='stat-label'>Satellites</span>"
"<span class='stat-value'>%d</span></div>"
"<div class='stat'><span class='stat-label'>Position</span>"
"<span class='stat-value'><a href='%s' target='_blank' style='color:#4ade80'>%s</a></span></div>"
"<div class='stat'><span class='stat-label'>GNSS Time</span>"
"<span class='stat-value'>%s</span></div>"
"<div class='stat'><span class='stat-label'>GNSS PPS</span>"
"<span class='stat-value'>%s</span></div>"
"<div class='stat'><span class='stat-label'>PPS Offset</span>"
"<span class='stat-value'>%s</span></div>"
"<div class='stat'><span class='stat-label'>PPS Drift</span>"
"<span class='stat-value'>%s</span></div>"
"<div class='stat'><span class='stat-label'>PPS Jitter</span>"
"<span class='stat-value'>%s</span></div>"
"<div class='stat'><span class='stat-label'>Rb PPS Captures</span>"
"<span class='stat-value'>%lu</span></div>"
"<div class='stat'><span class='stat-label'>GNSS PPS Captures</span>"
"<span class='stat-value'>%lu</span></div>"
"<div class='stat'><span class='stat-label'>GNSS Firmware</span>"
"<span class='stat-value'>%s</span></div>"
"<div class='stat'><span class='stat-label'>GNSS Hardware</span>"
"<span class='stat-value'>%s</span></div>"
"</div>"
"</div>"
"%s"
"<footer>v%s | %s</footer>"
"</div>"
"<script>"
"function updateTime(){"
"fetch('/api/time').then(r=>r.json()).then(d=>{"
"document.getElementById('time').textContent=d.time;"
"document.getElementById('time').className='time-display '+(d.valid?'time-valid':'time-invalid');"
"}).catch(()=>{});}"
"setInterval(updateTime,500);"
"setTimeout(function(){location.reload()},30000);"
"</script>"
"</body></html>";

static const char CONFIG_PAGE[] =
"<!DOCTYPE html>"
"<html><head>"
"<title>⚛ CHRONOS-Rb Configuration</title>"
"<link rel='icon' href=\"data:image/svg+xml,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 100 100'><text y='.9em' font-size='90'>⚛</text></svg>\">"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<style>"
"*{box-sizing:border-box;margin:0;padding:0}"
"body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;"
"background:linear-gradient(135deg,#1a1a2e 0%%,#16213e 100%%);color:#eee;min-height:100vh;padding:20px}"
".container{max-width:600px;margin:0 auto}"
"h1{text-align:center;margin-bottom:30px;font-size:1.8em;"
"background:linear-gradient(90deg,#e94560,#0f3460);-webkit-background-clip:text;"
"-webkit-text-fill-color:transparent}"
".card{background:rgba(255,255,255,0.05);border-radius:15px;padding:20px;margin-bottom:20px;"
"backdrop-filter:blur(10px);border:1px solid rgba(255,255,255,0.1)}"
".card h2{color:#e94560;margin-bottom:15px;font-size:1.1em}"
"label{display:block;color:#aaa;margin-bottom:5px;font-size:0.9em}"
"input[type=text],input[type=password]{width:100%%;padding:10px;margin-bottom:15px;"
"background:rgba(255,255,255,0.1);border:1px solid rgba(255,255,255,0.2);border-radius:8px;"
"color:#fff;font-size:1em}"
"input[type=text]:focus,input[type=password]:focus{outline:none;border-color:#e94560}"
".checkbox-row{display:flex;align-items:center;margin-bottom:15px}"
".checkbox-row input{margin-right:10px;width:18px;height:18px}"
".checkbox-row label{margin-bottom:0}"
"button{background:linear-gradient(90deg,#e94560,#0f3460);color:#fff;border:none;"
"padding:12px 30px;border-radius:8px;cursor:pointer;font-size:1em;width:100%%}"
"button:hover{opacity:0.9}"
".nav{text-align:center;margin-bottom:20px}"
".nav a{color:#e94560;text-decoration:none;margin:0 15px}"
".msg{padding:10px;border-radius:8px;margin-bottom:15px;text-align:center}"
".msg-ok{background:rgba(74,222,128,0.2);color:#4ade80}"
".msg-err{background:rgba(248,113,113,0.2);color:#f87171}"
".stat{display:flex;justify-content:space-between;padding:8px 0;border-bottom:1px solid rgba(255,255,255,0.1)}"
".stat:last-child{border-bottom:none}"
".stat-label{color:#aaa;font-size:0.9em}"
".stat-value{font-family:'Courier New',monospace;font-size:0.9em}"
".note{color:#888;font-size:0.85em;margin-top:10px}"
".cli-output{background:#0a0a12;border-radius:8px;padding:12px;font-family:'Courier New',monospace;"
"font-size:12px;white-space:pre;height:60em;line-height:1.2em;overflow:auto;margin-bottom:15px;color:#0f0}"
".cli-input input{width:100%%;background:rgba(255,255,255,0.1);border:1px solid rgba(255,255,255,0.2);"
"border-radius:8px;padding:10px;color:#fff;font-family:'Courier New',monospace;font-size:12px;margin-bottom:10px}"
".cli-input button{width:100%%}"
"footer{text-align:center;margin-top:30px;color:#666;font-size:0.9em}"
"</style>"
"</head><body>"
"<div class='container'>"
"<h1>⚙ Configuration</h1>"
"<div class='nav'><a href='/'>← Status</a><a href='/config'>Config</a></div>"
"%s"
"<form method='POST' action='/config'>"
"<div class='card'>"
"<h2>WiFi Settings</h2>"
"<label>SSID</label>"
"<input type='text' name='ssid' value='%s' maxlength='32'>"
"<label>Password</label>"
"<input type='password' name='pass' placeholder='Enter new password' maxlength='64'>"
"<div class='checkbox-row'>"
"<input type='checkbox' name='auto' id='auto' %s>"
"<label for='auto'>Auto-connect on boot</label>"
"</div>"
"</div>"
"<div class='card'>"
"<h2>System Settings</h2>"
"<div class='checkbox-row'>"
"<input type='checkbox' name='debug' id='debug' %s>"
"<label for='debug'>Enable debug output</label>"
"</div>"
"</div>"
"<button type='submit'>Save Configuration</button>"
"</form>"
"%s"
"<div class='card'>"
"<h2>Command Line</h2>"
"<div class='cli-output' id='cli-out'>Type a command and press Run</div>"
"<div class='cli-input'>"
"<input type='text' id='cli-cmd' placeholder='Enter command (e.g., help, status, reboot)' autocomplete='off'>"
"<button onclick='runCmd()'>Run</button>"
"</div>"
"</div>"
"<footer>CHRONOS-Rb v%s</footer>"
"</div>"
"<script>"
"var logPos=0,logOut=document.getElementById('cli-out');"
"function appendLog(txt){"
"if(!txt)return;"
"logOut.textContent+=txt;"
"logOut.scrollTop=logOut.scrollHeight;"
"}"
"function pollLogs(){"
"fetch('/api/logs?pos='+logPos)"
".then(r=>r.json()).then(d=>{"
"logPos=d.pos;appendLog(d.data);"
"}).catch(e=>{});"
"}"
"function runCmd(){"
"var i=document.getElementById('cli-cmd'),cmd=i.value;"
"if(!cmd)return;"
"i.value='';"
"fetch('/api/cli',{method:'POST',body:'cmd='+encodeURIComponent(cmd),"
"headers:{'Content-Type':'application/x-www-form-urlencoded'}})"
".then(r=>r.json()).then(d=>{"
"if(d.ok)appendLog('> '+cmd+'\\n'+d.output+'\\n');else appendLog('Error: '+d.error+'\\n');"
"}).catch(e=>{appendLog('Error: '+e+'\\n');});"
"}"
"document.getElementById('cli-cmd').addEventListener('keypress',function(e){"
"if(e.key==='Enter')runCmd();});"
"setInterval(pollLogs,1000);"
"logOut.textContent='';"
"pollLogs();"
"</script>"
"</body></html>";

/* AC Frequency Graph Page */
static const char AC_GRAPH_PAGE[] =
"<!DOCTYPE html>"
"<html><head>"
"<title>⚛ AC Frequency - CHRONOS-Rb</title>"
"<link rel='icon' href=\"data:image/svg+xml,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 100 100'><text y='.9em' font-size='90'>⚛</text></svg>\">"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<style>"
"*{box-sizing:border-box;margin:0;padding:0}"
"body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;"
"background:linear-gradient(135deg,#1a1a2e 0%%,#16213e 100%%);color:#eee;min-height:100vh;padding:20px}"
".container{max-width:900px;margin:0 auto}"
"h1{text-align:center;margin-bottom:20px;font-size:1.6em}"
".card{background:rgba(255,255,255,0.05);border-radius:15px;padding:20px;margin-bottom:20px;"
"backdrop-filter:blur(10px);border:1px solid rgba(255,255,255,0.1)}"
".card h2{color:#e94560;margin-bottom:10px;font-size:1.1em}"
".nav{text-align:center;margin-bottom:20px}"
".nav a{color:#e94560;text-decoration:none;margin:0 15px}"
".graph{width:100%%;height:200px;background:#0a0a12;border-radius:8px;position:relative}"
".graph svg{width:100%%;height:100%%}"
".axis{stroke:#444;stroke-width:1}"
".grid{stroke:#333;stroke-width:0.5}"
".line{fill:none;stroke:#e94560;stroke-width:2}"
".label{fill:#888;font-size:10px}"
".value{fill:#e94560;font-size:12px}"
".nominal{stroke:#4a4;stroke-width:1;stroke-dasharray:5,5}"
".info{display:flex;justify-content:space-between;margin-top:10px;font-size:0.9em;color:#aaa}"
".time-range{text-align:center;font-size:0.85em;color:#666;margin-top:5px}"
"</style>"
"</head><body>"
"<div class='container'>"
"<h1>⚛ AC Mains Frequency</h1>"
"<div class='nav'>"
"<a href='/'>Status</a>"
"<a href='/acfreq'>AC Freq</a>"
"<a href='/config'>Config</a>"
"<a href='/ota'>OTA</a>"
"</div>"
"<div class='card'>"
"<h2>Last 60 Minutes</h2>"
"<div class='graph' id='min-graph'><svg></svg></div>"
"<div class='info'><span id='min-info'>Loading...</span><span id='min-range'></span></div>"
"<div class='time-range' id='min-time'></div>"
"</div>"
"<div class='card'>"
"<h2>Last 48 Hours</h2>"
"<div class='graph' id='hour-graph'><svg></svg></div>"
"<div class='info'><span id='hour-info'>Loading...</span><span id='hour-range'></span></div>"
"<div class='time-range' id='hour-time'></div>"
"</div>"
"</div>"
"<script>"
"function fmtTime(d){return d.toTimeString().slice(0,5);}"
"function fmtDateShort(d){return d.toISOString().slice(5,10);}"
"function fmtDate(d){return d.toISOString().slice(0,10)+' '+d.toTimeString().slice(0,5);}"
"function drawGraph(id,data,nowMs,intervalMs){"
"if(!data||data.length===0){document.querySelector('#'+id+' svg').innerHTML='<text x=\"50%%\" y=\"50%%\" text-anchor=\"middle\" fill=\"#666\">No data yet</text>';return null;}"
"var svg=document.querySelector('#'+id+' svg');"
"var w=svg.clientWidth||850,h=svg.clientHeight||200;"
"var isHour=intervalMs>=3600000;"
"var pad={t:20,r:20,b:isHour?45:35,l:50};"
"var gw=w-pad.l-pad.r,gh=h-pad.t-pad.b;"
"var min=Math.min(...data),max=Math.max(...data);"
"var range=max-min;if(range<0.1)range=0.1;"
"min-=range*0.1;max+=range*0.1;"
"var nom=data[0]>55?60:50;"
"var html='<g transform=\"translate('+pad.l+','+pad.t+')\">';"
"html+='<line class=\"axis\" x1=\"0\" y1=\"'+gh+'\" x2=\"'+gw+'\" y2=\"'+gh+'\"/>';"
"html+='<line class=\"axis\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"'+gh+'\"/>';"
"for(var i=0;i<=4;i++){var y=gh*i/4;var v=(max-(max-min)*i/4).toFixed(2);"
"html+='<line class=\"grid\" x1=\"0\" y1=\"'+y+'\" x2=\"'+gw+'\" y2=\"'+y+'\"/>';"
"html+='<text class=\"label\" x=\"-5\" y=\"'+(y+4)+'\" text-anchor=\"end\">'+v+'</text>';}"
"var startMs=nowMs-(data.length-1)*intervalMs;"
"var ticks=isHour?6:5;var lastDate='';"
"for(var i=0;i<=ticks;i++){var x=gw*i/ticks;var tMs=startMs+(data.length-1)*intervalMs*i/ticks;"
"var dt=new Date(tMs);html+='<text class=\"label\" x=\"'+x+'\" y=\"'+(gh+12)+'\" text-anchor=\"middle\">'+fmtTime(dt)+'</text>';"
"if(isHour){var ds=fmtDateShort(dt);if(ds!==lastDate){html+='<text class=\"label\" x=\"'+x+'\" y=\"'+(gh+24)+'\" text-anchor=\"middle\">'+ds+'</text>';lastDate=ds;}}}"
"var ny=gh-(nom-min)/(max-min)*gh;"
"if(ny>0&&ny<gh)html+='<line class=\"nominal\" x1=\"0\" y1=\"'+ny+'\" x2=\"'+gw+'\" y2=\"'+ny+'\"/>';"
"var pts='';for(var i=0;i<data.length;i++){var x=gw*i/(data.length-1||1);var y=gh-(data[i]-min)/(max-min)*gh;pts+=(i?'L':'M')+x+','+y;}"
"html+='<path class=\"line\" d=\"'+pts+'\"/>';"
"html+='</g>';svg.innerHTML=html;"
"var startDt=new Date(startMs),endDt=new Date(nowMs);"
"return{min:Math.min(...data).toFixed(3),max:Math.max(...data).toFixed(3),avg:(data.reduce((a,b)=>a+b,0)/data.length).toFixed(3),start:startDt,end:endDt};}"
"function update(){"
"fetch('/api/ac_history').then(r=>r.json()).then(d=>{"
"var nowMs=d.time_unix*1000;"
"var m=drawGraph('min-graph',d.minutes,nowMs,60000);"
"if(m){document.getElementById('min-info').textContent=d.min_count+' samples, avg: '+m.avg+' Hz';"
"document.getElementById('min-range').textContent='Range: '+m.min+' - '+m.max+' Hz';"
"document.getElementById('min-time').textContent=fmtDate(m.start)+' → '+fmtDate(m.end);}"
"var h=drawGraph('hour-graph',d.hours,nowMs,3600000);"
"if(h){document.getElementById('hour-info').textContent=d.hour_count+' samples, avg: '+h.avg+' Hz';"
"document.getElementById('hour-range').textContent='Range: '+h.min+' - '+h.max+' Hz';"
"document.getElementById('hour-time').textContent=fmtDate(h.start)+' → '+fmtDate(h.end);}"
"}).catch(e=>{console.error(e);});}"
"update();setInterval(update,60000);"
"</script>"
"</body></html>";

static const char OTA_PAGE[] =
"<!DOCTYPE html>"
"<html><head>"
"<title>⚛ CHRONOS-Rb OTA Update</title>"
"<link rel='icon' href=\"data:image/svg+xml,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 100 100'><text y='.9em' font-size='90'>⚛</text></svg>\">"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<style>"
"*{box-sizing:border-box;margin:0;padding:0}"
"body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;"
"background:linear-gradient(135deg,#1a1a2e 0%%,#16213e 100%%);color:#eee;min-height:100vh;padding:20px}"
".container{max-width:600px;margin:0 auto}"
"h1{text-align:center;margin-bottom:30px;font-size:1.8em;"
"background:linear-gradient(90deg,#e94560,#0f3460);-webkit-background-clip:text;"
"-webkit-text-fill-color:transparent}"
".card{background:rgba(255,255,255,0.05);border-radius:15px;padding:20px;margin-bottom:20px;"
"backdrop-filter:blur(10px);border:1px solid rgba(255,255,255,0.1)}"
".card h2{color:#e94560;margin-bottom:15px;font-size:1.1em}"
".nav{text-align:center;margin-bottom:20px}"
".nav a{color:#e94560;text-decoration:none;margin:0 15px}"
".stat{display:flex;justify-content:space-between;padding:8px 0;border-bottom:1px solid rgba(255,255,255,0.1)}"
".stat:last-child{border-bottom:none}"
".stat-label{color:#aaa;font-size:0.9em}"
".stat-value{font-family:'Courier New',monospace;font-size:0.9em}"
"input[type=file]{width:100%%;padding:10px;margin-bottom:15px;background:rgba(255,255,255,0.1);"
"border:1px solid rgba(255,255,255,0.2);border-radius:8px;color:#fff}"
"button{background:linear-gradient(90deg,#e94560,#0f3460);color:#fff;border:none;"
"padding:12px 30px;border-radius:8px;cursor:pointer;font-size:1em;width:100%%}"
"button:hover{opacity:0.9}"
"button:disabled{opacity:0.5;cursor:not-allowed}"
".progress{width:100%%;height:20px;background:rgba(255,255,255,0.1);border-radius:10px;overflow:hidden;margin:15px 0}"
".progress-bar{height:100%%;background:linear-gradient(90deg,#4ade80,#22c55e);width:0%%;transition:width 0.3s}"
".msg{padding:10px;border-radius:8px;margin:15px 0;text-align:center}"
".msg-ok{background:rgba(74,222,128,0.2);color:#4ade80}"
".msg-err{background:rgba(248,113,113,0.2);color:#f87171}"
".msg-warn{background:rgba(251,191,36,0.2);color:#fbbf24}"
"#status{font-family:'Courier New',monospace}"
"footer{text-align:center;margin-top:30px;color:#666;font-size:0.9em}"
"</style>"
"</head><body>"
"<div class='container'>"
"<h1>Firmware Update</h1>"
"<div class='nav'><a href='/'>Status</a><a href='/acfreq'>AC Freq</a><a href='/config'>Config</a><a href='/ota'>OTA</a></div>"
"%s"
"<div class='card'>"
"<h2>Current Firmware</h2>"
"<div class='stat'><span class='stat-label'>Version</span><span class='stat-value'>%s</span></div>"
"<div class='stat'><span class='stat-label'>Build Date</span><span class='stat-value'>%s</span></div>"
"<div class='stat'><span class='stat-label'>OTA State</span><span class='stat-value'>%s</span></div>"
"</div>"
"<div class='card'>"
"<h2>Upload Firmware</h2>"
"<p style='color:#aaa;font-size:0.9em;margin-bottom:15px'>Select the encrypted FOTA image file (*_fota_image_encrypted.bin)</p>"
"<input type='file' id='firmware' accept='.bin'>"
"<div class='progress'><div class='progress-bar' id='progressBar'></div></div>"
"<div id='status'></div>"
"<button id='uploadBtn' onclick='uploadFirmware()'>Upload Firmware</button>"
"</div>"
"<div class='card' id='applyCard' style='display:none'>"
"<h2>Apply Update</h2>"
"<p style='color:#aaa;font-size:0.9em;margin-bottom:15px'>Firmware verified successfully. Click to apply and reboot.</p>"
"<button onclick='applyUpdate()' style='background:linear-gradient(90deg,#22c55e,#16a34a)'>Apply & Reboot</button>"
"</div>"
"<footer>CHRONOS-Rb v%s</footer>"
"</div>"
"<script>"
"async function uploadFirmware(){"
"const f=document.getElementById('firmware').files[0];"
"if(!f){alert('Select a file');return;}"
"const btn=document.getElementById('uploadBtn');"
"const bar=document.getElementById('progressBar');"
"const stat=document.getElementById('status');"
"btn.disabled=true;bar.style.width='0%%';"
"stat.textContent='Initializing...';"
"try{"
"let r=await fetch('/api/ota/begin',{method:'POST',headers:{'X-OTA-Size':f.size}});"
"if(!r.ok)throw new Error(await r.text());"
"const chunk=1024;let sent=0;"
"while(sent<f.size){"
"const end=Math.min(sent+chunk,f.size);"
"const blob=f.slice(sent,end);"
"const data=await blob.arrayBuffer();"
"r=await fetch('/api/ota/chunk',{method:'POST',body:new Uint8Array(data),"
"headers:{'Content-Type':'application/octet-stream'}});"
"if(!r.ok)throw new Error(await r.text());"
"sent=end;bar.style.width=(sent*100/f.size)+'%%';"
"stat.textContent='Uploading... '+(sent*100/f.size).toFixed(1)+'%%';}"
"stat.textContent='Validating...';"
"r=await fetch('/api/ota/finish',{method:'POST'});"
"if(!r.ok)throw new Error(await r.text());"
"bar.style.width='100%%';"
"stat.innerHTML='<span class=\"msg msg-ok\">Upload complete! Ready to apply.</span>';"
"document.getElementById('applyCard').style.display='block';"
"}catch(e){stat.innerHTML='<span class=\"msg msg-err\">Error: '+e.message+'</span>';}"
"btn.disabled=false;}"
"async function applyUpdate(){"
"if(!confirm('Apply update and reboot now?'))return;"
"document.getElementById('status').innerHTML='<span class=\"msg msg-warn\">Rebooting...</span>';"
"await fetch('/api/ota/apply',{method:'POST'});"
"setTimeout(()=>location.reload(),5000);}"
"</script>"
"</body></html>";

/*============================================================================
 * PRIVATE VARIABLES
 *============================================================================*/

static struct tcp_pcb *web_pcb = NULL;
static bool web_running = false;

/*============================================================================
 * HTTP HANDLERS
 *============================================================================*/

/* NTP to Unix epoch offset */
#define NTP_UNIX_OFFSET 2208988800UL

/**
 * Format current time as ISO 8601 string
 */
static void format_current_time(char *buf, size_t len) {
    timestamp_t ts = get_current_time();

    /* Protect against underflow if time not yet initialized */
    if (ts.seconds < NTP_UNIX_OFFSET) {
        snprintf(buf, len, "1970-01-01T00:00:00Z");
        return;
    }

    uint32_t unix_time = ts.seconds - NTP_UNIX_OFFSET;

    /* Calculate broken-down time */
    uint32_t days = unix_time / 86400;
    uint32_t remaining = unix_time % 86400;
    int hour = remaining / 3600;
    int min = (remaining % 3600) / 60;
    int sec = remaining % 60;

    /* Calculate year */
    int year = 1970;
    while (days > 365) {
        int days_in_year = ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0) ? 366 : 365;
        if (days < (uint32_t)days_in_year) break;
        days -= days_in_year;
        year++;
    }

    /* Days in each month */
    int days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0) {
        days_in_month[1] = 29;
    }

    int month = 0;
    while (month < 12 && days >= (uint32_t)days_in_month[month]) {
        days -= days_in_month[month];
        month++;
    }

    snprintf(buf, len, "%04d-%02d-%02dT%02d:%02d:%02dZ",
             year, month + 1, (int)days + 1, hour, min, sec);
}

/**
 * Generate pulse outputs HTML section
 */
static int generate_pulse_outputs_html(char *buf, size_t len) {
    const char *mode_names[] = { "Off", "Interval", "Second", "Minute", "Time" };
    int pos = 0;
    int count = 0;

    /* Count active outputs */
    for (int i = 0; i < MAX_PULSE_OUTPUTS; i++) {
        pulse_config_t *p = pulse_output_get(i);
        if (p && p->active) count++;
    }

    if (count == 0) {
        return snprintf(buf, len,
            "<div class='card'><h2>Pulse Outputs</h2>"
            "<div class='stat'><span class='stat-label'>Status</span>"
            "<span class='stat-value'>No outputs configured</span></div></div>");
    }

    pos += snprintf(buf + pos, len - pos,
        "<div class='card'><h2>Pulse Outputs</h2>");

    for (int i = 0; i < MAX_PULSE_OUTPUTS && pos < (int)len - 200; i++) {
        pulse_config_t *p = pulse_output_get(i);
        if (!p || !p->active) continue;

        char config[64];
        switch (p->mode) {
            case PULSE_MODE_INTERVAL:
                snprintf(config, sizeof(config), "every %lus, %ums",
                    (unsigned long)p->interval, p->pulse_width_ms);
                break;
            case PULSE_MODE_SECOND:
                snprintf(config, sizeof(config), "sec %d, %ums x%d",
                    p->trigger_second, p->pulse_width_ms, p->pulse_count);
                break;
            case PULSE_MODE_MINUTE:
                snprintf(config, sizeof(config), "min %d, %ums x%d",
                    p->trigger_minute, p->pulse_width_ms, p->pulse_count);
                break;
            case PULSE_MODE_TIME:
                snprintf(config, sizeof(config), "%02d:%02d, %ums x%d",
                    p->trigger_hour, p->trigger_minute,
                    p->pulse_width_ms, p->pulse_count);
                break;
            default:
                snprintf(config, sizeof(config), "disabled");
        }

        pos += snprintf(buf + pos, len - pos,
            "<div class='stat'><span class='stat-label'>GP%d (%s)</span>"
            "<span class='stat-value'>%s</span></div>",
            p->gpio_pin, mode_names[p->mode], config);
    }

    pos += snprintf(buf + pos, len - pos, "</div>");
    return pos;
}

/**
 * Generate status page HTML
 */
static int generate_status_page(char *buf, size_t len) {
    const char *sync_states[] = {
        "INIT", "FREQ_CAL", "COARSE", "FINE", "LOCKED", "HOLDOVER", "ERROR"
    };

    const char *sync_class = "status-syncing";
    const char *led_class = "led-yellow";

    if (g_time_state.sync_state == SYNC_STATE_LOCKED) {
        sync_class = "status-locked";
        led_class = "led-green";
    } else if (g_time_state.sync_state == SYNC_STATE_ERROR) {
        sync_class = "status-error";
        led_class = "led-red";
    }

    char ip_str[20];
    get_ip_address_str(ip_str, sizeof(ip_str));

    uint8_t stratum = NTP_STRATUM;
    if (g_time_state.sync_state != SYNC_STATE_LOCKED) {
        stratum = (g_time_state.sync_state >= SYNC_STATE_FINE) ? NTP_STRATUM + 1 : 16;
    }

    const ac_freq_state_t *ac = ac_freq_get_state();

    /* Generate pulse outputs section */
    static char pulse_html[512];
    generate_pulse_outputs_html(pulse_html, sizeof(pulse_html));

    /* Get current time as ISO 8601 */
    char time_str[32];
    format_current_time(time_str, sizeof(time_str));
    const char *time_class = g_time_state.time_valid ? "time-valid" : "time-invalid";

    /* Logo: green if everything OK, red otherwise */
    bool all_ok = g_time_state.rb_locked && g_time_state.time_valid &&
                  (g_time_state.sync_state == SYNC_STATE_LOCKED);
    const char *logo_class = all_ok ? "logo-ok" : "logo-error";

    /* GNSS status */
    const char *gnss_fix_str = "None";
    gnss_fix_type_t fix = gnss_get_fix_type();
    if (fix == GNSS_FIX_2D) gnss_fix_str = "2D";
    else if (fix == GNSS_FIX_3D) gnss_fix_str = "3D";

    char gnss_time_str[24] = "N/A";
    if (gnss_has_time()) {
        gnss_time_t gnss_t;
        gnss_get_utc_time(&gnss_t);
        snprintf(gnss_time_str, sizeof(gnss_time_str), "%02d:%02d:%02d",
                 gnss_t.hour, gnss_t.minute, gnss_t.second);
    }

    /* GNSS position with Google Maps link */
    char gnss_pos_str[32] = "N/A";
    char gnss_pos_url[64] = "#";
    if (gnss_has_fix()) {
        double lat, lon, alt;
        gnss_get_position(&lat, &lon, &alt);
        snprintf(gnss_pos_str, sizeof(gnss_pos_str), "Google Maps");
        snprintf(gnss_pos_url, sizeof(gnss_pos_url), "https://maps.google.com/?q=%.6f,%.6f", lat, lon);
    }

    /* Format uptime as days:hours:minutes:seconds */
    char uptime_str[24];
    uint32_t uptime_sec = (uint32_t)(time_us_64() / 1000000);
    uint32_t days = uptime_sec / 86400;
    uint32_t hours = (uptime_sec % 86400) / 3600;
    uint32_t mins = (uptime_sec % 3600) / 60;
    uint32_t secs = uptime_sec % 60;
    if (days > 0) {
        snprintf(uptime_str, sizeof(uptime_str), "%lud %02lu:%02lu:%02lu",
                 (unsigned long)days, (unsigned long)hours, (unsigned long)mins, (unsigned long)secs);
    } else {
        snprintf(uptime_str, sizeof(uptime_str), "%02lu:%02lu:%02lu",
                 (unsigned long)hours, (unsigned long)mins, (unsigned long)secs);
    }

    /* PPS offset, drift and jitter (FE PPS vs GNSS PPS) */
    char pps_offset_str[32] = "N/A";
    char pps_drift_str[32] = "N/A";
    char pps_jitter_str[32] = "N/A";
    if (freq_counter_pps_offset_valid()) {
        int32_t offset = freq_counter_get_pps_offset();
        double drift = freq_counter_get_pps_drift();
        double stddev = freq_counter_get_pps_stddev();
        /* Offset in ticks (100ns units) */
        snprintf(pps_offset_str, sizeof(pps_offset_str), "%+ld ticks", (long)offset);
        /* Drift in ticks/sec, convert to ns/s for display */
        snprintf(pps_drift_str, sizeof(pps_drift_str), "%+.1f ns/s", drift * 100.0);
        /* Jitter (stddev) in ticks, convert to ns */
        snprintf(pps_jitter_str, sizeof(pps_jitter_str), "%.1f ns", stddev * 100.0);
    }

    return snprintf(buf, len, HTML_PAGE,
        logo_class, time_class, time_str,
        sync_class, led_class, sync_states[g_time_state.sync_state],
        g_time_state.rb_locked ? "LOCKED" : "UNLOCKED",
        g_time_state.time_valid ? "YES" : "NO",
        uptime_str,
        (long long)g_time_state.offset_ns,
        (double)g_time_state.frequency_offset,
        (unsigned long)g_time_state.pps_count,
        (unsigned long)g_time_state.last_freq_count,
        ip_str,
        NTP_PORT, (int)stratum,
        (unsigned long)g_stats.ntp_requests,
        (unsigned long)g_stats.ptp_sync_sent,
        ac->signal_present ? "Detected" : "Not detected",
        (double)ac->frequency_hz,
        (double)ac->frequency_avg_hz,
        (double)ac->frequency_min_hz,
        (double)ac->frequency_max_hz,
        radio_timecode_is_enabled(RADIO_DCF77) ? "ON" : "OFF",
        radio_timecode_is_enabled(RADIO_WWVB) ? "ON" : "OFF",
        radio_timecode_is_enabled(RADIO_JJY40) ? "ON" : "OFF",
        radio_timecode_is_enabled(RADIO_JJY60) ? "ON" : "OFF",
        nmea_output_is_enabled() ? "ON" : "OFF",
        gnss_is_enabled() ? "Enabled" : "Disabled",
        gnss_fix_str,
        (int)gnss_get_satellites(),
        gnss_pos_url,
        gnss_pos_str,
        gnss_time_str,
        gnss_pps_valid() ? "Active" : "No signal",
        pps_offset_str,
        pps_drift_str,
        pps_jitter_str,
        (unsigned long)freq_counter_get_fe_pps_count(),
        (unsigned long)freq_counter_get_gnss_pps_count(),
        gnss_get_firmware_version(),
        gnss_get_hardware_version(),
        pulse_html,
        CHRONOS_VERSION_STRING,
        CHRONOS_BUILD_DATE
    );
}

/**
 * Generate pulse outputs JSON array
 */
static int generate_pulse_outputs_json(char *buf, size_t len) {
    const char *mode_names[] = { "disabled", "interval", "second", "minute", "time" };
    int pos = 0;

    pos += snprintf(buf + pos, len - pos, "[");

    bool first = true;
    for (int i = 0; i < MAX_PULSE_OUTPUTS && pos < (int)len - 100; i++) {
        pulse_config_t *p = pulse_output_get(i);
        if (!p || !p->active) continue;

        if (!first) pos += snprintf(buf + pos, len - pos, ",");
        first = false;

        pos += snprintf(buf + pos, len - pos,
            "{\"pin\":%d,\"mode\":\"%s\",\"interval\":%lu,"
            "\"second\":%d,\"minute\":%d,\"hour\":%d,"
            "\"width_ms\":%d,\"count\":%d,\"gap_ms\":%d}",
            p->gpio_pin, mode_names[p->mode],
            (unsigned long)p->interval,
            p->trigger_second, p->trigger_minute, p->trigger_hour,
            p->pulse_width_ms, p->pulse_count, p->pulse_gap_ms);
    }

    pos += snprintf(buf + pos, len - pos, "]");
    return pos;
}

/**
 * Generate JSON status
 */
static int generate_json_status(char *buf, size_t len) {
    char ip_str[20];
    get_ip_address_str(ip_str, sizeof(ip_str));
    const ac_freq_state_t *ac = ac_freq_get_state();

    /* Generate pulse outputs JSON */
    static char pulse_json[512];
    generate_pulse_outputs_json(pulse_json, sizeof(pulse_json));

    /* Get current time as ISO 8601 */
    char time_str[32];
    format_current_time(time_str, sizeof(time_str));

    /* Get uptime */
    uint32_t uptime_sec = to_ms_since_boot(get_absolute_time()) / 1000;

    return snprintf(buf, len,
        "{"
        "\"sync_state\":%d,"
        "\"rb_locked\":%s,"
        "\"time_valid\":%s,"
        "\"current_time\":\"%s\","
        "\"uptime_sec\":%lu,"
        "\"offset_ns\":%lld,"
        "\"freq_offset_ppb\":%.3f,"
        "\"pps_count\":%lu,"
        "\"freq_count\":%lu,"
        "\"freq_measurements\":%lu,"
        "\"ntp_requests\":%lu,"
        "\"ptp_syncs\":%lu,"
        "\"ac_mains\":{"
        "\"signal\":%s,"
        "\"freq_hz\":%.3f,"
        "\"avg_hz\":%.3f,"
        "\"min_hz\":%.3f,"
        "\"max_hz\":%.3f,"
        "\"zero_crossings\":%lu"
        "},"
        "\"rf_outputs\":{"
        "\"dcf77\":%s,"
        "\"wwvb\":%s,"
        "\"jjy40\":%s,"
        "\"jjy60\":%s"
        "},"
        "\"nmea\":%s,"
        "\"gps\":{"
        "\"enabled\":%s,"
        "\"has_fix\":%s,"
        "\"has_time\":%s,"
        "\"pps_valid\":%s,"
        "\"satellites\":%d,"
        "\"fix_type\":%d,"
        "\"pps_count\":%lu,"
        "\"nmea_count\":%lu,"
        "\"nmea_errors\":%lu,"
        "\"firmware\":\"%s\","
        "\"hardware\":\"%s\","
        "\"leap_seconds\":%d,"
        "\"leap_valid\":%s"
        "},"
        "\"pulse_outputs\":%s,"
        "\"ip\":\"%s\","
        "\"version\":\"%s\""
        "}",
        (int)g_time_state.sync_state,
        g_time_state.rb_locked ? "true" : "false",
        g_time_state.time_valid ? "true" : "false",
        time_str,
        (unsigned long)uptime_sec,
        (long long)g_time_state.offset_ns,
        (double)g_time_state.frequency_offset,
        (unsigned long)g_time_state.pps_count,
        (unsigned long)g_time_state.last_freq_count,
        (unsigned long)g_stats.freq_measurements,
        (unsigned long)g_stats.ntp_requests,
        (unsigned long)g_stats.ptp_sync_sent,
        ac->signal_present ? "true" : "false",
        (double)ac->frequency_hz,
        (double)ac->frequency_avg_hz,
        (double)ac->frequency_min_hz,
        (double)ac->frequency_max_hz,
        (unsigned long)ac->zero_cross_count,
        radio_timecode_is_enabled(RADIO_DCF77) ? "true" : "false",
        radio_timecode_is_enabled(RADIO_WWVB) ? "true" : "false",
        radio_timecode_is_enabled(RADIO_JJY40) ? "true" : "false",
        radio_timecode_is_enabled(RADIO_JJY60) ? "true" : "false",
        nmea_output_is_enabled() ? "true" : "false",
        gnss_is_enabled() ? "true" : "false",
        gnss_has_fix() ? "true" : "false",
        gnss_has_time() ? "true" : "false",
        gnss_pps_valid() ? "true" : "false",
        (int)gnss_get_satellites(),
        (int)gnss_get_fix_type(),
        (unsigned long)gnss_get_state()->pps_count,
        (unsigned long)gnss_get_state()->nmea_count,
        (unsigned long)gnss_get_state()->nmea_errors,
        gnss_get_firmware_version(),
        gnss_get_hardware_version(),
        (int)gnss_get_leap_seconds(),
        gnss_leap_seconds_is_valid() ? "true" : "false",
        pulse_json,
        ip_str,
        CHRONOS_VERSION_STRING
    );
}

/**
 * Generate JSON config
 */
static int generate_json_config(char *buf, size_t len) {
    config_t *cfg = config_get();

    return snprintf(buf, len,
        "{"
        "\"wifi_ssid\":\"%s\","
        "\"wifi_enabled\":%s,"
        "\"debug_enabled\":%s"
        "}",
        cfg->wifi_ssid,
        cfg->wifi_enabled ? "true" : "false",
        g_debug_enabled ? "true" : "false"
    );
}

/**
 * Generate pulse outputs section for config page
 */
static int generate_pulse_config_html(char *buf, size_t len) {
    const char *mode_names[] = { "Off", "Interval", "Second", "Minute", "Time" };
    int pos = 0;
    int count = 0;

    /* Count active outputs */
    for (int i = 0; i < MAX_PULSE_OUTPUTS; i++) {
        pulse_config_t *p = pulse_output_get(i);
        if (p && p->active) count++;
    }

    pos += snprintf(buf + pos, len - pos,
        "<div class='card'><h2>Pulse Outputs</h2>");

    if (count == 0) {
        pos += snprintf(buf + pos, len - pos,
            "<div class='stat'><span class='stat-label'>Status</span>"
            "<span class='stat-value'>No outputs configured</span></div>");
    } else {
        for (int i = 0; i < MAX_PULSE_OUTPUTS && pos < (int)len - 200; i++) {
            pulse_config_t *p = pulse_output_get(i);
            if (!p || !p->active) continue;

            char config[64];
            switch (p->mode) {
                case PULSE_MODE_INTERVAL:
                    snprintf(config, sizeof(config), "every %lus, %ums",
                        (unsigned long)p->interval, p->pulse_width_ms);
                    break;
                case PULSE_MODE_SECOND:
                    snprintf(config, sizeof(config), "sec %d, %ums x%d",
                        p->trigger_second, p->pulse_width_ms, p->pulse_count);
                    break;
                case PULSE_MODE_MINUTE:
                    snprintf(config, sizeof(config), "min %d, %ums x%d",
                        p->trigger_minute, p->pulse_width_ms, p->pulse_count);
                    break;
                case PULSE_MODE_TIME:
                    snprintf(config, sizeof(config), "%02d:%02d, %ums x%d",
                        p->trigger_hour, p->trigger_minute,
                        p->pulse_width_ms, p->pulse_count);
                    break;
                default:
                    snprintf(config, sizeof(config), "disabled");
            }

            pos += snprintf(buf + pos, len - pos,
                "<div class='stat'><span class='stat-label'>GP%d (%s)</span>"
                "<span class='stat-value'>%s</span></div>",
                p->gpio_pin, mode_names[p->mode], config);
        }
    }

    pos += snprintf(buf + pos, len - pos,
        "<p class='note'>Configure via CLI: pulse &lt;pin&gt; &lt;mode&gt; ...</p></div>");

    return pos;
}

/**
 * Generate config page HTML
 */
static int generate_config_page(char *buf, size_t len, const char *message) {
    config_t *cfg = config_get();

    /* Generate pulse outputs section */
    static char pulse_html[512];
    generate_pulse_config_html(pulse_html, sizeof(pulse_html));

    return snprintf(buf, len, CONFIG_PAGE,
        message ? message : "",
        cfg->wifi_ssid,
        cfg->wifi_enabled ? "checked" : "",
        g_debug_enabled ? "checked" : "",
        pulse_html,
        CHRONOS_VERSION_STRING
    );
}

/**
 * Generate OTA update page HTML
 */
static int generate_ota_page(char *buf, size_t len, const char *message) {
    const ota_status_t *ota = ota_get_status();
    const char *status_msg = "";

    /* Check for update/rollback messages */
    if (ota->is_after_rollback) {
        status_msg = "<div class='msg msg-err'>Rollback occurred - previous update failed!</div>";
    } else if (ota->is_after_update) {
        status_msg = "<div class='msg msg-ok'>Firmware updated successfully!</div>";
    }

    return snprintf(buf, len, OTA_PAGE,
        message ? message : status_msg,
        CHRONOS_VERSION_STRING,
        CHRONOS_BUILD_DATE,
        ota_state_str(ota->state),
        CHRONOS_VERSION_STRING
    );
}

/**
 * Parse HTTP header value
 */
static bool parse_http_header(const char *request, const char *header, char *value, size_t max_len) {
    char search[64];
    snprintf(search, sizeof(search), "%s:", header);

    const char *start = strstr(request, search);
    if (!start) {
        value[0] = '\0';
        return false;
    }

    start += strlen(search);
    while (*start == ' ') start++;  /* Skip whitespace */

    const char *end = strstr(start, "\r\n");
    size_t len = end ? (size_t)(end - start) : strlen(start);
    if (len >= max_len) len = max_len - 1;

    strncpy(value, start, len);
    value[len] = '\0';
    return true;
}

/**
 * URL decode a string in place
 */
static void url_decode(char *str) {
    char *src = str, *dst = str;
    while (*src) {
        if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else if (*src == '%' && src[1] && src[2]) {
            char hex[3] = { src[1], src[2], 0 };
            *dst++ = (char)strtol(hex, NULL, 16);
            src += 3;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

/**
 * Parse form field from POST body
 */
static bool parse_form_field(const char *body, const char *field, char *value, size_t max_len) {
    char search[64];
    snprintf(search, sizeof(search), "%s=", field);

    const char *start = strstr(body, search);
    if (!start) {
        value[0] = '\0';
        return false;
    }

    start += strlen(search);
    const char *end = strchr(start, '&');
    size_t len = end ? (size_t)(end - start) : strlen(start);
    if (len >= max_len) len = max_len - 1;

    strncpy(value, start, len);
    value[len] = '\0';
    url_decode(value);
    return true;
}

/**
 * Check if form field (checkbox) is present
 */
static bool parse_form_checkbox(const char *body, const char *field) {
    char search[64];
    snprintf(search, sizeof(search), "%s=", field);
    return strstr(body, search) != NULL;
}

/*============================================================================
 * TCP CALLBACKS
 *============================================================================*/

static err_t web_sent_callback(void *arg, struct tcp_pcb *tpcb, u16_t len) {
    (void)arg;
    (void)len;
    tcp_close(tpcb);
    return ERR_OK;
}

/* OTA chunk buffering state */
static uint8_t ota_chunk_buf[1280];  /* Buffer for OTA chunk data */
static size_t ota_chunk_expected = 0;
static size_t ota_chunk_received = 0;
static struct tcp_pcb *ota_chunk_pcb = NULL;

static err_t web_recv_callback(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    (void)arg;

    if (p == NULL || err != ERR_OK) {
        if (p != NULL) pbuf_free(p);
        tcp_close(tpcb);
        /* Reset OTA chunk state if this connection was buffering */
        if (tpcb == ota_chunk_pcb) {
            ota_chunk_expected = 0;
            ota_chunk_received = 0;
            ota_chunk_pcb = NULL;
        }
        return ERR_OK;
    }

    tcp_recved(tpcb, p->tot_len);

    /* Check if we're continuing to receive OTA chunk data */
    if (tpcb == ota_chunk_pcb && ota_chunk_expected > 0) {
        /* Append to buffer */
        size_t to_copy = p->tot_len;
        if (ota_chunk_received + to_copy > sizeof(ota_chunk_buf)) {
            to_copy = sizeof(ota_chunk_buf) - ota_chunk_received;
        }
        pbuf_copy_partial(p, ota_chunk_buf + ota_chunk_received, to_copy, 0);
        ota_chunk_received += to_copy;
        pbuf_free(p);

        /* Check if we have all the data */
        if (ota_chunk_received >= ota_chunk_expected) {
            printf("[WEB] OTA chunk complete: %zu bytes\n", ota_chunk_received);
            ota_error_t ota_err = ota_write_chunk(ota_chunk_buf, ota_chunk_received);

            static char response[256];
            if (ota_err == OTA_OK) {
                snprintf(response, sizeof(response), "%sOK", HTTP_OK_TEXT);
            } else {
                snprintf(response, sizeof(response), "%s%s", HTTP_400_RESPONSE, ota_error_str(ota_err));
            }

            size_t resp_len = strlen(response);
            tcp_write(tpcb, response, resp_len, TCP_WRITE_FLAG_COPY);
            tcp_output(tpcb);
            tcp_close(tpcb);

            ota_chunk_expected = 0;
            ota_chunk_received = 0;
            ota_chunk_pcb = NULL;
        }
        return ERR_OK;
    }

    /* Copy request to buffer for parsing - use pbuf_copy_partial to handle chained pbufs */
    static char request[2048];
    size_t copy_len = p->tot_len < sizeof(request) - 1 ? p->tot_len : sizeof(request) - 1;
    pbuf_copy_partial(p, request, copy_len, 0);
    request[copy_len] = '\0';

    pbuf_free(p);

    /* Allocate response buffer */
    static char response[20000];
    static char html_buf[18000];
    static char cli_output[4096];
    const char *msg = NULL;

    /* Parse HTTP method and path */
    bool is_post = (strncmp(request, "POST ", 5) == 0);

    if (strstr(request, "/api/time") != NULL) {
        /* Lightweight time-only API for fast polling */
        char time_str[32];
        format_current_time(time_str, sizeof(time_str));
        snprintf(response, sizeof(response),
            "%s{\"time\":\"%s\",\"valid\":%s}",
            HTTP_JSON_HEADER, time_str,
            g_time_state.time_valid ? "true" : "false");

    } else if (strstr(request, "/api/status") != NULL) {
        /* JSON status API */
        char json_buf[960];  /* Increased for GPS diagnostics */
        generate_json_status(json_buf, sizeof(json_buf));
        snprintf(response, sizeof(response), "%s%s", HTTP_JSON_HEADER, json_buf);

    } else if (strstr(request, "/api/config") != NULL) {
        /* JSON config API */
        char json_buf[256];
        generate_json_config(json_buf, sizeof(json_buf));
        snprintf(response, sizeof(response), "%s%s", HTTP_JSON_HEADER, json_buf);

    } else if (is_post && strstr(request, "/api/rf") != NULL) {
        /* POST /api/rf - toggle RF outputs */
        const char *body = strstr(request, "\r\n\r\n");
        if (body) {
            body += 4;
            config_t *cfg = config_get();

            char val[8];
            if (parse_form_field(body, "dcf77", val, sizeof(val))) {
                bool enable = (strcmp(val, "on") == 0 || strcmp(val, "1") == 0);
                radio_timecode_enable(RADIO_DCF77, enable);
                cfg->rf_dcf77_enabled = enable;
            }
            if (parse_form_field(body, "wwvb", val, sizeof(val))) {
                bool enable = (strcmp(val, "on") == 0 || strcmp(val, "1") == 0);
                radio_timecode_enable(RADIO_WWVB, enable);
                cfg->rf_wwvb_enabled = enable;
            }
            if (parse_form_field(body, "jjy40", val, sizeof(val))) {
                bool enable = (strcmp(val, "on") == 0 || strcmp(val, "1") == 0);
                radio_timecode_enable(RADIO_JJY40, enable);
                cfg->rf_jjy40_enabled = enable;
            }
            if (parse_form_field(body, "jjy60", val, sizeof(val))) {
                bool enable = (strcmp(val, "on") == 0 || strcmp(val, "1") == 0);
                radio_timecode_enable(RADIO_JJY60, enable);
                cfg->rf_jjy60_enabled = enable;
            }
            if (parse_form_field(body, "nmea", val, sizeof(val))) {
                bool enable = (strcmp(val, "on") == 0 || strcmp(val, "1") == 0);
                nmea_output_enable(enable);
                cfg->nmea_enabled = enable;
            }
            if (parse_form_field(body, "gps", val, sizeof(val))) {
                bool enable = (strcmp(val, "on") == 0 || strcmp(val, "1") == 0);
                gnss_enable(enable);
                cfg->gnss_enabled = enable;
            }
            if (parse_form_checkbox(body, "save")) {
                config_save();
            }
        }
        snprintf(response, sizeof(response), "%s{\"ok\":true}", HTTP_JSON_HEADER);

    } else if (is_post && strstr(request, "/api/cli") != NULL) {
        /* POST /api/cli - execute CLI command and return JSON */
        const char *body = strstr(request, "\r\n\r\n");
        if (body) {
            body += 4;
            char cmd[128];
            parse_form_field(body, "cmd", cmd, sizeof(cmd));
            if (cmd[0]) {
                cli_execute(cmd, cli_output, sizeof(cli_output));
                /* Escape output for JSON */
                static char escaped[8192];
                char *out = escaped;
                for (const char *p = cli_output; *p && out < escaped + sizeof(escaped) - 6; p++) {
                    if (*p == '"') { *out++ = '\\'; *out++ = '"'; }
                    else if (*p == '\\') { *out++ = '\\'; *out++ = '\\'; }
                    else if (*p == '\n') { *out++ = '\\'; *out++ = 'n'; }
                    else if (*p == '\r') { *out++ = '\\'; *out++ = 'r'; }
                    else if (*p == '\t') { *out++ = '\\'; *out++ = 't'; }
                    else if ((unsigned char)*p >= 32) { *out++ = *p; }
                }
                *out = '\0';
                snprintf(response, sizeof(response), "%s{\"ok\":true,\"output\":\"%s\"}", HTTP_JSON_HEADER, escaped);
            } else {
                snprintf(response, sizeof(response), "%s{\"ok\":false,\"error\":\"No command\"}", HTTP_JSON_HEADER);
            }
        } else {
            snprintf(response, sizeof(response), "%s{\"ok\":false,\"error\":\"No body\"}", HTTP_JSON_HEADER);
        }

    } else if (strstr(request, "/api/logs") != NULL) {
        /* GET /api/logs?pos=N - get log output since position N */
        static uint32_t client_pos = 0;  /* Track position per connection */

        /* Parse position parameter */
        const char *pos_param = strstr(request, "pos=");
        if (pos_param) {
            client_pos = (uint32_t)strtoul(pos_param + 4, NULL, 10);
        }

        /* Read logs into buffer */
        static char log_buf[4096];
        size_t log_len = log_buffer_read(log_buf, sizeof(log_buf), &client_pos);

        /* Escape for JSON */
        static char escaped[8192];
        char *out = escaped;
        for (size_t i = 0; i < log_len && out < escaped + sizeof(escaped) - 6; i++) {
            char c = log_buf[i];
            if (c == '"') { *out++ = '\\'; *out++ = '"'; }
            else if (c == '\\') { *out++ = '\\'; *out++ = '\\'; }
            else if (c == '\n') { *out++ = '\\'; *out++ = 'n'; }
            else if (c == '\r') { *out++ = '\\'; *out++ = 'r'; }
            else if (c == '\t') { *out++ = '\\'; *out++ = 't'; }
            else if ((unsigned char)c >= 32) { *out++ = c; }
        }
        *out = '\0';

        snprintf(response, sizeof(response),
            "%s{\"pos\":%lu,\"data\":\"%s\"}",
            HTTP_JSON_HEADER, (unsigned long)client_pos, escaped);

    } else if (strstr(request, "/api/ac_history") != NULL) {
        /* GET /api/ac_history - get AC frequency history for graphing */
        static float min_buf[AC_FREQ_MINUTE_HISTORY];
        static float hour_buf[AC_FREQ_HOUR_HISTORY];

        int min_count = ac_freq_get_minute_history(min_buf, AC_FREQ_MINUTE_HISTORY);
        int hour_count = ac_freq_get_hour_history(hour_buf, AC_FREQ_HOUR_HISTORY);

        /* Get current Unix time */
        timestamp_t ts = get_current_time();
        uint32_t unix_time = (ts.seconds >= NTP_UNIX_OFFSET) ? ts.seconds - NTP_UNIX_OFFSET : 0;

        /* Build JSON response */
        char *p = response;
        int rem = sizeof(response);
        int n;

        n = snprintf(p, rem, "%s{\"time_unix\":%lu,\"minutes\":[", HTTP_JSON_HEADER, (unsigned long)unix_time);
        p += n; rem -= n;

        for (int i = 0; i < min_count && rem > 20; i++) {
            n = snprintf(p, rem, "%s%.3f", i > 0 ? "," : "", min_buf[i]);
            p += n; rem -= n;
        }

        n = snprintf(p, rem, "],\"hours\":[");
        p += n; rem -= n;

        for (int i = 0; i < hour_count && rem > 20; i++) {
            n = snprintf(p, rem, "%s%.3f", i > 0 ? "," : "", hour_buf[i]);
            p += n; rem -= n;
        }

        n = snprintf(p, rem, "],\"min_count\":%d,\"hour_count\":%d}", min_count, hour_count);

    } else if (is_post && strstr(request, "/config") != NULL) {
        /* POST /config - save configuration */
        const char *body = strstr(request, "\r\n\r\n");
        if (body) {
            body += 4;

            char ssid[33], pass[65];
            parse_form_field(body, "ssid", ssid, sizeof(ssid));
            parse_form_field(body, "pass", pass, sizeof(pass));
            bool auto_connect = parse_form_checkbox(body, "auto");
            bool debug = parse_form_checkbox(body, "debug");

            /* Update debug flag */
            g_debug_enabled = debug;

            /* Update WiFi config if SSID provided */
            if (ssid[0]) {
                config_set_wifi(ssid, pass[0] ? pass : NULL, auto_connect);
            }

            /* Save to flash */
            if (config_save()) {
                msg = "<div class='msg msg-ok'>Configuration saved!</div>";
            } else {
                msg = "<div class='msg msg-err'>Failed to save configuration</div>";
            }
        }
        generate_config_page(html_buf, sizeof(html_buf), msg);
        snprintf(response, sizeof(response), "%s%s", HTTP_RESPONSE_HEADER, html_buf);

    } else if (strstr(request, "GET /config") != NULL) {
        /* GET /config - show config page */
        generate_config_page(html_buf, sizeof(html_buf), NULL);
        snprintf(response, sizeof(response), "%s%s", HTTP_RESPONSE_HEADER, html_buf);

    } else if (strstr(request, "GET / ") != NULL || strstr(request, "GET /index") != NULL) {
        /* Status page */
        generate_status_page(html_buf, sizeof(html_buf));
        snprintf(response, sizeof(response), "%s%s", HTTP_RESPONSE_HEADER, html_buf);

    } else if (strstr(request, "GET /acfreq") != NULL) {
        /* AC frequency graph page */
        snprintf(response, sizeof(response), "%s%s", HTTP_RESPONSE_HEADER, AC_GRAPH_PAGE);

    } else if (strstr(request, "GET /ota") != NULL) {
        /* OTA update page */
        generate_ota_page(html_buf, sizeof(html_buf), NULL);
        snprintf(response, sizeof(response), "%s%s", HTTP_RESPONSE_HEADER, html_buf);

    } else if (is_post && strstr(request, "/api/ota/begin") != NULL) {
        /* OTA begin - parse size from header */
        char size_str[16];
        if (parse_http_header(request, "X-OTA-Size", size_str, sizeof(size_str))) {
            size_t fw_size = (size_t)atoi(size_str);
            ota_error_t err = ota_begin(fw_size, 0);
            if (err == OTA_OK) {
                snprintf(response, sizeof(response), "%sOK", HTTP_OK_TEXT);
            } else {
                snprintf(response, sizeof(response), "%s%s", HTTP_400_RESPONSE, ota_error_str(err));
            }
        } else {
            snprintf(response, sizeof(response), "%sMissing X-OTA-Size header", HTTP_400_RESPONSE);
        }

    } else if (is_post && strstr(request, "/api/ota/chunk") != NULL) {
        /* OTA chunk - write binary data (may arrive in multiple TCP packets) */
        char content_len_str[16] = {0};
        size_t content_len = 0;
        if (parse_http_header(request, "Content-Length", content_len_str, sizeof(content_len_str))) {
            content_len = (size_t)atoi(content_len_str);
        }

        const char *body = strstr(request, "\r\n\r\n");
        if (body && content_len > 0) {
            body += 4;
            size_t header_len = body - request;
            size_t body_in_first_packet = copy_len - header_len;

            printf("[WEB] OTA chunk: content_len=%zu, got=%zu\n", content_len, body_in_first_packet);

            if (body_in_first_packet >= content_len) {
                /* All data arrived in first packet */
                ota_error_t err = ota_write_chunk((const uint8_t *)body, content_len);
                if (err == OTA_OK) {
                    snprintf(response, sizeof(response), "%sOK", HTTP_OK_TEXT);
                } else {
                    snprintf(response, sizeof(response), "%s%s", HTTP_400_RESPONSE, ota_error_str(err));
                }
            } else {
                /* Need to buffer and wait for more data */
                if (content_len > sizeof(ota_chunk_buf)) {
                    snprintf(response, sizeof(response), "%sChunk too large", HTTP_400_RESPONSE);
                } else {
                    /* Copy what we have and wait for more */
                    memcpy(ota_chunk_buf, body, body_in_first_packet);
                    ota_chunk_received = body_in_first_packet;
                    ota_chunk_expected = content_len;
                    ota_chunk_pcb = tpcb;
                    printf("[WEB] OTA chunk: buffering, need %zu more\n", content_len - body_in_first_packet);
                    return ERR_OK;  /* Don't send response yet, wait for more data */
                }
            }
        } else {
            snprintf(response, sizeof(response), "%sNo body or Content-Length", HTTP_400_RESPONSE);
        }

    } else if (is_post && strstr(request, "/api/ota/finish") != NULL) {
        /* OTA finish - validate and mark ready */
        ota_error_t err = ota_finish();
        if (err == OTA_OK) {
            snprintf(response, sizeof(response), "%sOK", HTTP_OK_TEXT);
        } else {
            snprintf(response, sizeof(response), "%s%s", HTTP_400_RESPONSE, ota_error_str(err));
        }

    } else if (is_post && strstr(request, "/api/ota/apply") != NULL) {
        /* OTA apply - reboot with new firmware */
        snprintf(response, sizeof(response), "%sRebooting...", HTTP_OK_TEXT);
        /* Send response first, then reboot */
        size_t resp_len = strlen(response);
        tcp_write(tpcb, response, resp_len, TCP_WRITE_FLAG_COPY);
        tcp_output(tpcb);
        sleep_ms(100);
        ota_apply_and_reboot();
        /* Won't return */

    } else if (is_post && strstr(request, "/api/ota/abort") != NULL) {
        /* OTA abort - cancel current upload */
        ota_abort();
        snprintf(response, sizeof(response), "%sAborted", HTTP_OK_TEXT);

    } else if (strstr(request, "/api/ota/status") != NULL) {
        /* OTA status JSON */
        const ota_status_t *ota = ota_get_status();
        snprintf(response, sizeof(response),
            "%s{\"state\":\"%s\",\"progress\":%d,\"total\":%u,\"received\":%u,\"error\":\"%s\"}",
            HTTP_JSON_HEADER,
            ota_state_str(ota->state),
            ota_get_progress(),
            (unsigned)ota->total_size,
            (unsigned)ota->bytes_received,
            ota_error_str(ota->last_error));

    } else {
        strcpy(response, HTTP_404_RESPONSE);
    }

    /* Send response */
    size_t resp_len = strlen(response);
    err_t write_err = tcp_write(tpcb, response, resp_len, TCP_WRITE_FLAG_COPY);

    if (write_err == ERR_OK) {
        tcp_output(tpcb);
        tcp_sent(tpcb, web_sent_callback);
    } else {
        tcp_close(tpcb);
    }

    led_blink_activity();
    return ERR_OK;
}

static err_t web_accept_callback(void *arg, struct tcp_pcb *newpcb, err_t err) {
    (void)arg;
    
    if (err != ERR_OK || newpcb == NULL) {
        return ERR_VAL;
    }
    
    tcp_recv(newpcb, web_recv_callback);
    return ERR_OK;
}

/*============================================================================
 * INITIALIZATION
 *============================================================================*/

/**
 * Initialize web server
 */
void web_init(void) {
    if (web_running) {
        printf("[WEB] Already running\n");
        return;
    }

    printf("[WEB] Initializing web interface\n");

    web_pcb = tcp_new();
    if (web_pcb == NULL) {
        printf("[WEB] ERROR: Failed to create TCP PCB\n");
        return;
    }
    
    err_t err = tcp_bind(web_pcb, IP_ADDR_ANY, WEB_PORT);
    if (err != ERR_OK) {
        printf("[WEB] ERROR: Failed to bind to port %d\n", WEB_PORT);
        tcp_close(web_pcb);
        web_pcb = NULL;
        return;
    }
    
    web_pcb = tcp_listen(web_pcb);
    if (web_pcb == NULL) {
        printf("[WEB] ERROR: Failed to listen\n");
        return;
    }
    
    tcp_accept(web_pcb, web_accept_callback);
    
    web_running = true;
    char ip_str[20];
    get_ip_address_str(ip_str, sizeof(ip_str));
    printf("[WEB] Server running on port %d\n", WEB_PORT);
    printf("[WEB] Status page: http://%s/\n", ip_str);
    printf("[WEB] JSON API: http://%s/api/status\n", ip_str);
}

/**
 * Web server task
 */
void web_task(void) {
    /* Currently nothing needed - all handling is in callbacks */
}

/**
 * Check if web server is running
 */
bool web_is_running(void) {
    return web_running;
}

/**
 * Shutdown web server
 */
void web_shutdown(void) {
    if (web_pcb != NULL) {
        tcp_close(web_pcb);
        web_pcb = NULL;
        web_running = false;
        printf("[WEB] Server stopped\n");
    }
}
