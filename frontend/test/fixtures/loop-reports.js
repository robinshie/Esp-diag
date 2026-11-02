// Synthetic reports are test fixtures only; never imported by the production UI.
export function demoLoopReports(now=Date.now()) {
 const common='device=switch-demo port=GigabitEthernet1/0/8 component=GigabitEthernet1/0/8 fault=LOOP incident=demo-loop-1';
 const texts=[
  `H3C LOG ${common} event=LOOP_DETECTED`,
  `[BDI][LOOP] ${common} stage=ASSESS allowed=true reason=ALLOWLIST`,
  `[BDI][CMD] ISOLATE_LOOP_PORT ${common} action=ISOLATE_LOOP_PORT request_id=demo-req-1`,
  `[BDI][ACK] ${common} action=ISOLATE_LOOP_PORT request_id=demo-req-1 result=COMMAND_ACCEPTED`,
  `[BDI][RECOVERED] ${common} verified=true`
 ];
 return [{report_id:'demo-only',lines:texts.map((text,i)=>({text,at_ms:now-20000+i*4000}))}];
}
