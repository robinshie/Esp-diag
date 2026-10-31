"""Curated message facts -> authored synthetic paraphrases, not collected real logs.

All variants of a mnemonic family, including up/down and alarm/clear, share one
group. Near-identical documentation from other product series is NOT a new group.
"""
import json
from pathlib import Path

HERE = Path(__file__).resolve().parent
# source, module/mnemonic, semantic intent, independently authored compact paraphrases
FACTS = [
 ('ifnet','IFNET/PHY_UPDOWN','link_down','Interface {port} physical carrier became down','The physical ethernet connection on {port} is unavailable'),
 ('ifnet','IFNET/PHY_UPDOWN','normal','Interface {port} physical carrier became up','The physical ethernet connection on {port} is available'),
 ('ifnet','IFNET/LINK_UPDOWN','link_down','Interface {port} line protocol became down','Link layer protocol is unavailable at {port}'),
 ('ifnet','IFNET/LINK_UPDOWN','normal','Interface {port} line protocol became up','Link layer protocol is available at {port}'),
 ('ifnet','IFNET/PROTOCOL_UPDOWN','link_down','Network protocol on {port} transitioned down','Protocol connectivity became unavailable at {port}'),
 ('ifnet','IFNET/PROTOCOL_UPDOWN','normal','Network protocol on {port} transitioned up','Protocol connectivity became available at {port}'),
 ('lpdt','LPDT/LPDT_LOOPED','loop','Local loop detection reports a loop at {port}','Port {port} received a loop detection indication'),
 ('lpdt','LPDT/LPDT_VLAN_LOOPED','loop','VLAN {vlan} loop detection triggered at {port}','Loop indication for VLAN {vlan} was observed at {port}'),
 ('lpdt','LPDT/LPDT_RECOVERED','recovery_report','Port {port} reports loop detection cleared','Loop detection recovery was reported for {port}'),
 ('stp','STP/STP_LOOP_PROTECTION','stp_protection','Loop guard on {port} missed configuration BPDU packets','Configuration BPDUs stopped arriving on protected interface {port}'),
 ('stp','STP/STP_LOOPBACK_PROTECTION','stp_protection','STP interface {port} received a BPDU originating from itself','Own BPDU returned to spanning tree port {port}'),
 ('stp','STP/STP_NOT_ROOT','stp_protection','Switch lost root bridge role after a superior BPDU','A better BPDU changed the root bridge selection'),
 ('stp','STP/STP_FORWARDING','normal','Spanning tree selected forwarding for interface {port}','STP forwarding state now applies to {port}'),
 ('fan','DEV/FAN_FAILED','fan','Cooling fan {slot} stopped unexpectedly','Fan tray {slot} reports an operational fault'),
 ('fan','DEV/FAN_ABSENT','fan','Cooling tray {slot} is missing from its slot','Fan presence check could not find tray {slot}'),
 ('fan','DEV/FAN_DIRECTION_NOT_PREFERRED','fan','Fan {slot} airflow conflicts with the configured direction','Cooling tray direction does not match the airflow setting'),
 ('fan-clear','DEV/FAN_RECOVERED','recovery_report','Fan tray {slot} reports normal operation again','Cooling fan {slot} returned to working state'),
 ('dev','DEV/POWER_FAILED','power','Power module {slot} reports failure','Supply {slot} is not operating correctly'),
 ('dev','DEV/POWER_MONITOR_ABSENT','power','Power monitoring module {slot} was not found','Supply monitor {slot} presence is missing'),
 ('dev','DEV/TEMPERATURE_WARNING','thermal','Sensor {slot} temperature crossed the warning boundary','A high temperature warning applies to sensor {slot}'),
 ('thermal','DEV/TEMPERATURE_SHUTDOWN','thermal','Sensor {slot} exceeded the thermal shutdown limit','Overtemperature reached the automatic shutdown boundary'),
 ('temp-clear','DEV/TEMPERATURE_NORMAL','recovery_report','Sensor {slot} temperature returned inside its normal interval','Temperature recovery was reported by sensor {slot}'),
 ('power','DEV/POWER_ABSENT','power','Supply module {slot} is no longer present','Power supply presence check failed for module {slot}'),
 ('power','DEV/POWER_MONITOR_FAILED','power','Supply monitoring unit {slot} reports a fault','Power monitor {slot} cannot operate correctly'),
 ('power','DEV/POWER_MONITOR_RECOVERED','recovery_report','Supply monitoring unit {slot} reports recovery','Power monitor {slot} is working again'),
 ('power','DEV/POWER_RECOVERED','recovery_report','Supply module {slot} reports recovery','Power supply {slot} returned to normal operation'),
 ('diag','DIAG/CPU_MINOR_THRESHOLD','resource_pressure','CPU utilization crossed its minor alarm boundary','Processor usage has entered a minor alarm condition'),
 ('diag','DIAG/CPU_MINOR_RECOVERY','recovery_report','CPU utilization fell into the recovery interval','Processor minor usage alarm was removed'),
 ('diag','DIAG/MEM_EXCEED_THRESHOLD','resource_pressure','Memory consumption crossed the configured alarm boundary','Memory resources reached a usage alarm threshold'),
 ('diag','DIAG/MEM_BELOW_THRESHOLD','recovery_report','Memory consumption returned below its recovery boundary','Memory usage alarm was removed'),
 ('core','DIAG/CORE_MINOR_THRESHOLD','resource_pressure','CPU core {slot} utilization exceeded its warning boundary','Processor core {slot} usage entered a minor alarm condition'),
 ('core','DIAG/CORE_MINOR_RECOVERY','recovery_report','CPU core {slot} minor usage alarm was removed','Processor core {slot} utilization returned to recovery range'),
 ('core','DIAG/CORE_RECOVERY','recovery_report','CPU core {slot} severe usage alarm was removed','Processor core {slot} reports usage recovery'),
 ('shell','SHELL/SHELL_LOGINFAIL','login_failure','Management user login attempt was unsuccessful','User authentication did not complete a management login'),
 ('shell','SHELL/SHELL_LOGIN','normal','Management session login completed successfully','User established a management session'),
 ('shell','SHELL/SHELL_LOGOUT','normal','User ended a management session','Management session logout was recorded'),
 ('shell','SHELL/SHELL_CMD','normal','Administrative command audit record was created','A user command was recorded for auditing'),
 ('shell','SSH/SSH_LOGIN','normal','SSH management login completed successfully','User established an SSH management session'),
 ('aaa','AAA/AAA_FAILURE','login_failure','AAA request did not pass authentication or authorization','AAA service rejected the requested user access'),
 ('aaa','AAA/AAA_SUCCESS','normal','AAA request passed access validation','AAA service accepted the user request'),
 ('aaa','AAA/AAA_LAUNCH','normal','A user access request was submitted to AAA','AAA processing started for a user request'),
 ('mac','MAC/MAC_FLAPPING','mac_move','A learned MAC address moved between ports {port} and {peer}','MAC forwarding location changed within VLAN {vlan}'),
 ('syslog','SYSLOG/SYSLOG_LOGFILE_FULL','record_gap','The allocated log file capacity has been exhausted','Journal file storage has reached its capacity limit'),
 ('syslog','SYSLOG/SYSLOG_NO_SPACE','record_gap','Insufficient storage prevented saving device logs','Log persistence could not continue because storage is exhausted'),
 ('syslog','SYSLOG/SYSLOG_LOGBUFFER_FAILURE','record_gap','Log buffer delivery failed after an internal communication timeout','A timeout prevented recording logs in the device buffer'),
 ('syslog','SYSLOG/SYSLOG_RTM_EVENT_BUFFER_FULL','record_gap','A full event buffer prevented monitoring some log records','Log monitoring missed records because its buffer filled'),
 ('syslog','SYSLOG/SYSLOG_RESTART','normal','The Comware system generated a restart notice','System startup was recorded after a restart')
]


def main():
    sources = {s['id'] for s in json.loads((HERE / 'sources.json').read_text())['sources']}
    rows=[]
    for source, key, label, *phrases in FACTS:
        assert source in sources
        # Shared family for CPU alarms AND recovery; prevents sibling-form leakage.
        family=key
        for stem in ('CPU_MINOR','CORE_MINOR','POWER_MONITOR','FAN','TEMPERATURE','SYSLOG'):
            if key.split('/')[1].startswith(stem):
                family=key.split('/')[0]+'/'+stem
                break
        if key.startswith('DIAG/MEM_'):family='DIAG/MEM'
        if key in ('DEV/POWER_FAILED','DEV/POWER_ABSENT','DEV/POWER_RECOVERED'):family='DEV/POWER'
        if key.startswith('LPDT/'):family='LPDT/LOOP'
        for index,phrase in enumerate(phrases):
            rows.append({'id':f'{key}:{label}:{index}','source_id':source,
                         'module':key.split('/')[0],'mnemonic':key.split('/')[1],
                         'family':'h3c:'+family,'label':label,'body':phrase,
                         'kind':'official_fact_authored_paraphrase'})
    (HERE / 'h3c.json').write_text(json.dumps(rows,ensure_ascii=False,indent=2)+'\n')
    print(f'{len(sources)} official sources, {len(FACTS)} event/state entries, {len(rows)} authored paraphrases')


if __name__=='__main__':main()
