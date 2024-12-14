#include "inc/winm.h"
#include <glog/logging.h>

inline void errorStackPrinter(const char *str, size_t size)
{
    LOG(ERROR) << std::string(str, size);
}

int main(int argc, char **argv)
{
    FLAGS_colorlogtostderr = true;
    google::InstallFailureSignalHandler();
    google::InstallFailureWriter(errorStackPrinter);
    google::InitGoogleLogging(argv[0]);

    auto window_manager = x11::WindowManager::instance(); // CLI参数留空，使用DISPLAY环境变量
    if (!window_manager) {
        LOG(ERROR) << "Failed to initialize window manager.";
        return EXIT_FAILURE;
    }

    window_manager->run();

    return EXIT_SUCCESS;
}