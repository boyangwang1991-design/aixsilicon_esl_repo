#include <gpio/model.hpp>
#include <aix/esl/mmio32.hpp>
#include <stdexcept>
namespace aix::esl::gpio {
namespace {
std::uint32_t mask(unsigned width){if(!width||width>32)throw std::invalid_argument("gpio: width must be 1..32");return width==32?UINT32_MAX:(1u<<width)-1;}
}
struct Model::Impl {
    aix::esl::Mmio32 mmio;
    sc_core::sc_event changed;
    std::uint32_t valid,direction=0,output=0,enable=0,pending=0,previous=0,rising=0;
    sc_core::sc_time edge_time;
    explicit Impl(Config c):mmio(c.access_latency),valid(mask(c.width)){}
};
Model::Model(sc_core::sc_module_name n,Config c):sc_module(n),impl_(std::make_unique<Impl>(c)){
    registers.register_b_transport(this,&Model::transport);
    registers.register_get_direct_mem_ptr(this,&Model::dmi);
    registers.register_transport_dbg(this,&Model::debug);
    SC_METHOD(handle_reset);sensitive<<reset;
    SC_METHOD(update_outputs);sensitive<<reset<<inputs<<impl_->changed;
}
Model::~Model()=default;
bool Model::idle()const{return impl_->mmio.idle();}
const sc_core::sc_event& Model::idle_event()const{return impl_->mmio.idle_event();}
void Model::request_drain(){impl_->mmio.drain();}
bool Model::resume(){return !reset.read()&&impl_->mmio.resume();}
void Model::handle_reset(){
    auto& p=*impl_;p.mmio.reset(reset.read());
    if(reset.read()){p.direction=p.output=p.enable=p.pending=p.rising=0;p.previous=inputs.read().to_uint()&p.valid;}
    p.changed.notify(sc_core::SC_ZERO_TIME);
}
void Model::sample(){
    auto& p=*impl_;auto current=inputs.read().to_uint()&p.valid&~p.direction;
    if(reset.read()){p.previous=current;return;}
    if(p.edge_time!=sc_core::sc_time_stamp()){p.rising=0;p.edge_time=sc_core::sc_time_stamp();}
    p.rising|=current&~p.previous;p.pending|=current&~p.previous;p.previous=current;
}
void Model::update_outputs(){
    sample();auto& p=*impl_;
    outputs.write(reset.read()?0:p.output&p.direction);output_enable.write(reset.read()?0:p.direction);
    irq.write(!reset.read()&&(p.pending&p.enable));
}
bool Model::dmi(int,tlm::tlm_generic_payload& tx,tlm::tlm_dmi&){tx.set_dmi_allowed(false);return false;}
unsigned Model::debug(int,tlm::tlm_generic_payload&){return 0;}
void Model::transport(int,tlm::tlm_generic_payload& tx,sc_core::sc_time& delay){
    impl_->mmio.access(tx,delay,[&](std::uint64_t address,bool write,std::uint32_t& value){
        auto& p=*impl_;
        if(reset.read())return tlm::TLM_GENERIC_ERROR_RESPONSE;
        sample();
        if(write){
            if(value&~p.valid)return tlm::TLM_COMMAND_ERROR_RESPONSE;
            switch(address){
            case reg::direction:p.direction=value;p.previous=inputs.read().to_uint()&p.valid&~p.direction;break;
            case reg::output:p.output=value;break;
            case reg::irq_enable:p.enable=value;break;
            case reg::pending:p.pending=(p.pending&~value)|p.rising;break;
            case reg::input:return tlm::TLM_COMMAND_ERROR_RESPONSE;
            default:return tlm::TLM_ADDRESS_ERROR_RESPONSE;
            }
        }else{switch(address){
            case reg::direction:value=p.direction;break;
            case reg::output:value=p.output;break;
            case reg::input:value=inputs.read().to_uint()&p.valid&~p.direction;break;
            case reg::irq_enable:value=p.enable;break;
            case reg::pending:value=p.pending;break;
            default:return tlm::TLM_ADDRESS_ERROR_RESPONSE;
        }}
        p.changed.notify(sc_core::SC_ZERO_TIME);return tlm::TLM_OK_RESPONSE;
    });
}
}
