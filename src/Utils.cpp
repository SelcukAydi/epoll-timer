#include <Utils.hpp>
#include <cstdio>
#include <cstring>
#include <sys/utsname.h>

namespace sia::epoll::timer
{

std::int64_t determineJiffiesUnit()
{
    struct utsname uname_info;

    if (uname(&uname_info) != 0)
    {
        return -1;
    }

    char config_path[256];
    snprintf(config_path, sizeof(config_path), "/boot/config-%s", uname_info.release);
    FILE* file = fopen(config_path, "r");

    if (file == nullptr)
    {
        return -1;
    }

    std::int64_t hertz = -1;
    char buf[1024];

    while (fgets(buf, sizeof(buf), file) != nullptr)
    {
        if (strcmp(buf, "CONFIG_NO_HZ=y\n") == 0)
        {
            return -1;
        }
        else if (strcmp(buf, "CONFIG_HZ=1000\n") == 0)
        {
            hertz = 1000;
        }
        else if (strcmp(buf, "CONFIG_HZ=300\n") == 0)
        {
            hertz = 300;
        }
        else if (strcmp(buf, "CONFIG_HZ=250\n") == 0)
        {
            hertz = 250;
        }
        else if (strcmp(buf, "CONFIG_HZ=100\n") == 0)
        {
            hertz = 100;
        }
    }

    return hertz;
}
}  // namespace sia::epoll::timer