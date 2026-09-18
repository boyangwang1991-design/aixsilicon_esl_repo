#pragma once
#include <array>
#include <cstdint>
#include <map>
#include <stdexcept>
namespace aix::esl {
// Sparse zero-filled functional backing store; pages are allocated only by writes.
class SparseStore {
public:
    explicit SparseStore(uint64_t capacity):capacity_(capacity){if(!capacity)throw std::invalid_argument("sparse capacity");}
    unsigned char read(uint64_t address)const{
        bounds(address);auto it=pages_.find(address/4096);return it==pages_.end()?0:it->second[address%4096];
    }
    void write(uint64_t address,unsigned char value){bounds(address);pages_[address/4096][address%4096]=value;}
    void clear(){pages_.clear();}
    size_t allocated_pages()const{return pages_.size();}
private:uint64_t capacity_;std::map<uint64_t,std::array<unsigned char,4096>> pages_;
    void bounds(uint64_t a)const{if(a>=capacity_)throw std::out_of_range("sparse address");}
};
}
