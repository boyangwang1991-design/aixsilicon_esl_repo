#pragma once
#include <algorithm>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <vector>
namespace aix::esl {
// Closed-loop dependency admission. SystemC consumers execute work and report completion.
// Each task's output buffer remains charged until all direct consumers terminate.
class TaskGraph {
public:
    enum class State{waiting,running,succeeded,failed,cancelled};
    struct Task{uint64_t id;std::vector<uint64_t> dependencies;size_t output_bytes=0;};
    TaskGraph(std::vector<Task> tasks,size_t capacity,unsigned concurrency):capacity_(capacity),concurrency_(concurrency){
        if(!concurrency)throw std::invalid_argument("task concurrency");
        for(auto& t:tasks){if(t.output_bytes>capacity||!nodes_.emplace(t.id,Node{t,State::waiting,false}).second)throw std::invalid_argument("task ID/output capacity");}
        for(auto& pair:nodes_){auto& deps=pair.second.task.dependencies;std::sort(deps.begin(),deps.end());
            if(std::adjacent_find(deps.begin(),deps.end())!=deps.end())throw std::invalid_argument("duplicate dependency");
            for(auto id:deps)if(!nodes_.count(id))throw std::invalid_argument("unknown dependency");}
        std::map<uint64_t,unsigned> colors;for(auto& p:nodes_)visit(p.first,colors);
    }
    std::vector<uint64_t> ready()const{
        std::vector<uint64_t> result;
        for(auto& p:nodes_){const auto& n=p.second;if(n.state!=State::waiting)continue;bool ready=true;
            for(auto id:n.task.dependencies)ready&=nodes_.at(id).state==State::succeeded;
            if(ready)result.push_back(p.first);}
        return result;
    }
    bool start(uint64_t id){auto& n=nodes_.at(id);auto candidates=ready();
        if(std::find(candidates.begin(),candidates.end(),id)==candidates.end())throw std::logic_error("task not ready");
        if(running_==concurrency_||n.task.output_bytes>capacity_-used_)return false;
        n.state=State::running;n.held=true;used_+=n.task.output_bytes;++running_;return true;
    }
    void complete(uint64_t id,bool success){auto& n=nodes_.at(id);if(n.state!=State::running)throw std::logic_error("task completion state");
        n.state=success?State::succeeded:State::failed;--running_;
        bool changed=true;while(changed){changed=false;for(auto& p:nodes_)if(p.second.state==State::waiting)
            for(auto dep:p.second.task.dependencies)if(nodes_.at(dep).state==State::failed||nodes_.at(dep).state==State::cancelled){p.second.state=State::cancelled;changed=true;break;}}
        release();
    }
    State state(uint64_t id)const{return nodes_.at(id).state;}
    bool finished()const{for(auto& p:nodes_)if(!terminal(p.second.state))return false;return true;}
    size_t bytes_used()const{return used_;}
    bool stalled()const{
        if(running_||finished())return false;
        for(auto id:ready())if(nodes_.at(id).task.output_bytes<=capacity_-used_)return false;
        return true;
    }
private:
    struct Node{Task task;State state;bool held;};std::map<uint64_t,Node> nodes_;
    size_t capacity_,used_=0;unsigned concurrency_,running_=0;
    static bool terminal(State state){return state==State::succeeded||state==State::failed||state==State::cancelled;}
    void visit(uint64_t id,std::map<uint64_t,unsigned>& colors){if(colors[id]==1)throw std::invalid_argument("DAG cycle");if(colors[id]==2)return;
        colors[id]=1;for(auto dep:nodes_.at(id).task.dependencies)visit(dep,colors);colors[id]=2;}
    void release(){for(auto& p:nodes_){auto& n=p.second;if(!n.held||!terminal(n.state))continue;bool consumed=true;
        for(auto& q:nodes_)if(std::find(q.second.task.dependencies.begin(),q.second.task.dependencies.end(),p.first)!=q.second.task.dependencies.end())consumed&=terminal(q.second.state);
        if(consumed){used_-=n.task.output_bytes;n.held=false;}}}
};
}
