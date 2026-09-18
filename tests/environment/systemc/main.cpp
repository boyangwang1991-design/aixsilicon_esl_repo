// 最小 SystemC 用例（K13 esl-environment-setup 验证）
// Use the local CMake entry and SystemC::systemc (configured C++ ABI).
#include <systemc.h>

SC_MODULE(Counter) {
    sc_in_clk clk;
    sc_out<int> count;
    int val = 0;
    SC_CTOR(Counter) {
        SC_METHOD(tick);
        sensitive << clk.pos();
        dont_initialize();  // Count clock edges only, not the initialization invocation.
    }
    void tick() { count.write(++val); }
};

int sc_main(int argc, char* argv[]) {
    sc_clock clk("clk", 1, SC_NS);
    sc_signal<int> cnt;
    Counter c("c");
    c.clk(clk);
    c.count(cnt);
    sc_start(10, SC_NS);
    std::cout << "SystemC OK: count=" << cnt.read() << " (期望 10)" << std::endl;
    return (cnt.read() == 10) ? 0 : 1;
}
