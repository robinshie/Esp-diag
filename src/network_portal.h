#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <WebServer.h>
#include <ETH.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <functional>
#include "network_settings.h"
#include "mqtt_settings.h"
#include "setup_settings.h"
#include "network_page.h"
#include "journal_page.h"
#include "boot_gesture.h"

// Enable only after setting the actual board wiring in platformio.ini.
#ifdef ESP_ETH_W5500
#if !defined(ESP_ETH_SCK) || !defined(ESP_ETH_MISO) || !defined(ESP_ETH_MOSI) || !defined(ESP_ETH_CS) || !defined(ESP_ETH_IRQ) || !defined(ESP_ETH_RST)
#error "W5500 requires ESP_ETH_SCK/MISO/MOSI/CS/IRQ/RST"
#endif
#endif
class NetworkPortal : public BLECharacteristicCallbacks, public BLEServerCallbacks {
  NetworkSettings settings;
  MqttSettings mqttSettings;
  SwitchSettings switchSettings;
  unsigned mqttVersion=0;
  std::function<String()> storageStatus;
  std::function<String()> storageFiles;
  std::function<String(const String&,uint32_t,uint32_t)> storageRead;
  std::function<String(const String&)> logExplain;
  BootGesture bootGesture;
  bool viewingLogs=false;
  static bool parseNumber(const String& text,uint32_t& value){
    if(text.isEmpty()||text.length()>10)return false;
    uint64_t parsed=0;
    for(unsigned i=0;i<text.length();++i){if(text[i]<'0'||text[i]>'9')return false;parsed=parsed*10+text[i]-'0';if(parsed>UINT32_MAX)return false;}
    value=parsed;return true;
  }
  static String urlEncode(const std::string& text) {
    String result;const char* hex="0123456789ABCDEF";
    for(unsigned char c:text){if(isalnum(c)||c=='-'||c=='_')result+=char(c);else {result+='%';result+=hex[c>>4];result+=hex[c&15];}}return result;
  }
  bool allowedOrigin() {
    return !web.hasHeader("Origin") || web.header("Origin")=="http://"+web.client().localIP().toString() || (hotspotReady && web.client().localIP()==WiFi.softAPIP() && web.header("Origin")=="http://esp.setup");
  }
  Preferences preferences;
  WebServer web{80};
  DNSServer dnsServer;
  bool hotspotReady = false;
  uint32_t feedbackUntil = 0;
  bool feedbackError = false;
  void feedback(bool error) { feedbackError=error; feedbackUntil=millis()+3000; }
  void ledTick() {
    uint8_t r=0,g=0,b=0;
    if(feedbackUntil && int32_t(feedbackUntil-millis())>0) {
      if(feedbackError) r=(millis()/150)%2?32:0; else g=32;
    } else if(hotspotReady) {
      if(viewingLogs){if(millis()%1500<130 || (millis()%1500>=260 && millis()%1500<390)){g=20;b=20;}}
      else if((millis()/(WiFi.softAPgetStationNum()?180:700))%2) b=24;
    }
    rgbLedWrite(21,r,g,b);
  }
  bool overlapsHotspot(const NetworkSettings& candidate) {
    if(!hotspotReady) return false;
    uint32_t ip,mask,ap;
    NetworkSettings::address(candidate.ip,ip); NetworkSettings::address(candidate.mask,mask);
    NetworkSettings::address(WiFi.softAPIP().toString().c_str(),ap);
    return (ip & mask)==(ap & mask) || (ip>>8)==(ap>>8);
  }
  void startHotspot() {
    if(hotspotReady) return;
    uint32_t ip,mask; NetworkSettings::address(settings.ip,ip); NetworkSettings::address(settings.mask,mask);
    // Choose a private /24 outside the configured Ethernet subnet.
    IPAddress ap; bool found=false;
    for(unsigned n=4;n<255;++n) {
      uint32_t candidate=(192u<<24)|(168u<<16)|(n<<8)|1;
      if((candidate & mask)!=(ip & mask)) { ap=IPAddress(192,168,n,1);found=true;break; }
    }
    if(!found) {
      uint32_t candidate=(172u<<24)|(31u<<16)|(255u<<8)|1;
      ap=(candidate & mask)!=(ip & mask)?IPAddress(172,31,255,1):IPAddress(10,42,0,1);
    }
    WiFi.mode(WIFI_AP);
    String name=String(viewingLogs?"ESP-Logs-":"ESP-Config-")+String(uint32_t(ESP.getEfuseMac()),HEX);
    if(!WiFi.softAPConfig(ap,ap,IPAddress(255,255,255,0),IPAddress(ap[0],ap[1],ap[2],2),ap) || !WiFi.softAP(name.c_str())) {
      feedback(true);Serial.println("[NETWORK] ERROR hotspot start failed");return;
    }
    hotspotReady=true;
    WiFi.AP.enableDhcpCaptivePortal();
    if(!dnsServer.start(53,"*",ap)) {feedback(true);Serial.println("[NETWORK] ERROR captive DNS start failed");}
    Serial.printf("[NETWORK] hotspot=%s url=http://%s window=10min mode=%s\n",name.c_str(),ap.toString().c_str(),viewingLogs?"logs_read_only":"configuration");
  }
  BLECharacteristic* characteristic = nullptr;
  BLEServer* bleServer = nullptr;
  SemaphoreHandle_t lock = xSemaphoreCreateMutex();
  String fragments, pending;
  uint32_t windowStart = 0, applyAt = 0, closeAt = 0;
  bool opened = false, ethernetReady = false, disconnected = false;
  bool accessWindow() { return opened && !closeAt && uint32_t(millis()-windowStart) < 600000; }
  bool inWindow() { return accessWindow() && !viewingLogs; }
  void onDisconnect(BLEServer*) override { xSemaphoreTake(lock,portMAX_DELAY); fragments=""; disconnected=true; xSemaphoreGive(lock); }
  void onWrite(BLECharacteristic* c) override {
    xSemaphoreTake(lock,portMAX_DELAY);
    String part = c->getValue();
    if (!inWindow()) { fragments=""; c->setValue("ERROR: 配置模式已关闭，请按住 BOOT 3 秒"); }
    else if (!pending.isEmpty()) c->setValue("SAVING");
    else if (part.length()+fragments.length()>80) { fragments=""; c->setValue("ERROR: 数据过长"); }
    else {
      fragments += part;
      int end=fragments.indexOf('\n');
      if (end>=0) {
        if (end==0 || end!=int(fragments.length())-1) c->setValue("ERROR: 配置格式错误");
        else { pending=fragments.substring(0,end); c->setValue("SAVING"); }
        fragments="";
      }
    }
    xSemaphoreGive(lock);
  }
  bool store(const NetworkSettings& next, const SwitchSettings& profile, const MqttSettings& mqtt) {
    SetupSettings bundle;bundle.network=next;bundle.device=profile;bundle.mqtt=mqtt;
    if(!bundle.validate().empty()){feedback(true);return false;}
    String value(bundle.encode().c_str());
    if(preferences.putString("setup-v2",value)!=value.length()){feedback(true);return false;}
    if(mqttSettings.encode()!=mqtt.encode())++mqttVersion;
    settings=next;switchSettings=profile;mqttSettings=mqtt;
    feedback(false);applyAt=millis()+1000;closeAt=millis()+3000;return true;
  }
  bool store(const NetworkSettings& next, const SwitchSettings& profile) {return store(next,profile,mqttSettings);}
  void apply() {
#ifdef ESP_ETH_W5500
    if (ethernetReady) {
      IPAddress ip,mask,gateway,dns;
      ip.fromString(settings.ip.c_str()); mask.fromString(settings.mask.c_str());
      gateway.fromString(settings.gateway.c_str()); dns.fromString(settings.dns.c_str());
      if (!ETH.config(ip,gateway,mask,dns)) Serial.println("[NETWORK] ERROR apply failed");
      Serial.printf("[NETWORK] actual_ip=%s link=%s mask=%s gateway=%s dns=%s\n",
        ETH.localIP().toString().c_str(), ETH.linkUp()?"up":"down",
        ETH.subnetMask().toString().c_str(), ETH.gatewayIP().toString().c_str(), ETH.dnsIP().toString().c_str());
    }
#endif
    Serial.printf("[NETWORK] ip=%s ethernet=%s\n",settings.ip.c_str(),ethernetReady?"ready":"unavailable");
  }
public:
  void setStorageStatus(std::function<String()> provider){storageStatus=provider;}
  void setLogExplainer(std::function<String(const String&)> provider){logExplain=provider;}
  void setStorageViewer(std::function<String()> list,std::function<String(const String&,uint32_t,uint32_t)> read){storageFiles=list;storageRead=read;}
  const MqttSettings& mqttConfig() const {return mqttSettings;}
  unsigned mqttRevision() const {return mqttVersion;}
  void begin() {
    pinMode(0,INPUT_PULLUP);
    preferences.begin("network",false);
    settings.decode(preferences.getString("ipv4", "").c_str());
    mqttSettings.decode(preferences.getString("mqtt-lab", "").c_str());
    std::string setup=preferences.getString("setup-v1", "").c_str();
    size_t split=setup.find('\n');
    if(split!=std::string::npos) {
      NetworkSettings net; SwitchSettings sw;
      if(net.decode(setup.substr(0,split))&&sw.decode(setup.substr(split+1))&&sw.validateNetwork(net).empty()){settings=net;switchSettings=sw;}
      else Serial.println("[NETWORK] ERROR invalid stored setup profile");
    }
    std::string saved=preferences.getString("setup-v2", "").c_str();
    if(!saved.empty()){
      SetupSettings bundle;
      if(bundle.decode(saved)){settings=bundle.network;switchSettings=bundle.device;mqttSettings=bundle.mqtt;}
      else Serial.println("[NETWORK] ERROR invalid stored unified setup");
    }
#ifdef ESP_ETH_W5500
    SPI.begin(ESP_ETH_SCK, ESP_ETH_MISO, ESP_ETH_MOSI);
    ethernetReady=ETH.begin(ETH_PHY_W5500,0,ESP_ETH_CS,ESP_ETH_IRQ,ESP_ETH_RST,SPI);
#endif
    apply();
    String name="ESP-Config-"+String(uint32_t(ESP.getEfuseMac()),HEX);
    BLEDevice::init(name); BLEDevice::setMTU(128);
    auto server=BLEDevice::createServer(); bleServer=server; server->setCallbacks(this);
    auto service=server->createService("68b10001-7d85-4f37-b71b-58d52ce12000");
    characteristic=service->createCharacteristic("68b10002-7d85-4f37-b71b-58d52ce12000",BLECharacteristic::PROPERTY_READ|BLECharacteristic::PROPERTY_WRITE);
    characteristic->setCallbacks(this); characteristic->setValue(settings.encode().c_str()); service->start();
    BLEDevice::getAdvertising()->addServiceUUID(service->getUUID());
    web.on("/",HTTP_GET,[this](){web.sendHeader("Cache-Control","no-store");web.send_P(200,"text/html; charset=utf-8",viewingLogs?JOURNAL_PAGE:NETWORK_PAGE);});
    web.on("/journal",HTTP_GET,[this](){
      if(!accessWindow()){web.send(403,"text/plain","Viewer closed");return;}
      web.sendHeader("Cache-Control","no-store");web.send_P(200,"text/html; charset=utf-8",JOURNAL_PAGE);
    });
    web.on("/portal",HTTP_GET,[this](){web.sendHeader("Location","/",true);web.send(302,"text/plain","");});
    web.on("/captive-portal",HTTP_GET,[this](){web.send(200,"application/captive+json","{\"captive\":true,\"user-portal-url\":\"http://"+WiFi.softAPIP().toString()+"/\"}");});
    web.onNotFound([this](){
      if(hotspotReady && web.client().localIP()==WiFi.softAPIP()) {
        web.sendHeader("Cache-Control","no-store");
        web.sendHeader("Location","http://"+WiFi.softAPIP().toString()+"/",true);
        web.send(302,"text/plain","Open device configuration");
      }else web.send(404,"text/plain","Not found");
    });
    web.on("/network",HTTP_GET,[this](){if(!inWindow()){web.send(403,"text/plain","Configuration closed");return;}web.send(200,"text/plain; charset=utf-8",settings.encode().c_str());});
    web.on("/storage",HTTP_GET,[this](){
      if(!accessWindow()){web.send(403,"text/plain","Viewer closed");return;}
      web.sendHeader("Cache-Control","no-store");
      web.send(200,"application/x-www-form-urlencoded",(storageStatus?storageStatus():String("state=unavailable"))+"&viewer_only="+(viewingLogs?"1":"0"));
    });
    web.on("/storage/files",HTTP_GET,[this](){
      if(!accessWindow()){web.send(403,"text/plain","Viewer closed");return;}
      web.sendHeader("Cache-Control","no-store");
      web.send(200,"application/json; charset=utf-8",storageFiles?storageFiles():String("{\"state\":\"unavailable\",\"files\":[]}"));
    });
    web.on("/model/explain",HTTP_GET,[this](){
      if(!accessWindow()){web.send(403,"text/plain","Viewer closed");return;}
      web.sendHeader("Cache-Control","no-store");
      if(!logExplain){web.send(503,"application/json","{\"experimental\":false,\"message\":\"Model not installed\"}");return;}
      const String text=web.arg("log");
      if(text.isEmpty()||text.length()>1024){web.send(400,"text/plain","Log must contain 1..1024 bytes");return;}
      web.send(200,"application/json; charset=utf-8",logExplain(text));
    });
    web.on("/storage/read",HTTP_GET,[this](){
      if(!accessWindow()){web.send(403,"text/plain","Viewer closed");return;}
      uint32_t offset=0,job=0;
      if((web.hasArg("offset")&&!parseNumber(web.arg("offset"),offset))||(web.hasArg("job")&&!parseNumber(web.arg("job"),job))){web.send(400,"text/plain","Invalid offset/job");return;}
      web.sendHeader("Cache-Control","no-store");
      web.send(200,"application/json; charset=utf-8",storageRead?storageRead(web.arg("file"),offset,job):String("{\"state\":\"error\",\"message\":\"记录任务不可用\"}"));
    });
    web.on("/storage/close",HTTP_POST,[this](){
      if(!accessWindow()||!viewingLogs||!allowedOrigin()){web.send(403,"text/plain","Viewer closed");return;}
      closeAt=millis()+300;web.send(200,"text/plain; charset=utf-8","查看入口已关闭，热点即将隐藏。双击 BOOT 可再次查看。");
    });
    web.on("/network",HTTP_POST,[this](){
      if(!inWindow()){web.send(403,"text/plain; charset=utf-8","配置模式已关闭，请按住 BOOT 3 秒");return;}
      if(!allowedOrigin()) { web.send(403,"text/plain","Invalid origin");return; }
      NetworkSettings next;
      next.ip=web.arg("ip").c_str(); next.mask=web.arg("mask").c_str(); next.gateway=web.arg("gateway").c_str(); next.dns=web.arg("dns").c_str();
      auto error=next.validate();
      if(!error.empty()){feedback(true);web.send(400,"text/plain; charset=utf-8",error.c_str());return;}
      if(overlapsHotspot(next)){feedback(true);web.send(400,"text/plain; charset=utf-8","网口地址不能与配置热点同网段，请更换网口网段；热点地址不会随网口 IP 改变");return;}
      SwitchSettings profile=switchSettings;
      if(web.hasArg("sw_protocol")) {
        profile.protocol=web.arg("sw_protocol").c_str();profile.host=web.arg("sw_host").c_str();profile.username=web.arg("sw_username").c_str();
        profile.fingerprint=web.arg("sw_fingerprint").c_str();profile.transport=web.arg("sw_transport").c_str();
        if(web.hasArg("sw_verify_fingerprint")) {
          if(web.arg("sw_verify_fingerprint")=="0")profile.fingerprint="";
          else if(web.arg("sw_verify_fingerprint")!="1"||profile.fingerprint.empty()) {
            feedback(true);web.send(400,"text/plain; charset=utf-8","启用指纹验证时请填写可信的 SSH 主机指纹");return;
          }
        }
        if(!MqttSettings::parsePort(web.arg("sw_port").c_str(),profile.port)||!SwitchSettings::timeout(web.arg("sw_connect_timeout").c_str(),profile.connectTimeout)||!SwitchSettings::timeout(web.arg("sw_command_timeout").c_str(),profile.commandTimeout)) {
          feedback(true);web.send(400,"text/plain; charset=utf-8","交换机端口或超时无效");return;
        }
        if(web.arg("sw_clear_password")=="1")profile.password="";
        else if(!web.arg("sw_password").isEmpty())profile.password=web.arg("sw_password").c_str();
        if(web.arg("sw_clear_enable_password")=="1")profile.enablePassword="";
        else if(!web.arg("sw_enable_password").isEmpty())profile.enablePassword=web.arg("sw_enable_password").c_str();
        auto error=profile.validate();if(!error.empty()){feedback(true);web.send(400,"text/plain; charset=utf-8",error.c_str());return;}
        if(profile.protocol=="telnet"&&web.arg("sw_telnet_ack")!="1"){feedback(true);web.send(400,"text/plain; charset=utf-8","请确认 Telnet 明文传输风险");return;}
      }
      auto profileError=profile.validateNetwork(next);
      if(!profileError.empty()){feedback(true);web.send(400,"text/plain; charset=utf-8",profileError.c_str());return;}
      MqttSettings mqtt=mqttSettings;
      if(web.hasArg("settings_version")) {
        if(web.arg("settings_version")!="2"||!web.hasArg("sw_protocol")||!web.hasArg("mqtt_enabled")||!web.hasArg("mqtt_host")||!web.hasArg("mqtt_port")||!web.hasArg("mqtt_username")||!web.hasArg("mqtt_prefix")) {
          feedback(true);web.send(400,"text/plain; charset=utf-8","完整配置缺少字段，请刷新页面后重试");return;
        }
        mqtt.enabled=web.arg("mqtt_enabled")=="1";mqtt.host=web.arg("mqtt_host").c_str();mqtt.username=web.arg("mqtt_username").c_str();mqtt.prefix=web.arg("mqtt_prefix").c_str();
        if(!MqttSettings::parsePort(web.arg("mqtt_port").c_str(),mqtt.port)){feedback(true);web.send(400,"text/plain; charset=utf-8","MQTT 端口必须为 1–65535");return;}
        if(web.arg("mqtt_clear_password")=="1")mqtt.password="";
        else if(!web.arg("mqtt_password").isEmpty())mqtt.password=web.arg("mqtt_password").c_str();
        auto mqttError=mqtt.validate();if(!mqttError.empty()){feedback(true);web.send(400,"text/plain; charset=utf-8",mqttError.c_str());return;}
      }
      if(!store(next,profile,mqtt)){web.send(500,"text/plain; charset=utf-8","整份配置保存失败，原配置未更新，请重试");return;}
      xSemaphoreTake(lock,portMAX_DELAY);characteristic->setValue(settings.encode().c_str());xSemaphoreGive(lock);
      web.send(200,"text/plain; charset=utf-8",ethernetReady?"全部配置已保存（交换机尚未连接），1 秒后应用网口地址，3 秒后关闭配置入口":"参数已保存；网口硬件尚未启用，交换机尚未连接");
    });
    web.on("/switch",HTTP_GET,[this](){
      if(!inWindow()){web.send(403,"text/plain","Configuration closed");return;}
      String body="protocol="+urlEncode(switchSettings.protocol)+"&host="+urlEncode(switchSettings.host)+"&port="+String(switchSettings.port)+"&username="+urlEncode(switchSettings.username)+"&fingerprint="+urlEncode(switchSettings.fingerprint)+"&transport="+urlEncode(switchSettings.transport)+"&connect_timeout="+String(switchSettings.connectTimeout)+"&command_timeout="+String(switchSettings.commandTimeout)+"&has_password="+String(switchSettings.password.empty()?0:1)+"&has_enable_password="+String(switchSettings.enablePassword.empty()?0:1)+"&client_supported=0&state=not_connected";
      body+="&verify_fingerprint="+String(switchSettings.fingerprint.empty()?0:1);
      web.sendHeader("Cache-Control","no-store");web.send(200,"application/x-www-form-urlencoded",body);
    });
    web.on("/mqtt",HTTP_GET,[this](){
      if(!inWindow()){web.send(403,"text/plain","Configuration closed");return;}
      String body="setup_version=2&enabled="+String(mqttSettings.enabled?1:0)+"&host="+urlEncode(mqttSettings.host)+"&port="+String(mqttSettings.port)+"&username="+urlEncode(mqttSettings.username)+"&prefix="+urlEncode(mqttSettings.prefix)+"&has_password="+String(mqttSettings.password.empty()?0:1);
#if ESP_MQTT_EXPERIMENT
      body+="&compiled=1";
#else
      body+="&compiled=0";
#endif
      web.sendHeader("Cache-Control","no-store");web.send(200,"application/x-www-form-urlencoded",body);
    });
    web.on("/mqtt",HTTP_POST,[this](){
      if(!inWindow()){web.send(403,"text/plain","Configuration closed");return;}
      web.send(409,"text/plain; charset=utf-8","请刷新配置页，使用底部的保存全部配置按钮统一保存");
    });
    const char* headers[]={"Origin"};web.collectHeaders(headers,1);
    rgbLedWrite(21,0,0,0);
    Serial.println("[NETWORK] closed; double-click BOOT for logs, hold 3 seconds for configuration");
  }
  void open(bool logs=false) {
    if(opened)close();
    xSemaphoreTake(lock,portMAX_DELAY);viewingLogs=logs;windowStart=millis();opened=true;closeAt=0;feedbackUntil=0;fragments="";pending="";xSemaphoreGive(lock);
    startHotspot();
    web.begin();
    if(!logs)BLEDevice::startAdvertising();
    Serial.println(logs?"[NETWORK] read-only TF viewer open for 10 minutes; LED=cyan_double_blink":"[NETWORK] configuration window open for 10 minutes");
  }
  void close() {
    xSemaphoreTake(lock,portMAX_DELAY);
    opened=false;closeAt=0;fragments="";pending="";disconnected=false;
    xSemaphoreGive(lock);
    BLEDevice::stopAdvertising();
    for(const auto& peer:bleServer->getPeerDevices(false)) bleServer->disconnect(peer.first);
    dnsServer.stop();
    if(hotspotReady){WiFi.softAPdisconnect(true);hotspotReady=false;}
    web.stop();
    rgbLedWrite(21,0,0,0);
    Serial.println("[NETWORK] configuration closed; hotspot, BLE and HTTP stopped");
  }
  void tick() {
    const auto gesture=bootGesture.tick(digitalRead(0)==LOW,millis());
    if(gesture==BootGesture::ViewLogs)open(true);
    else if(gesture==BootGesture::Configure)open();
    xSemaphoreTake(lock,portMAX_DELAY);
    bool shouldClose=opened && (uint32_t(millis()-windowStart)>=600000 || (closeAt && int32_t(millis()-closeAt)>=0));
    if(disconnected){disconnected=false;if(inWindow())BLEDevice::startAdvertising();}
    if(!pending.isEmpty()){
      NetworkSettings next;
      if(!next.decode(pending.c_str())){feedback(true);characteristic->setValue("ERROR: IP、掩码或网关无效，请检查同网段及主机地址");}
      else if(overlapsHotspot(next)){feedback(true);characteristic->setValue("ERROR: 网口不能与配置热点同网段");}
      else if(!switchSettings.validateNetwork(next).empty()){feedback(true);characteristic->setValue(("ERROR: "+switchSettings.validateNetwork(next)).c_str());}
      else if(!store(next,switchSettings))characteristic->setValue("ERROR: 保存失败");
      else characteristic->setValue(settings.encode().c_str());
      pending="";
    }
    xSemaphoreGive(lock);
    if(applyAt&&int32_t(millis()-applyAt)>=0){applyAt=0;apply();}
    if(shouldClose)close();
    if(hotspotReady) {
      dnsServer.processNextRequest();
      static uint8_t lastClients=0;
      uint8_t clients=WiFi.softAPgetStationNum();
      if(clients!=lastClients){Serial.printf("[NETWORK] hotspot_clients=%u\n",clients);lastClients=clients;}
    }
    if(opened)web.handleClient();
    ledTick();
  }
};
