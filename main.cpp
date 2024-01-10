#include <cstdlib>
#include <glog/logging.h>
#include "inc/winm.h"

inline void errorStackPrinter(const char *str, int size) {
    LOG(ERROR) << ::std::string(str, size);
}

int main(int argc, char **argv) {
    FLAGS_colorlogtostderr = true;
    ::google::InstallFailureSignalHandler(); // 配置安装程序崩溃失败信号处理器
    ::google::InstallFailureWriter(
        errorStackPrinter); // 安装配置程序失败信号的信息打印过程，设置回调函数
    ::google::InitGoogleLogging(argv[0]);

    ::std::unique_ptr<x11::WindowManager> window_manager =
        x11::WindowManager::getInstance(); // CLI参数留空，使用DISPLAY环境变量
    if (!window_manager) {
        LOG(ERROR) << "Failed to initialize window manager.";
        return EXIT_FAILURE;
    }

    window_manager->run();

    return EXIT_SUCCESS;
}