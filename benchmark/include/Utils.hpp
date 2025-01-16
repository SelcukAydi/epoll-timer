#pragma once

#include <cstdint>
#include <random>

static int generateRandomNumber(std::uint64_t limit)
{
    if(limit != 0)
    {
        return limit;
    }

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(1, 500);
    return dist(gen);
}