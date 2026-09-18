#include <uart/model.hpp>
#include <aix/esl/mmio32.hpp>
#include <deque>
#include <limits>
#include <stdexcept>
namespace aix::esl::uart {
struct Model::Impl {
    struct Frame { unsigned char byte; bool loopback; };
    Config config;
    aix::esl::Mmio32 mmio;
    std::deque<Frame> tx;
    std::deque<unsigned char> rx;
    sc_core::sc_event wake, changed, became_idle;
    std::uint32_t control=0;
    std::uint64_t epoch=0;
    bool overrun=false;
    explicit Impl(Config c):config(c),mmio(c.access_latency){
        if(!c.tx_depth||!c.rx_depth||c.tx_depth>65535||c.rx_depth>65535||c.frame_time==sc_core::SC_ZERO_TIME)
            throw std::invalid_argument("uart: depths must be 1..65535 and frame_time positive");
    }
};
Model::Model(sc_core::sc_module_name n,Config c):sc_module(n),impl_(std::make_unique<Impl>(c)){
    registers.register_b_transport(this,&Model::transport);
    registers.register_get_direct_mem_ptr(this,&Model::dmi);
    registers.register_transport_dbg(this,&Model::debug);
    SC_THREAD(run);
    SC_METHOD(handle_reset);sensitive<<reset;
    SC_METHOD(update_outputs);sensitive<<impl_->changed<<impl_->mmio.idle_event();
}
Model::~Model()=default;
bool Model::idle()const{return impl_->mmio.idle()&&impl_->tx.empty();}
const sc_core::sc_event& Model::idle_event()const{return impl_->became_idle;}
void Model::request_drain(){impl_->mmio.drain();impl_->changed.notify(sc_core::SC_ZERO_TIME);}
bool Model::resume(){return !reset.read()&&impl_->mmio.resume();}
void Model::handle_reset(){
    auto& p=*impl_;p.mmio.reset(reset.read());
    if(reset.read()){++p.epoch;p.tx.clear();p.rx.clear();p.control=0;p.overrun=false;p.wake.notify(sc_core::SC_ZERO_TIME);}
    p.changed.notify(sc_core::SC_ZERO_TIME);
}
void Model::update_outputs(){
    auto& p=*impl_;
    irq.write(!reset.read()&&(((p.control&reg::irq_rx)&&!p.rx.empty())||
        ((p.control&reg::irq_tx_empty)&&p.tx.empty())||((p.control&reg::irq_error)&&p.overrun)));
    if(idle())p.became_idle.notify(sc_core::SC_ZERO_TIME);
}
bool Model::receive(unsigned char byte){
    auto& p=*impl_;
    if(reset.read()||!p.mmio.accepting())return false;
    if(p.rx.size()==p.config.rx_depth){p.overrun=true;p.changed.notify(sc_core::SC_ZERO_TIME);return false;}
    p.rx.push_back(byte);p.changed.notify(sc_core::SC_ZERO_TIME);return true;
}
void Model::run(){
    auto& p=*impl_;
    while(true){
        if(reset.read()||p.tx.empty()){sc_core::wait(p.wake);continue;}
        const auto epoch=p.epoch;const auto frame=p.tx.front();
        if(p.config.frame_time.value()>std::numeric_limits<std::uint64_t>::max()-sc_core::sc_time_stamp().value())
            SC_REPORT_FATAL(name(),"UART service deadline overflow");
        auto deadline=sc_core::sc_time_stamp()+p.config.frame_time;
        while(epoch==p.epoch&&!reset.read()&&sc_core::sc_time_stamp()<deadline)
            sc_core::wait(deadline-sc_core::sc_time_stamp(),p.wake);
        if(epoch!=p.epoch||reset.read())continue;
        if(frame.loopback){
            if(p.rx.size()==p.config.rx_depth)p.overrun=true;
            else p.rx.push_back(frame.byte);
        }else{
            while(epoch==p.epoch&&!reset.read()&&!tx.nb_write(frame.byte))
                sc_core::wait(tx.data_read_event()|p.wake);
            if(epoch!=p.epoch||reset.read())continue;
        }
        p.tx.pop_front();p.changed.notify(sc_core::SC_ZERO_TIME);
    }
}
bool Model::dmi(int,tlm::tlm_generic_payload& tx,tlm::tlm_dmi&){tx.set_dmi_allowed(false);return false;}
unsigned Model::debug(int,tlm::tlm_generic_payload&){return 0;}
void Model::transport(int,tlm::tlm_generic_payload& tx,sc_core::sc_time& delay){
    impl_->mmio.access(tx,delay,[&](std::uint64_t address,bool write,std::uint32_t& value){
        auto& p=*impl_;
        if(reset.read())return tlm::TLM_GENERIC_ERROR_RESPONSE;
        if(write){switch(address){
            case reg::data:
                if(value>255)return tlm::TLM_COMMAND_ERROR_RESPONSE;
                if(p.tx.size()==p.config.tx_depth)return tlm::TLM_GENERIC_ERROR_RESPONSE;
                p.tx.push_back({static_cast<unsigned char>(value),bool(p.control&reg::loopback)});
                p.wake.notify(sc_core::SC_ZERO_TIME);break;
            case reg::control:
                if(value&~15u)return tlm::TLM_COMMAND_ERROR_RESPONSE;
                p.control=value;break;
            case reg::error:
                if(value&~1u)return tlm::TLM_COMMAND_ERROR_RESPONSE;
                if(value&1)p.overrun=false;break;
            case reg::status:case reg::levels:return tlm::TLM_COMMAND_ERROR_RESPONSE;
            default:return tlm::TLM_ADDRESS_ERROR_RESPONSE;
        }}else{switch(address){
            case reg::data:
                if(p.rx.empty())return tlm::TLM_GENERIC_ERROR_RESPONSE;
                value=p.rx.front();p.rx.pop_front();break;
            case reg::status:value=(!p.rx.empty()?1u:0u)|(p.tx.size()==p.config.tx_depth?2u:0u)|(p.tx.empty()?4u:0u)|(p.overrun?8u:0u);break;
            case reg::control:value=p.control;break;
            case reg::levels:value=(std::uint32_t(p.tx.size())<<16)|std::uint32_t(p.rx.size());break;
            case reg::error:value=p.overrun;break;
            default:return tlm::TLM_ADDRESS_ERROR_RESPONSE;
        }}
        p.changed.notify(sc_core::SC_ZERO_TIME);return tlm::TLM_OK_RESPONSE;
    });
}
}
