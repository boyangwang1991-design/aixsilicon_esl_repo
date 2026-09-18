// {{name}}：compute_cpp 模板生成的 SystemC 薄骨架。
// 边界：sc_module 只写业务 hook（transform）；时间/事件由 SystemC 内核提供，
// 不复制 esl_repo common 实现。生成后须能 cmake 构建并运行。
#include <systemc.h>

SC_MODULE({{name}}) {
    // 输入/输出端口（业务 hook 可替换）
    sc_in<bool> clk;
    sc_out<int> result;

    SC_CTOR({{name}}) {
        SC_METHOD(tick);
        sensitive << clk.pos();
    }

    // 业务功能 hook：按需替换为真实数据变换
    int transform(int x) { return x; }

    void tick() {
        result.write(transform(last_clk));
    }
    int last_clk = 0;
};

int sc_main(int argc, char* argv[]) {
    sc_clock clk("clk", 1, SC_NS);
    sc_signal<int> res;
    {{name}} m("m");
    m.clk(clk);
    m.result(res);
    sc_start(3, SC_NS);
    std::cout << "{{name}} SystemC OK" << std::endl;
    return 0;
}
