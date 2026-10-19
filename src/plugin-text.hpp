#pragma once

#include <obs-module.h>

#include <QString>

// Localized UI text from data/locale/<lang>.ini (falls back to en-US, then
// to the key itself).
inline QString ObsText(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}
