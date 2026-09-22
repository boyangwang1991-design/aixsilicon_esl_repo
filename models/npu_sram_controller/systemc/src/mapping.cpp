#include "npu_sram_controller/mapping.hpp"
#include <aix/esl/region_mapper.hpp>
#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
namespace aix::esl::npu_sram_controller {
static bool pow2(uint64_t x){return x && !(x&(x-1));}
static ::aix::esl::RegionMapper region_mapper(const Config& config) {
    std::vector<::aix::esl::RegionMapper::Region> regions;
    for (const auto& r : config.regions)
        regions.push_back({r.base, r.length, r.local_base, r.banks, r.stripe,
                          r.policy == "xor" ? ::aix::esl::AddressMapper::Policy::xor_interleaved :
                                              ::aix::esl::AddressMapper::Policy::interleaved,
                          r.shift, r.rotation});
    return {config.capacity, config.banks, config.capacity / config.banks, config.groups, std::move(regions)};
}
Mapper::Mapper(const Config& c) : c_(c) {
    c_.validate();
    if (c_.mapping == "region") regions_ = std::make_shared<const ::aix::esl::RegionMapper>(region_mapper(c_));
}
void Config::validate() const {
    auto require=[](bool ok,const char* why){if(!ok)throw std::invalid_argument(why);};
    require(ports>0&&ports<=8&&pow2(banks)&&banks<=64,"ports/banks");
    require(pow2(word_bytes)&&word_bytes>=8&&word_bytes<=128,"word width");
    require(pow2(stripe_bytes)&&stripe_bytes>=8&&stripe_bytes<=2048,"stripe");
    require(pow2(groups)&&groups<=banks&&banks%groups==0&&ports%groups==0,"groups");
    require(capacity>0&&capacity%(banks*word_bytes)==0&&capacity%(banks*stripe_bytes)==0,"capacity");
    require(mapping=="modulo"||mapping=="xor"||mapping=="contiguous"||mapping=="region","mapping");
    require(topology=="flat"||topology=="hierarchical"||topology=="ideal","topology");
    require(queue=="fifo"||queue=="voq"||queue=="group_voq","queue policy");
    require(arbitration=="rr"||arbitration=="age"||arbitration=="weighted"||arbitration=="read_first","arbitration");
    require(xor_shift<=16&&group_first<=1&&local_xor<=1&&dual_port<=1&&dual_ingress<=dual_port&&full_data<=1,"flags");
    require(read_latency&&write_latency&&bank_ii&&outstanding&&ids&&ids<=16,"timing/outstanding/IDs");
    require(ingress_entries>=128/std::min(word_bytes,stripe_bytes)&&bank_entries&&rob_beats&&w_beats,"queue capacity");
    require(completion_entries&&rmw_contexts&&lanes&&lanes<=16&&return_bytes>=8&&remote_bytes>=8,"ports/buffers");
    require(link_entries&&link_buffer_bytes>=word_bytes&&link_latency&&credit_delay&&matching_rounds&&age_guard,"links");
    require(ecc_bytes==0||ecc_bytes==8||ecc_bytes==16,"ECC codeword");
    require(!ecc_bytes||(word_bytes%ecc_bytes==0&&stripe_bytes%ecc_bytes==0),"ECC alignment");
    require(ecc_lanes&&ecc_ii&&ecc_latency&&ecc_group<=1&&macro_word_write<=1&&cycle_limit,"ECC/resources");
    require((mapping=="region")==!regions.empty()&&regions.size()<=8,"region table");
    for(auto& r:regions){
        require(r.length&&pow2(r.banks.size())&&pow2(r.stripe)&&r.stripe>=8&&r.stripe<=2048,"region shape");
        require(r.base<=capacity&&r.length<=capacity-r.base&&r.base%r.stripe==0&&r.length%(r.banks.size()*r.stripe)==0,"region range");
        require(r.policy=="modulo"||r.policy=="xor","region policy");
        require(r.shift<=16&&r.rotation<r.banks.size()&&r.local_base%word_bytes==0,"region layout");
        require(!ecc_bytes||r.stripe%ecc_bytes==0,"region ECC alignment");
    }
    if (!regions.empty()) (void)region_mapper(*this); // public bounds/subset/alias contract
}
Config Config::read(const std::string& path){
    Config c;std::ifstream in(path);if(!in)throw std::runtime_error("config open");
    std::string line;std::set<std::string> seen;
    while(std::getline(in,line)){if(line.empty()||line[0]=='#')continue;auto pos=line.find('=');if(pos==std::string::npos)throw std::invalid_argument("config key=value");
        auto k=line.substr(0,pos),v=line.substr(pos+1);if(k!="region"&&!seen.insert(k).second)throw std::invalid_argument("duplicate config key");
        if(k=="region"){Region r;std::istringstream s(v);std::string bs;if(!(s>>r.base>>r.length>>r.local_base>>r.policy>>r.stripe>>r.shift>>r.rotation>>bs))throw std::invalid_argument("region syntax");std::replace(bs.begin(),bs.end(),',',' ');std::istringstream b(bs);unsigned x;while(b>>x)r.banks.push_back(x);c.regions.push_back(r);continue;}
#define STR(x) if(k==#x){c.x=v;continue;}
        STR(mapping) STR(topology) STR(queue) STR(arbitration)
#undef STR
        if(v.empty()||v.find_first_not_of("0123456789")!=std::string::npos)throw std::invalid_argument("integer config value");
        auto n=std::stoull(v);if(k=="capacity"){c.capacity=n;continue;}if(k=="cycle_limit"){c.cycle_limit=n;continue;}
        if(n>10000000)throw std::invalid_argument("config integer range");
#define NUM(x) if(k==#x){c.x=static_cast<unsigned>(n);continue;}
        NUM(ports) NUM(banks) NUM(word_bytes) NUM(stripe_bytes) NUM(groups) NUM(xor_shift) NUM(group_first) NUM(local_xor)
        NUM(dual_port) NUM(dual_ingress) NUM(full_data) NUM(read_latency) NUM(write_latency) NUM(bank_ii)
        NUM(outstanding) NUM(ids) NUM(ingress_entries) NUM(bank_entries) NUM(rob_beats) NUM(w_beats)
        NUM(completion_entries) NUM(rmw_contexts) NUM(lanes) NUM(return_bytes) NUM(remote_bytes) NUM(link_entries)
        NUM(link_buffer_bytes) NUM(link_latency) NUM(credit_delay) NUM(matching_rounds) NUM(age_guard)
        NUM(max_read_grants) NUM(ecc_bytes) NUM(ecc_lanes) NUM(ecc_ii) NUM(ecc_latency) NUM(ecc_group)
        NUM(macro_word_write) NUM(scrub_interval) NUM(correction_latency) NUM(trace_limit)
#undef NUM
        throw std::invalid_argument("unknown config field: "+k);
    }c.validate();return c;
}
Location Mapper::map(uint64_t a)const{
    if(a>=c_.capacity)throw std::out_of_range("address");
    if(c_.mapping=="region"){
        const auto location = regions_->map(a);
        return {location.bank, location.local};
    }
    if(c_.mapping=="contiguous")return {unsigned(a/(c_.capacity/c_.banks)),a%(c_.capacity/c_.banks)};
    auto q=a/c_.stripe_bytes,h=q/c_.banks;unsigned b=q%c_.banks,B=c_.banks/c_.groups;
    if(c_.mapping=="xor"){
        unsigned mask=c_.local_xor?B-1:c_.banks-1;
        if(c_.local_xor&&c_.group_first)b=(b%c_.groups)+c_.groups*((b/c_.groups)^((h>>c_.xor_shift)&mask));
        else b^=(h>>c_.xor_shift)&mask;
    }
    if(c_.group_first)b=(b%c_.groups)*B+b/c_.groups;
    return {b,h*c_.stripe_bytes+a%c_.stripe_bytes};
}
uint64_t Mapper::inverse(unsigned b,uint64_t local)const{
    if(b>=c_.banks||local>=c_.capacity/c_.banks)throw std::out_of_range("physical address");
    if(c_.mapping=="region") return regions_->inverse(b, local);
    if(c_.mapping=="contiguous")return b*(c_.capacity/c_.banks)+local;
    auto h=local/c_.stripe_bytes;unsigned B=c_.banks/c_.groups;
    if(c_.group_first)b=(b%B)*c_.groups+b/B;
    if(c_.mapping=="xor"){
        unsigned mask=c_.local_xor?B-1:c_.banks-1;
        if(c_.local_xor&&c_.group_first)b=(b%c_.groups)+c_.groups*((b/c_.groups)^((h>>c_.xor_shift)&mask));
        else b^=(h>>c_.xor_shift)&mask;
    }
    return (h*c_.banks+b)*c_.stripe_bytes+local%c_.stripe_bytes;
}
std::vector<Slice> Mapper::split(uint64_t a,unsigned bytes)const{
    std::vector<Slice> slices;std::map<std::pair<unsigned,uint64_t>,size_t> index;
    for(unsigned i=0;i<bytes;++i){auto p=map(a+i);auto key=std::make_pair(p.bank,p.local/c_.word_bytes);
        auto it=index.find(key);if(it==index.end()){index[key]=slices.size();slices.push_back({key.first,key.second,{},{}});it=index.find(key);}
        slices[it->second].offsets.push_back(i);slices[it->second].lanes.push_back(p.local%c_.word_bytes);
    }return slices;
}
}
