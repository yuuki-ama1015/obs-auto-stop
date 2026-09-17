#include "stop-controller.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>

void StopController::requestStop(const char *reason)
{
	if (stop_requested_) {
		return;
	}

	stop_requested_ = true;
	blog(LOG_INFO, "OBS Auto Stop: requesting recording stop (%s)",
	     reason ? reason : "unspecified");

	if (obs_frontend_recording_active()) {
		obs_frontend_recording_stop();
	}
}
