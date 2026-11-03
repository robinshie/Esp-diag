#pragma once
#include <functional>
#include "command_transport.h"
#include "../config/communication_timing.h"

namespace switchtransport {
// SSH first; Console starts on demand. Recovery order is strictly:
// Console request finished -> idle wait -> SSH login attempt -> accepted session.
class FailoverTransport : public ICommandTransport {
  ICommandTransport& primary;
  ICommandTransport& backup;
  ICommandTransport* active;
  std::string pending;
  switchcli::Result saved;
  uint64_t unavailableSince=0,epoch=0,waitMs=switchconfig::failoverWaitMs(switchconfig::DEFAULT_FAILOVER_WAIT_SECONDS),nowMs=0,idleSince=0,attemptSince=0;
  bool timing=false,enabled=true,blocked=false,backupStarted=false;
  bool primaryPaused=false,consoleCompleted=false,reconnecting=false,retryBlocked=false;
  void stopBackup(uint64_t now){if(backupStarted&&setBackupEnabled)setBackupEnabled(false,now);backupStarted=false;}
  void primaryEnabled(bool on){primaryPaused=!on;if(setPrimaryEnabled)setPrimaryEnabled(on);}
  bool quiescent()const{return !primaryStopped||primaryStopped();}
  bool waitingForPrimaryStop()const{return active==&backup&&primaryPaused&&!quiescent();}
  void completed(){if(active==&backup){consoleCompleted=true;idleSince=nowMs;}}
  bool settlePending(){
    if(pending.empty())return true;
    if(!active->resultReady(pending))return false;
    const auto oldId=pending;saved=active->takeResult(pending);pending.clear();
    if(saved.id!=oldId||!saved.done()){blocked=true;return false;}
    completed();return true;
  }
  static bool accepted(const std::string& state){return state=="queued"||state=="running";}
  static bool credentialsFailed(const std::string& state){return state=="authentication_failed"||state=="host_key_mismatch"||state=="host_key_store_error"||state=="missing_password";}
  std::string submitCommand(const std::string& id,const std::string& command,switchmodel::Action action){
    if(blocked)return "reconfiguration_requires_restart";
    if(saved.id==id)return saved.command==command?saved.state:"id_conflict";
    if(!pending.empty()&&pending!=id)return "busy";
    if(reconnecting)return "busy";
    if(!sessionAccepted())return "not_connected";
    const auto answer=action==switchmodel::Action::None?active->submit(id,command):active->submitAction(id,action,command.substr(command.find(':')+1));
    if(accepted(answer)){pending=id;if(active==&backup)consoleCompleted=false;}
    return answer;
  }
public:
  std::function<void(const std::string&)> log;
  std::function<void(bool,uint64_t)> setBackupEnabled;
  std::function<void(bool)> setPrimaryEnabled;
  std::function<bool()> primaryStopped;
  FailoverTransport(ICommandTransport& ssh,ICommandTransport& console):primary(ssh),backup(console),active(&ssh){}
  void setWaitSeconds(unsigned seconds){waitMs=switchconfig::failoverWaitMs(seconds);}
  void configure(bool on,uint64_t now=0,unsigned waitSeconds=switchconfig::DEFAULT_FAILOVER_WAIT_SECONDS){
    nowMs=now;setWaitSeconds(waitSeconds);stopBackup(now);
    if(!pending.empty()){blocked=true;if(log)log("[MANAGEMENT][BLOCKED] reason=reconfiguration_with_unsettled_request restart_required=1");}
    if(primaryPaused)primaryEnabled(true);
    enabled=on;timing=consoleCompleted=reconnecting=retryBlocked=false;active=&primary;++epoch;
  }
  void tick(uint64_t now){
    nowMs=now;if(!enabled||blocked)return;
    if(active==&backup){
      // Wait for the SSH worker to acknowledge pause before Console business I/O.
      if(waitingForPrimaryStop())return;
      if(!settlePending()||!consoleCompleted||retryBlocked)return;
      if(!reconnecting){
        if(now-idleSince<switchconfig::CONSOLE_IDLE_WAIT_MS)return;
        reconnecting=true;attemptSince=now;primaryEnabled(true);
        if(log)log("[MANAGEMENT][RECONNECT] transport=ssh reason=console_completed_and_idle idle_ms="+std::to_string(switchconfig::CONSOLE_IDLE_WAIT_MS));
      }
      if(primary.sessionAccepted()){
        active=&primary;timing=consoleCompleted=reconnecting=false;++epoch;stopBackup(now);
        if(log)log("[MANAGEMENT][FAILBACK] from=uart_console to=ssh reason=ssh_login_accepted console=stopped old_requests_replayed=0");
        return;
      }
      const std::string state=primary.stateName();
      if(credentialsFailed(state)||state=="connect_failed"||state=="shell_failed"||state=="out_of_memory"||state=="task_failed"||now-attemptSince>=switchconfig::SSH_RECONNECT_DEADLINE_MS){
        retryBlocked=credentialsFailed(state);primaryEnabled(false);reconnecting=false;idleSince=now;
        if(log)log("[MANAGEMENT][RECONNECT] state=failed reason="+state+" console=retained credentials_require_configuration="+(retryBlocked?"1":"0"));
      }
      return;
    }
    if(primary.sessionAccepted()){timing=false;stopBackup(now);return;}
    if(!timing){unavailableSince=now;timing=true;}
    if(now-unavailableSince<waitMs||!settlePending())return;
    if(!backupStarted){
      backupStarted=true;if(setBackupEnabled)setBackupEnabled(true,now);
      if(log)log("[MANAGEMENT][BACKUP] transport=uart_console state=starting reason=ssh_session_unavailable");
    }
    if(!backup.sessionAccepted())return;
    active=&backup;consoleCompleted=reconnecting=retryBlocked=false;primaryEnabled(false);++epoch;
    if(log)log("[MANAGEMENT][FAILOVER] from=ssh to=uart_console reason=ssh_session_unavailable old_requests_replayed=0");
  }
  uint64_t generation()const{return epoch;}
  bool usingBackup()const{return active==&backup;}
  const char* kind()const override{return active->kind();}
  const char* stateName()const override{
    if(blocked)return "reconfiguration_requires_restart";
    if(active==&backup){if(reconnecting)return "reconnecting_ssh";if(waitingForPrimaryStop())return "waiting_ssh_worker_stop";}
    if(active==&primary&&timing&&!pending.empty())return "waiting_ssh_request_to_finish";
    if(active==&primary&&backupStarted)return "connecting_console";
    return active->stateName();
  }
  bool connected()override{return enabled&&!blocked&&!waitingForPrimaryStop()&&active->connected();}
  bool sessionAccepted()override{return connected()&&active->sessionAccepted();}
  std::string submit(const std::string& id,const std::string& command)override{return submitCommand(id,command,switchmodel::Action::None);}
  std::string submitIsolation(const std::string& id,const std::string& port)override{return submitAction(id,switchmodel::Action::Isolate,port);}
  std::string submitAction(const std::string& id,switchmodel::Action action,const std::string& port)override{return submitCommand(id,switchmodel::actionCommandKey(action,port),action);}
  bool resultReady(const std::string& id)override{return saved.id==id?saved.done():active->resultReady(id);}
  switchcli::Result result(const std::string& id)override{
    if(saved.id==id)return saved;
    auto value=active->result(id);
    if(value.id==pending&&value.done()){saved=active->takeResult(id);pending.clear();completed();return saved;}
    return value;
  }
  switchcli::Result takeResult(const std::string& id)override{
    if(saved.id==id){auto value=saved;saved=switchcli::Result();return value;}
    auto value=active->takeResult(id);if(value.id==pending&&value.done()){pending.clear();completed();}return value;
  }
};
}
