#ifndef ESP_CLUSTER_RUNTIME_H
#define ESP_CLUSTER_RUNTIME_H
#include <memory>
#include "cluster_model.h"
#include "agent/agent.h"
#include "bdi_configuration.h"

// Each device owns an independent Embedded-BDI belief/event/intention base.
class ClusterRuntime {
  struct Context {
    AgentSettings settings;
    Agent agent;
    Context() : agent(settings.get_belief_base(), settings.get_event_base(),
                      settings.get_plan_base(), settings.get_intention_base()) {}
    void drain() {
      // Each plan is one action; bounded draining prevents the serial reader
      // from filling the event queue before the next reasoning iteration.
      for (unsigned i = 0; i < 32 &&
           (!settings.get_event_base()->is_empty() || !settings.get_intention_base()->is_empty()); ++i)
        agent.run();
    }
  };
  std::map<std::string, std::unique_ptr<Context>> agents_;
  void belief(Context& c, BdiProposition p, bool value, const char* name) {
    if (c.settings.set_belief(p, value))
      model.emit("[BDI][EVENT] device=" + bdi_device + " belief=" + name +
                 " value=" + (value ? "true" : "false"));
  }
  Context& select(cluster::Device& d) {
    bdi_device = d.id;
    auto& c = agents_[d.id];
    if (!c) c.reset(new Context());
    bdi_request_action = nullptr;
    return *c;
  }
 public:
  cluster::Model model;
  explicit ClusterRuntime(const std::string& boot) : model(boot) {
    model.changed = [this](cluster::Device& d) {
      Context& c = select(d);
      belief(c, SERVER_HEALTHY, d.online && !d.incomplete && d.faults.empty(), "server_healthy");
      belief(c, SERVER_CRITICAL, cluster::Model::critical(d), "server_critical");
      belief(c, SERVER_LOCKED, cluster::Model::locked(d), "server_locked");
      belief(c, SWITCH_LINK_FAULT, cluster::Model::link_fault(d), "switch_link_fault");
      c.drain();
    };
    model.opened = [this](cluster::Device& d, const std::string& key) {
      const auto& fault = d.faults.at(key);
      if (fault.event != "AUTH_LOCKED" && fault.event != "LINK_DOWN") return;
      Context& c = select(d);
      // A new component fault must get its own intention even when another
      // component already holds the same aggregate belief true.
      auto p = fault.event == "AUTH_LOCKED" ? SERVER_LOCKED : SWITCH_LINK_FAULT;
      c.settings.set_belief(p, false); c.drain();
      bdi_request_action = [this, &d, key]() { model.request(d, key); };
      c.settings.set_belief(p, true); c.drain();
      bdi_request_action = nullptr;
    };
  }
};
#endif
