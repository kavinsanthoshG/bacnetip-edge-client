#include <stdio.h>
#include "discovery.h"
#include "watchlist.h"

int main(int argc, char *argv[]) {
    // Pass through command-line args to discovery module
    discovery_run_cli(argc, argv);
    return device_watchlist( argc,  argv);

}