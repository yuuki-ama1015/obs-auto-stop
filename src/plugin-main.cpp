#include <obs-module.h>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-auto-stop", "en-US")

bool obs_module_load(void)
{
    blog(LOG_INFO, "OBS Auto Stop plugin loaded");
    return true;
}

void obs_module_unload(void)
{
    blog(LOG_INFO, "OBS Auto Stop plugin unloaded");
}
