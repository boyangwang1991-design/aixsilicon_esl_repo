#pragma once
#include <cstdint>
#include <stdexcept>
namespace aix::esl {
class AddressMapper {
public:
    enum class Policy { contiguous, interleaved, xor_interleaved };
    struct Location { unsigned bank, group; uint64_t local, row; };
    AddressMapper(uint64_t capacity, unsigned banks, unsigned stripe, unsigned groups=1,
                  Policy policy=Policy::interleaved, unsigned xor_shift=0)
        : capacity_(capacity), banks_(banks), stripe_(stripe), groups_(groups), policy_(policy), shift_(xor_shift) {
        if (!capacity || !banks || !stripe || !groups || banks%groups ||
            capacity%(uint64_t(banks)*stripe) || xor_shift>=64 ||
            (policy==Policy::xor_interleaved && (banks&(banks-1))))
            throw std::invalid_argument("mapping capacity/banks/stripe/groups/XOR");
    }
    Location map(uint64_t address) const {
        if (address>=capacity_) throw std::out_of_range("logical address");
        unsigned bank; uint64_t local;
        if (policy_==Policy::contiguous) {bank=address/(capacity_/banks_);local=address%(capacity_/banks_);}
        else {
            auto stripe=address/stripe_, row=stripe/banks_;
            bank=stripe%banks_;
            if (policy_==Policy::xor_interleaved) bank^=(row>>shift_)&(banks_-1);
            local=row*stripe_+address%stripe_;
        }
        return {bank,bank/(banks_/groups_),local,local/stripe_};
    }
    uint64_t inverse(unsigned bank, uint64_t local) const {
        if (bank>=banks_ || local>=capacity_/banks_) throw std::out_of_range("physical address");
        if (policy_==Policy::contiguous) return bank*(capacity_/banks_)+local;
        auto row=local/stripe_;
        if (policy_==Policy::xor_interleaved) bank^=(row>>shift_)&(banks_-1);
        return (row*banks_+bank)*stripe_+local%stripe_;
    }
private:
    uint64_t capacity_; unsigned banks_,stripe_,groups_; Policy policy_; unsigned shift_;
};
}
