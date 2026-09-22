#pragma once
#include <tlm>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>
namespace aix::esl {
struct TransactionMetadata : tlm::tlm_extension<TransactionMetadata> {
    uint64_t id=0,parent=0,epoch=0,issued_tick=0;
    unsigned source=0,qos=0;
    tlm::tlm_extension_base* clone() const override {return new TransactionMetadata(*this);}
    void copy_from(const tlm::tlm_extension_base& other) override {
        *this=dynamic_cast<const TransactionMetadata&>(other);
    }
};
struct Transaction {
    uint64_t address=0;
    bool write=false;
    std::vector<unsigned char> data,mask;
    TransactionMetadata metadata;
    void validate() const {
        if (data.empty() || data.size()>std::numeric_limits<unsigned>::max() ||
            data.size()-1>std::numeric_limits<uint64_t>::max()-address ||
            mask.size()>std::numeric_limits<unsigned>::max())
            throw std::invalid_argument("transaction range/length");
        for(auto byte:mask) if(byte!=0 && byte!=0xff) throw std::invalid_argument("transaction mask");
    }
};
// Scoped synchronous adapter. It restores the caller's metadata extension.
// The payload and transaction must not be retained by an asynchronous target.
class TransactionBinding {
public:
    TransactionBinding(Transaction& transaction,tlm::tlm_generic_payload& payload):p_(payload) {
        transaction.validate();
        old_=p_.get_extension<TransactionMetadata>();
        p_.set_command(transaction.write?tlm::TLM_WRITE_COMMAND:tlm::TLM_READ_COMMAND);
        p_.set_address(transaction.address);p_.set_data_ptr(transaction.data.data());
        p_.set_data_length(static_cast<unsigned>(transaction.data.size()));
        p_.set_streaming_width(static_cast<unsigned>(transaction.data.size()));
        p_.set_byte_enable_ptr(transaction.mask.empty()?nullptr:transaction.mask.data());
        p_.set_byte_enable_length(static_cast<unsigned>(transaction.mask.size()));
        p_.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);p_.set_dmi_allowed(false);
        p_.set_extension(&transaction.metadata);
    }
    ~TransactionBinding(){p_.clear_extension<TransactionMetadata>();if(old_)p_.set_extension(old_);}
    TransactionBinding(const TransactionBinding&)=delete;
    TransactionBinding& operator=(const TransactionBinding&)=delete;
private:tlm::tlm_generic_payload& p_;TransactionMetadata* old_;
};
struct Fragment {uint64_t address;size_t offset,length;};
inline std::vector<Fragment> split_transaction(uint64_t address,size_t bytes,unsigned boundary,unsigned max_bytes) {
    if(!bytes||!boundary||!max_bytes||bytes-1>std::numeric_limits<uint64_t>::max()-address)
        throw std::invalid_argument("fragment range/geometry");
    std::vector<Fragment> result;
    for(size_t offset=0;offset<bytes;){auto current=address+offset;
        auto length=std::min<size_t>(bytes-offset,std::min<uint64_t>(max_bytes,boundary-current%boundary));
        result.push_back({current,offset,length});offset+=length;}
    return result;
}
// Each fragment completes once, including errors. No early completion or silent retry.
class CompletionAssembly {
public:
    explicit CompletionAssembly(size_t fragments):done_(fragments,false){if(!fragments)throw std::invalid_argument("empty assembly");}
    void complete(size_t index,bool success){
        if(index>=done_.size()||done_[index])throw std::logic_error("unknown/duplicate fragment");
        done_[index]=true;++count_;success_&=success;
    }
    bool ready()const{return count_==done_.size();}
    bool success()const{if(!ready())throw std::logic_error("incomplete assembly");return success_;}
private:std::vector<bool> done_;size_t count_=0;bool success_=true;
};
}
