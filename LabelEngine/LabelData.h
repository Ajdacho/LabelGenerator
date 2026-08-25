#pragma once
#include <string>
#include <vector>

struct LabelData {
    std::string customer;
    std::string color;
    std::string rawComposition;
    std::string careSymbols;
    double widthMm = 30.0;
    double heightMm = 90.0;
};