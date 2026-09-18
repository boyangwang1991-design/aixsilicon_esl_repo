// {{name}} 生成物测试（compute_cpp 模板）：独立 sc_main，验证 SystemC 链接可用。
// 运行：LD_LIBRARY_PATH=$SYSTEMC_HOME/lib-linux64 ./build/test_{{name}}
#include <systemc.h>
#include <cassert>

int sc_main(int argc, char* argv[]) {
    sc_clock clk("clk", 1, SC_NS);
    sc_signal<int> sig;
    sig = 0;
    sc_start(1, SC_NS);
    // 编译+链接成功即通过；此处验证 SystemC 内核可运行
    std::cout << "{{name}} test compile+link+run OK" << std::endl;
    return 0;
}
