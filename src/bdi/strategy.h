#pragma once
#include "../domain/switch_policy.h"
#include <functional>
#include <initializer_list>

namespace switchbdi {
enum class StrategyPhase { Observe, ProposeAction };
class IBdiStrategy {
public:
  std::function<void(const std::string&)> log;
  virtual ~IBdiStrategy()=default;
  virtual const char* id()const=0;
  virtual StrategyPhase phase()const{return StrategyPhase::Observe;}
  virtual void reset()=0;
  virtual void disconnected(){reset();}
  virtual void evaluate(const switchmodel::Observation& observation,const switchmodel::Policy& policy,uint64_t now)=0;
  // Semantic proposal only. Runtime independently authorizes/persists/executes it.
  virtual const switchmodel::ActionPlan* proposal()const{return nullptr;}
  virtual std::string json(uint64_t,uint64_t)const{return "{}";}
};
// Non-owning, fixed-capacity boot-time composition: no registry allocation or
// dynamic loading. Strategies and their immutable identities outlive Runtime.
class StrategySet {
  IBdiStrategy* items[8]{};
  size_t count_=0;
  bool frozen_=false,valid_=true;
public:
  StrategySet()=default;
  StrategySet(std::initializer_list<IBdiStrategy*> items){for(auto* item:items)if(!item||!add(*item))valid_=false;}
  bool valid()const{return valid_;}
  void freeze(){frozen_=true;}
  bool add(IBdiStrategy& item){
    if(frozen_||count_==8||!item.id()||!*item.id())return false;
    for(size_t i=0;i<count_;++i)if(items[i]==&item||std::string(items[i]->id())==item.id())return false;
    items[count_++]=&item;return true;
  }
  size_t size()const{return count_;}
  IBdiStrategy* at(size_t index)const{return index<count_?items[index]:nullptr;}
  void reset(){for(size_t i=0;i<count_;++i)items[i]->reset();}
  void disconnected(){for(size_t i=0;i<count_;++i)items[i]->disconnected();}
  std::string status(const std::string& id,uint64_t now,uint64_t age)const{
    for(size_t i=0;i<count_;++i)if(id==items[i]->id())return items[i]->json(now,age);
    return "{}";
  }
};
}
