#include <{{name}}/model.hpp>
#include <limits>
namespace aix::esl::{{name}} {
Model::Model(sc_core::sc_module_name n, Config c):sc_module(n),config_(c),value_(c.initial_value){
    registers.register_b_transport(this,&Model::transport);
    registers.register_get_direct_mem_ptr(this,&Model::dmi);
    registers.register_transport_dbg(this,&Model::debug);
}
bool Model::dmi(tlm::tlm_generic_payload& tx,tlm::tlm_dmi&){tx.set_dmi_allowed(false);return false;}
unsigned Model::debug(tlm::tlm_generic_payload&){return 0;}
void Model::transport(tlm::tlm_generic_payload& tx,sc_core::sc_time& delay){
    tx.set_dmi_allowed(false);
    if(!tx.is_read()&&!tx.is_write()){tx.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);return;}
    if(tx.get_address()!=0){tx.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);return;}
    if(!tx.get_data_ptr()||tx.get_data_length()!=4||tx.get_streaming_width()<4){tx.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE);return;}
    if(tx.get_byte_enable_ptr()){tx.set_response_status(tlm::TLM_BYTE_ENABLE_ERROR_RESPONSE);return;}
    if(!accepting_ || config_.access_latency.value()>std::numeric_limits<std::uint64_t>::max()-delay.value()){
        tx.set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE);return;
    }
    auto* bytes=tx.get_data_ptr();
    if(tx.is_write()) {value_=0;for(unsigned i=0;i<4;++i)value_|=std::uint32_t(bytes[i])<<(8*i);}
    else for(unsigned i=0;i<4;++i)bytes[i]=(value_>>(8*i))&255;
    delay+=config_.access_latency;tx.set_response_status(tlm::TLM_OK_RESPONSE);
}
}
