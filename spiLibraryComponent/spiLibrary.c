#include "legato.h"
#include "spiLibrary.h"

void spiLib_SayHello(const char* name)
{
    LE_INFO("Hello %s", name);
}

COMPONENT_INIT
{
    LE_INFO("spiLibraryComponent initializing");
}
