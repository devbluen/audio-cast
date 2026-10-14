/*
 *  AudioCast for open.mp and SA-MP
 *  Copyright (c) 2026 devbluen
 */

#include "samp-pawn.hpp"
#include "natives.hpp"
#include <algorithm>

const StaticArray<void*, NUM_AMX_FUNCS>& SampPawnComponent::getAmxFunctions() const
{
	static const StaticArray<void*, NUM_AMX_FUNCS> functions{};
	return functions;
}

SampPawnScript* SampPawnComponent::findSampScript(AMX* amx) const noexcept
{
	for (const auto& script : scripts_)
	{
		if (script && script->GetAMX() == amx)
		{
			return script.get();
		}
	}
	return nullptr;
}

IPawnScript* SampPawnComponent::getScript(AMX* amx)
{
	return findSampScript(amx);
}

const IPawnScript* SampPawnComponent::getScript(AMX* amx) const
{
	return findSampScript(amx);
}

IPawnScript* SampPawnComponent::mainScript()
{
	return scripts_.empty() ? nullptr : scripts_.front().get();
}

const Span<IPawnScript*> SampPawnComponent::sideScripts()
{
	return Span<IPawnScript*>(sideScripts_.data(), sideScripts_.size());
}

void SampPawnComponent::rebuildSideScripts() noexcept
{
	sideScripts_.clear();
	if (scripts_.size() < 2)
	{
		return;
	}

	sideScripts_.reserve(scripts_.size() - 1);
	for (std::size_t index = 1; index < scripts_.size(); ++index)
	{
		sideScripts_.push_back(scripts_[index].get());
	}
}

void SampPawnComponent::load(AMX* amx)
{
	if (!amx || findSampScript(amx) || !SampAmx::available())
	{
		return;
	}

	auto script = std::make_unique<SampPawnScript>(amx, nextScriptId_++);
	// amx_Register reports AMX_ERR_NOTFOUND whenever the script also uses
	// natives from plugins that register after this one (sscanf, streamer...).
	// Our natives are registered anyway, so only other errors drop the script;
	// otherwise the gamemode would never receive callbacks or string arguments.
	const int error = RegisterAudioCastNatives(*script);
	if (error != AMX_ERR_NONE && error != AMX_ERR_NOTFOUND)
	{
		return;
	}

	scripts_.push_back(std::move(script));
	rebuildSideScripts();
}

void SampPawnComponent::unload(AMX* amx)
{
	const auto it = std::find_if(scripts_.begin(), scripts_.end(), [amx](const auto& script)
	{
		return script && script->GetAMX() == amx;
	});
	if (it == scripts_.end())
	{
		return;
	}

	scripts_.erase(it);
	rebuildSideScripts();
}

void SampPawnComponent::clear()
{
	scripts_.clear();
	sideScripts_.clear();
}
