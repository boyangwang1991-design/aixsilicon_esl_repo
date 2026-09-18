#include <uart/model.hpp>
#include <gpio/model.hpp>
#include <irq_controller/model.hpp>
#include <host_master/model.hpp>
#include <tlm_bus/model.hpp>
#include <tlm_utils/simple_initiator_socket.h>
#include <iostream>
#include <stdexcept>
using namespace sc_core;
using namespace aix::esl;
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
constexpr std::uint64_t U=0x20002000,G=0x20003000,U2=0x21002000,G2=0x21003000,I=0x20001000;
tlm_bus::Config map(){tlm_bus::Config c;c.regions={{U,4096,0},{G,4096,1},{U2,4096,2},{G2,4096,3},{I,4096,4}};return c;}
uart::Config uart_config(unsigned depth=2,unsigned frame=10){uart::Config c;c.tx_depth=depth;c.rx_depth=depth;c.frame_time=sc_time(frame,SC_NS);return c;}
gpio::Config gpio_config(unsigned width=8){gpio::Config c;c.width=width;return c;}
struct Bench:sc_module {
    host_master::Model host{"host"};tlm_bus::Model bus{"bus",map()};
    uart::Model u{"uart0",uart_config()},u2{"uart1",uart_config(1,20)};
    gpio::Model g{"gpio0",gpio_config()},g2{"gpio1",gpio_config(4)};
    irq_controller::Model controller{"controller"};
    tlm_utils::simple_initiator_socket<Bench> du{"du"},dg{"dg"},peer{"peer"};
    sc_signal<bool> rst{"rst"},ui{"ui"},ui2{"ui2"},gi{"gi"},gi2{"gi2"},irq{"irq"};
    sc_signal<sc_dt::sc_uint<32>> pins{"pins"},pins2{"pins2"},out{"out"},out2{"out2"},oe{"oe"},oe2{"oe2"};
    sc_fifo<unsigned char> sink{"sink",1},sink2{"sink2",1};
    sc_event start;std::string mode;bool done=false,worker_done=false;
    SC_HAS_PROCESS(Bench);
    Bench(sc_module_name n,std::string m):sc_module(n),mode(m){
        host.memory(bus.targets);bus.initiators(u.registers);bus.initiators(g.registers);bus.initiators(u2.registers);bus.initiators(g2.registers);bus.initiators(controller.registers);
        du(u.registers);dg(g.registers);peer(u.registers);
        u.reset(rst);u2.reset(rst);g.reset(rst);g2.reset(rst);controller.reset(rst);
        u.irq(ui);u2.irq(ui2);g.irq(gi);g2.irq(gi2);u.tx(sink);u2.tx(sink2);
        g.inputs(pins);g.outputs(out);g.output_enable(oe);g2.inputs(pins2);g2.outputs(out2);g2.output_enable(oe2);
        controller.sources[0](ui);controller.sources[1](gi);controller.sources[2](ui2);controller.sources[3](gi2);controller.irq(irq);
        SC_THREAD(run);SC_THREAD(worker);
    }
    void settle(){for(int i=0;i<8;++i)wait(SC_ZERO_TIME);}
    tlm::tlm_response_status wr(std::uint64_t a,std::uint32_t v){std::vector<unsigned char>d(4);for(unsigned i=0;i<4;++i)d[i]=(v>>(8*i))&255;return host.write(a,d);}
    void write(std::uint64_t a,std::uint32_t v){check(wr(a,v)==tlm::TLM_OK_RESPONSE,"write failed");}
    std::uint32_t read(std::uint64_t a){std::vector<unsigned char>d(4);check(host.read(a,d)==tlm::TLM_OK_RESPONSE,"read failed");return std::uint32_t(d[0])|(std::uint32_t(d[1])<<8)|(std::uint32_t(d[2])<<16)|(std::uint32_t(d[3])<<24);}
    tlm::tlm_response_status raw(tlm_utils::simple_initiator_socket<Bench>& socket,std::uint64_t a,std::uint32_t v,unsigned ns=0){
        unsigned char d[4];for(unsigned i=0;i<4;++i)d[i]=(v>>(8*i))&255;
        tlm::tlm_generic_payload tx;tx.set_write();tx.set_address(a);tx.set_data_ptr(d);tx.set_data_length(4);tx.set_streaming_width(4);
        sc_time delay(ns,SC_NS);socket->b_transport(tx,delay);if(tx.is_response_ok())check(delay==SC_ZERO_TIME,"delay unconsumed");return tx.get_response_status();
    }
    void reset_all(){rst.write(true);settle();check(!u.resume()&&!g.resume(),"resume during reset");rst.write(false);settle();check(u.resume()&&u2.resume()&&g.resume()&&g2.resume()&&controller.resume(),"resume failed");}
    void worker(){
        if(mode!="reset_mmio"&&mode!="drain_mmio"&&mode!="capacity"&&mode!="gpio_collision"&&mode!="uart_collision"&&mode!="gpio_mmio_reset"&&mode!="gpio_mmio_drain"&&mode!="gpio_capacity"){worker_done=true;return;}
        wait(start);wait(1,SC_NS);
        if(mode=="reset_mmio"||mode=="gpio_mmio_reset"){rst.write(true);wait(2,SC_NS);rst.write(false);}
        else if(mode=="drain_mmio"||mode=="gpio_mmio_drain"){u.request_drain();g.request_drain();check(mode=="drain_mmio"?(!u.idle()&&!u.resume()):(!g.idle()&&!g.resume()),"in-flight drain must remain busy");}
        else if(mode=="capacity")check(raw(peer,8,7)==tlm::TLM_GENERIC_ERROR_RESPONSE,"MMIO capacity rejection");
        else if(mode=="gpio_capacity")check(raw(dg,4,7)==tlm::TLM_GENERIC_ERROR_RESPONSE,"GPIO capacity rejection");
        else if(mode=="uart_collision")check(!u.receive(3),"overflow expected");
        else {pins.write(1);}
        worker_done=true;
    }
    void run(){
        settle();
        if(mode=="uart_tx"){
            write(U,0xa5);auto accepted=sc_time_stamp();wait(9,SC_NS);check(sink.num_available()==0,"early frame");wait(1,SC_NS);settle();
            unsigned char byte;check(sink.nb_read(byte)&&byte==0xa5&&sc_time_stamp()-accepted==sc_time(10,SC_NS),"frame data/time");
            write(U+8,uart::reg::irq_tx_empty);settle();check(ui.read(),"TX empty IRQ");check(sink.num_available()==0,"duplicate frame");
        }else if(mode=="loopback"){
            write(I+4,1);write(U+8,uart::reg::loopback|uart::reg::irq_rx);write(U,0x81);wait(10,SC_NS);settle();
            check(ui.read()&&irq.read()&&read(U)==0x81,"loopback/IRQ");check(sink.num_available()==0,"loopback leaked externally");
            write(I,1);settle();check(!ui.read()&&!irq.read(),"IRQ source/controller clear");
        }else if(mode=="rx_overrun"){
            write(U+8,uart::reg::irq_error);check(u.receive(0x11)&&u.receive(0x22)&&!u.receive(0x33),"RX capacity");settle();check(ui.read(),"overrun IRQ");
            check(read(U+16)==1&&read(U)==0x11&&read(U)==0x22,"RX FIFO preserved");write(U+16,1);settle();check(!ui.read(),"overrun clear");
            std::vector<unsigned char>d(4);check(host.read(U,d)==tlm::TLM_GENERIC_ERROR_RESPONSE,"empty RX error");
        }else if(mode=="backpressure"){
            check(sink.nb_write(0xee),"prefill");write(U,0x31);write(U,0x32);check(wr(U,0x33)==tlm::TLM_GENERIC_ERROR_RESPONSE,"full TX reject");wait(20,SC_NS);
            u.request_drain();check(!u.idle()&&!u.resume()&&!u.receive(9),"drain admission");unsigned char b;check(sink.nb_read(b)&&b==0xee,"prefill retained");settle();
            check(sink.nb_read(b)&&b==0x31,"first frame preserved");wait(10,SC_NS);settle();check(sink.nb_read(b)&&b==0x32,"second frame preserved");settle();
            if(!u.idle())wait(u.idle_event());check(u.resume()&&sink.num_available()==0,"drained without duplicates");
        }else if(mode=="reset_tx"){
            check(sink.nb_write(0xee),"prefill");write(U,0x41);wait(12,SC_NS);reset_all();unsigned char b;check(sink.nb_read(b)&&b==0xee,"external accepted data survives reset");
            wait(20,SC_NS);check(sink.num_available()==0,"cancelled frame escaped");write(U,0x42);wait(10,SC_NS);settle();check(sink.nb_read(b)&&b==0x42,"restart frame");
        }else if(mode=="reset_mmio"||mode=="drain_mmio"||mode=="capacity"){
            start.notify(SC_ZERO_TIME);auto response=raw(du,8,uart::reg::loopback,10);
            check(response==(mode=="reset_mmio"?tlm::TLM_GENERIC_ERROR_RESPONSE:tlm::TLM_OK_RESPONSE),"in-flight result");
            if(mode=="reset_mmio"){check(!u.resume(),"reset held");wait(rst.negedge_event());settle();check(u.resume()&&u2.resume()&&g.resume()&&g2.resume()&&controller.resume(),"resume reset");check(read(U+8)==0,"cancelled write committed");}
            else if(mode=="drain_mmio"){check(raw(du,8,0)==tlm::TLM_GENERIC_ERROR_RESPONSE,"drain closed");check(u.idle()&&u.resume()&&g.resume(),"drain completion");check(read(U+8)==1,"accepted write lost");}
            else check(read(U+8)==1,"capacity reject changed accepted value");
            check(worker_done,"worker incomplete");
        }else if(mode=="uart_collision"){
            write(U+8,uart::reg::irq_error);check(u.receive(1)&&u.receive(2),"fill RX");start.notify(SC_ZERO_TIME);
            check(raw(du,16,1)==tlm::TLM_OK_RESPONSE,"UART W1C");settle();check(ui.read()&&read(U+16)==1,"overrun must win same-time clear");write(U+16,1);settle();check(!ui.read(),"later error clear");
        }else if(mode=="gpio_mmio_reset"||mode=="gpio_mmio_drain"||mode=="gpio_capacity"){
            start.notify(SC_ZERO_TIME);auto status=raw(dg,4,0x55,10);
            check(status==(mode=="gpio_mmio_reset"?tlm::TLM_GENERIC_ERROR_RESPONSE:tlm::TLM_OK_RESPONSE),"GPIO in-flight result");
            if(mode=="gpio_mmio_reset"){check(!g.resume(),"GPIO reset held");wait(rst.negedge_event());settle();check(u.resume()&&u2.resume()&&g.resume()&&g2.resume()&&controller.resume(),"GPIO resume reset");check(read(G+4)==0,"cancelled GPIO write committed");}
            else if(mode=="gpio_mmio_drain"){check(raw(dg,4,0)==tlm::TLM_GENERIC_ERROR_RESPONSE,"GPIO drain closed");check(g.idle()&&g.resume()&&u.resume(),"GPIO drain completion");check(read(G+4)==0x55,"GPIO accepted write lost");}
            else check(read(G+4)==0x55,"GPIO rejected write leaked");
        }else if(mode=="gpio_edges"){
            write(I+4,2);write(G,0xf0);write(G+4,0xa5);settle();check(out.read()==0xa0&&oe.read()==0xf0,"GPIO output/direction");
            pins.write(0x101);settle();check(read(G+8)==1&&read(G+16)==1&&!gi.read(),"masked edge/width");write(G+12,1);settle();check(gi.read()&&irq.read(),"unmask IRQ");
            write(G+16,1);write(I,2);settle();check(!gi.read()&&!irq.read(),"W1C clear high input without relatch");pins.write(0);settle();pins.write(1);settle();check(gi.read(),"second rising edge");
        }else if(mode=="gpio_collision"){
            write(G+12,1);start.notify(SC_ZERO_TIME);check(raw(dg,16,1)==tlm::TLM_OK_RESPONSE,"W1C");settle();check(gi.read()&&read(G+16)==1,"same-time set must win");write(G+16,1);settle();check(!gi.read(),"later clear");
        }else if(mode=="gpio_reset"){
            write(G,0xf0);write(G+4,0xf0);write(G+12,1);pins.write(1);settle();check(gi.read(),"pre-reset edge");reset_all();check(read(G)==0&&read(G+4)==0&&read(G+16)==0&&!gi.read()&&oe.read()==0,"GPIO reset state");
            write(G+12,1);pins.write(0);settle();pins.write(1);settle();check(gi.read(),"GPIO restart");
        }else if(mode=="transport"){
            auto before=sc_time_stamp();check(raw(du,8,1,7)==tlm::TLM_OK_RESPONSE&&sc_time_stamp()-before==sc_time(8,SC_NS),"UART time owner");
            before=sc_time_stamp();check(raw(dg,4,0xa5,7)==tlm::TLM_OK_RESPONSE&&sc_time_stamp()-before==sc_time(8,SC_NS),"GPIO time owner");
            before=sc_time_stamp();check(read(G+4)==0xa5&&sc_time_stamp()-before==sc_time(2,SC_NS),"bus delay/little endian");
        }else if(mode=="errors"){
            check(wr(U,256)==tlm::TLM_COMMAND_ERROR_RESPONSE&&wr(U+8,16)==tlm::TLM_COMMAND_ERROR_RESPONSE&&wr(U+4,0)==tlm::TLM_COMMAND_ERROR_RESPONSE,"UART invalid fields");
            check(wr(G,256)==tlm::TLM_COMMAND_ERROR_RESPONSE&&wr(G+8,0)==tlm::TLM_COMMAND_ERROR_RESPONSE,"GPIO fields/RO");
            check(wr(U+20,0)==tlm::TLM_ADDRESS_ERROR_RESPONSE&&wr(G+20,0)==tlm::TLM_ADDRESS_ERROR_RESPONSE,"unknown offset");
            for(auto* socket:{&du,&dg}){unsigned char data[4]={},be=0;tlm::tlm_generic_payload tx;tx.set_write();tx.set_data_ptr(data);tx.set_data_length(4);tx.set_streaming_width(4);tx.set_byte_enable_ptr(&be);tx.set_byte_enable_length(1);sc_time d(3,SC_NS);
                (*socket)->b_transport(tx,d);check(tx.get_response_status()==tlm::TLM_BYTE_ENABLE_ERROR_RESPONSE&&d==sc_time(3,SC_NS),"byte enable");
                tx.set_byte_enable_ptr(nullptr);tx.set_address(1);(*socket)->b_transport(tx,d);check(tx.get_response_status()==tlm::TLM_ADDRESS_ERROR_RESPONSE,"unaligned");
                tx.set_address(0);tx.set_data_length(2);(*socket)->b_transport(tx,d);check(tx.get_response_status()==tlm::TLM_BURST_ERROR_RESPONSE,"length");
                tlm::tlm_dmi info;check(!(*socket)->get_direct_mem_ptr(tx,info)&&(*socket)->transport_dbg(tx)==0,"DMI/debug");}
        }else if(mode=="dual"){
            write(U+8,3);write(U2+8,3);write(U,0x51);write(U2,0x62);wait(20,SC_NS);settle();check(read(U)==0x51&&read(U2)==0x62,"UART isolation");
            write(G,0xff);write(G2,0xf);write(G+4,0xa5);write(G2+4,3);settle();check(out.read()==0xa5&&out2.read()==3&&oe.read()==255&&oe2.read()==15,"GPIO isolation/config");
        }else throw std::runtime_error("unknown case");
        u.request_drain();u2.request_drain();g.request_drain();g2.request_drain();controller.request_drain();host.request_drain();bus.request_drain();
        check(u.idle()&&u2.idle()&&g.idle()&&g2.idle()&&controller.idle()&&host.idle()&&bus.idle(),"final drain");done=true;
    }
};
int sc_main(int argc,char**argv){std::string mode=argc>1?argv[1]:"uart_tx";try{
    if(mode=="invalid_uart"){uart::Config c;c.tx_depth=0;try{uart::Model m("bad_uart",c);}catch(const std::invalid_argument&e){return std::string(e.what()).find("depths must")!=std::string::npos?0:1;}return 1;}
    if(mode=="invalid_gpio"){gpio::Config c;c.width=33;try{gpio::Model m("bad_gpio",c);}catch(const std::invalid_argument&e){return std::string(e.what()).find("width must")!=std::string::npos?0:1;}return 1;}
    if(mode=="unbound_uart"){uart::Model m("unbound_uart");try{sc_start(SC_ZERO_TIME);}catch(const sc_report&e){return std::string(e.what()).find("unbound_uart.")!=std::string::npos?0:1;}return 1;}
    if(mode=="unbound_gpio"){gpio::Model m("unbound_gpio");try{sc_start(SC_ZERO_TIME);}catch(const sc_report&e){return std::string(e.what()).find("unbound_gpio.")!=std::string::npos?0:1;}return 1;}
    Bench b("bench",mode);sc_start(1,SC_US);check(b.done&&b.worker_done,"watchdog/incomplete");return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
