#!/bin/bash

cat > monitors.h << EOF
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
EOF

(
echo "#include \"monitors.h\""
echo
echo "// Initialize the monitor information"

# Parse xrandr output and generate the vector initialization
xrandr --query | awk '/ connected/ {
    split($3, res, "+");
    split(res[1], dims, "x");
    x_offset = res[2];
    y_offset = res[3];
    width = dims[1];
    height = dims[2];
    print x_offset, y_offset, width, height;
}' | sort -n | awk '
BEGIN { print "std::vector<MonitorInfo> monitors = {"; }
{
    position = (NR == 1) ? "Left" : (NR == 2) ? "Center" : "Right";
    printf("    {%s, %s, %s, %s, \"%s\"},\n", $1, $2, $3, $4, position);
}
END { print "};"; }'
) > monitors.cpp