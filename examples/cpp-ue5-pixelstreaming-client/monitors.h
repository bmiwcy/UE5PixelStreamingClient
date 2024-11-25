#ifndef MONITORS_H
#define MONITORS_H

#include <vector>
#include <string>

// Define the MonitorInfo struct
struct MonitorInfo {
    int x;        // X coordinate of the monitor's top-left corner
    int y;        // Y coordinate of the monitor's top-left corner
    int width;    // Width of the monitor
    int height;   // Height of the monitor
    std::string position; // Relative position: Left, Center, or Right
};

// Declare the monitors vector
extern std::vector<MonitorInfo> monitors;

#endif // MONITORS_H
