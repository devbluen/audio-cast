/*
 *  AudioCast for open.mp and SA-MP
 *  Copyright (c) 2026 devbluen
 */

#include "audiocast.hpp"
#include "log.hpp"
#include "natives.hpp"
#include "samp-config.hpp"
#include "text.hpp"
#include "version.hpp"
#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdlib>
#include <vector>

AudioCastComponent* AudioCastComponent::instance_ = nullptr;

namespace
{
constexpr int MAX_PLAYERS = 1000;

std::string upper(std::string value)
{
	std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
	return value;
}

bool usableHost(const std::string& host)
{
	return !host.empty() && host != "0.0.0.0" && host != "::";
}
}

AudioCastComponent* AudioCastComponent::getInstance()
{
	if (!instance_) instance_ = new AudioCastComponent();
	return instance_;
}

SemanticVersion AudioCastComponent::componentVersion() const
{
	return SemanticVersion(AUDIOCAST_VERSION_MAJOR, AUDIOCAST_VERSION_MINOR, AUDIOCAST_VERSION_PATCH);
}

// ---------------------------------------------------------------------------
// open.mp component
// ---------------------------------------------------------------------------

void AudioCastComponent::onLoad(ICore* core)
{
	sampMode_ = false;
	core_ = core;
	AudioCastLogInit(core_);
	if (!core_) return;
	core_->getEventDispatcher().addEventHandler(this);
	core_->getPlayers().getPlayerConnectDispatcher().addEventHandler(this);
}

void AudioCastComponent::onInit(IComponentList* components)
{
	if (!components) return;
	pawn_ = components->queryComponent<IPawnComponent>();
	if (pawn_)
	{
		pawn_->getEventDispatcher().addEventHandler(this);
	}
	else
	{
		AudioCastLogWarning("the Pawn component is not loaded; the natives are unavailable");
	}
}

void AudioCastComponent::onReady()
{
	startServices();
}

void AudioCastComponent::onFree(IComponent* component)
{
	if (pawn_ && component == pawn_)
	{
		pawn_->getEventDispatcher().removeEventHandler(this);
		pawn_ = nullptr;
	}
}

void AudioCastComponent::provideConfiguration(ILogger&, IEarlyConfig& config, bool defaults)
{
	// Only written when generating a fresh config.json; existing files keep
	// working without any audiocast section (see configValue).
	if (!defaults) return;
	const AudioCastConfig fallback;
	config.setString("audiocast.bind", fallback.bindAddress);
	config.setInt("audiocast.port", fallback.port);
	config.setString("audiocast.public_host", "");
	config.setInt("audiocast.public_port", 0);
	config.setInt("audiocast.burst_ms", fallback.burstMs);
	config.setInt("audiocast.bitrate", fallback.bitrateKbps);
	config.setBool("audiocast.normalize", fallback.normalize);
	config.setBool("audiocast.allow_public", fallback.allowPublic);
	config.setBool("audiocast.utf8", fallback.utf8);
	config.setInt("audiocast.metadata_codepage", fallback.metadataCodepage);
	config.setBool("audiocast.log_songs", fallback.logSongs);
	config.setString("audiocast.ytdlp", fallback.ytdlpPath);
	config.setString("audiocast.js_runtime", fallback.jsRuntime);
	config.setString("audiocast.cookies", "");
	config.setString("audiocast.data_dir", fallback.dataDir);
	config.setInt("audiocast.cache_minutes", fallback.cacheMinutes);
	config.setInt("audiocast.max_duration", fallback.maxDurationSec);
	config.setInt("audiocast.download_timeout", fallback.downloadTimeoutSec);
	config.setInt("audiocast.workers", fallback.workers);
	config.setInt("audiocast.max_listeners", fallback.maxListeners);
}

void AudioCastComponent::free()
{
	stopServices();
	if (core_)
	{
		core_->getEventDispatcher().removeEventHandler(this);
		core_->getPlayers().getPlayerConnectDispatcher().removeEventHandler(this);
	}
	if (pawn_ && !sampMode_) pawn_->getEventDispatcher().removeEventHandler(this);
	if (sampMode_) sampPawn_.clear();
	pawn_ = nullptr;
	core_ = nullptr;
	instance_ = nullptr;
	delete this;
}

void AudioCastComponent::reset()
{
	// GMX: the gamemode's radios are destroyed when its script unloads.
}

void AudioCastComponent::onTick(Microseconds, TimePoint)
{
	tick();
}

void AudioCastComponent::onAmxLoad(IPawnScript& script)
{
	RegisterAudioCastNatives(script);
}

void AudioCastComponent::onAmxUnload(IPawnScript& script)
{
	destroyRadiosOwnedBy(script.GetAMX());
}

void AudioCastComponent::onPlayerDisconnect(IPlayer& player, PeerDisconnectReason)
{
	detachPlayer(player.getID());
	playerVolumes_.erase(player.getID());
}

// ---------------------------------------------------------------------------
// SA-MP plugin
// ---------------------------------------------------------------------------

void AudioCastComponent::sampLoad()
{
	sampMode_ = true;
	pawn_ = &sampPawn_;
	AudioCastLogInit(nullptr);
	startServices();
}

void AudioCastComponent::sampUnload()
{
	stopServices();
	sampPawn_.clear();
	pawn_ = nullptr;
	instance_ = nullptr;
	delete this;
}

void AudioCastComponent::sampAmxLoad(AMX* amx)
{
	sampPawn_.load(amx);
}

void AudioCastComponent::sampAmxUnload(AMX* amx)
{
	destroyRadiosOwnedBy(amx);
	sampPawn_.unload(amx);
}

void AudioCastComponent::sampTick()
{
	tick();
}

// ---------------------------------------------------------------------------
// Services
// ---------------------------------------------------------------------------

std::string AudioCastComponent::configValue(const std::string& key) const
{
	// 1. Environment: audiocast_port -> AUDIOCAST_PORT
	if (const char* value = std::getenv(upper(key).c_str()); value && *value) return value;

	// 2. open.mp config.json: "audiocast_port" or { "audiocast": { "port" } }
	if (core_)
	{
		IConfig& config = core_->getConfig();
		const std::string dotted = "audiocast." + key.substr(std::char_traits<char>::length("audiocast_"));
		for (const std::string& name : { key, dotted })
		{
			switch (config.getType(name))
			{
			case ConfigOptionType_String:
			{
				const StringView value = config.getString(name);
				if (!value.empty()) return std::string(value.data(), value.length());
				break;
			}
			case ConfigOptionType_Int:
				if (const int* value = config.getInt(name)) return std::to_string(*value);
				break;
			case ConfigOptionType_Bool:
				if (const bool* value = config.getBool(name)) return *value ? "1" : "0";
				break;
			default:
				break;
			}
		}
	}

	// 3. server.cfg (SA-MP, or open.mp running a legacy config)
	return readSampConfigValue(key);
}

void AudioCastComponent::startServices()
{
	if (started_) return;
	started_ = true;

	config_ = LoadAudioCastConfig([this](const std::string& key) { return configValue(key); });

	publicHost_ = config_.publicHost;
	if (!usableHost(publicHost_) && core_)
	{
		const StringView publicAddress = core_->getConfig().getString("network.public_addr");
		publicHost_.assign(publicAddress.data(), publicAddress.length());
		if (!usableHost(publicHost_))
		{
			const StringView bind = core_->getConfig().getString("network.bind");
			publicHost_.assign(bind.data(), bind.length());
		}
	}
	if (!usableHost(publicHost_)) publicHost_ = readSampConfigValue("bind");
	if (!usableHost(publicHost_)) publicHost_ = usableHost(config_.bindAddress) ? config_.bindAddress : std::string();
	if (!usableHost(publicHost_))
	{
		publicHost_ = "127.0.0.1";
		AudioCastLogWarning("audiocast_public_host is not set: stream URLs use 127.0.0.1 and only work on this machine. "
			"Set it to the server's public IP or domain.");
	}
	publicPort_ = config_.publicPort > 0 ? config_.publicPort : config_.port;

	loader_.start(config_);
	std::string error;
	if (server_.start(config_, error))
	{
		AudioCastLogMessage("v" AUDIOCAST_VERSION " streaming on " + config_.bindAddress + ":" + std::to_string(config_.port) +
			" (TCP), clients use http://" + publicHost_ + ":" + std::to_string(publicPort_) + "/");
	}
	else
	{
		AudioCastLogWarning(error + ". Players will not hear the radios until the port is free.");
	}
}

void AudioCastComponent::stopServices()
{
	if (!started_) return;
	started_ = false;
	loader_.stop();
	server_.stop();
	radios_.clear();
	listeners_.clear();
	searches_.clear();
	events_.clear();
	AudioCastLogFlush();
}

void AudioCastComponent::destroyRadiosOwnedBy(AMX* amx)
{
	for (auto search = searches_.begin(); search != searches_.end();)
	{
		if (search->second.owner == amx) search = searches_.erase(search);
		else ++search;
	}

	std::vector<int> owned;
	for (const auto& entry : radios_)
	{
		if (entry.second.owner == amx) owned.push_back(entry.first);
	}
	for (int radioId : owned) destroyRadio(radioId);
}

void AudioCastComponent::tick()
{
	AudioCastLogFlush();
	// An exception escaping here would unwind into the server's C code.
	try
	{
		deliverLoadResults();
		deliverEvents();
	}
	catch (const std::exception& exception)
	{
		AudioCastLogWarning(std::string("tick failed: ") + exception.what());
	}
	catch (...)
	{
		AudioCastLogWarning("tick failed");
	}
}

void AudioCastComponent::deliverLoadResults()
{
	for (LoadResult& result : loader_.drain())
	{
		if (result.kind == LoadRequest::Kind::Search)
		{
			const auto search = searches_.find(result.requestId);
			if (search == searches_.end()) continue; // freed meanwhile
			search->second.done = true;
			search->second.entries = std::move(result.entries);
			search->second.error = result.error;
			AudioCastEvent event;
			event.type = AudioCastEvent::Type::SearchResults;
			event.requestId = result.requestId;
			event.value = static_cast<int>(search->second.entries.size());
			event.text = result.error;
			events_.push(std::move(event));
			continue;
		}

		const auto it = radios_.find(result.radioId);
		if (it == radios_.end()) continue;
		Station& station = *it->second.station;

		if (result.track)
		{
			AudioCastEvent loaded;
			loaded.type = AudioCastEvent::Type::TrackLoaded;
			loaded.radioId = result.radioId;
			loaded.requestId = result.requestId;
			loaded.value = static_cast<int>(result.track->durationMs());
			loaded.text = result.track->title;
			// Requests removed by Radio_Stop/Radio_ClearQueue are dropped.  The
			// event is queued before the track can start, so scripts see
			// OnRadioTrackLoaded before OnRadioTrackStart.
			if (station.waitingFor(result.requestId))
			{
				events_.push(std::move(loaded));
				station.trackLoaded(result.requestId, result.track);
			}
		}
		else if (station.trackFailed(result.requestId))
		{
			AudioCastLogWarning("radio " + std::to_string(result.radioId) + ": could not load '" + result.query + "': " + result.error);
			AudioCastEvent failed;
			failed.type = AudioCastEvent::Type::TrackError;
			failed.radioId = result.radioId;
			failed.requestId = result.requestId;
			failed.text = result.query;
			failed.text2 = result.error;
			events_.push(std::move(failed));
		}
	}
}

template <typename... Args>
void AudioCastComponent::callPublic(const char* name, Args... args)
{
	if (!pawn_) return;

	// Copy the list: a callback may load or unload a filterscript.
	std::vector<IPawnScript*> scripts;
	for (IPawnScript* side : pawn_->sideScripts()) scripts.push_back(side);
	scripts.push_back(pawn_->mainScript());

	for (IPawnScript* target : scripts)
	{
		if (!target || !target->IsLoaded()) continue;
		int index = -1;
		if (target->FindPublic(name, &index) != AMX_ERR_NONE || index < 0 || index == INT_MAX) continue;
		cell result = 1;
		const int error = target->CallChecked(index, result, args...);
		if (error != AMX_ERR_NONE) target->PrintError(error);
	}
}

void AudioCastComponent::deliverEvents()
{
	for (const AudioCastEvent& event : events_.drain())
	{
		const std::string text = toPawn(event.text);
		const std::string text2 = toPawn(event.text2);
		const StringView textView(text.data(), text.size());
		const StringView text2View(text2.data(), text2.size());

		switch (event.type)
		{
		case AudioCastEvent::Type::TrackLoaded:
			callPublic("OnRadioTrackLoaded", event.radioId, event.requestId, textView, event.value);
			break;
		case AudioCastEvent::Type::TrackStart:
			callPublic("OnRadioTrackStart", event.radioId, event.requestId, textView, event.value);
			break;
		case AudioCastEvent::Type::TrackEnd:
			callPublic("OnRadioTrackEnd", event.radioId, event.requestId, event.value);
			break;
		case AudioCastEvent::Type::TrackError:
			callPublic("OnRadioTrackError", event.radioId, event.requestId, textView, text2View);
			break;
		case AudioCastEvent::Type::ListenerConnect:
			callPublic("OnRadioListenerConnect", event.radioId, event.playerId);
			break;
		case AudioCastEvent::Type::ListenerDisconnect:
			callPublic("OnRadioListenerDisconnect", event.radioId, event.playerId);
			break;
		case AudioCastEvent::Type::SearchResults:
			callPublic("OnRadioSearchResults", event.requestId, event.value, textView);
			break;
		}
	}
}

// ---------------------------------------------------------------------------
// Native support
// ---------------------------------------------------------------------------

IPawnScript* AudioCastComponent::script(AMX* amx)
{
	return pawn_ ? pawn_->getScript(amx) : nullptr;
}

std::string AudioCastComponent::toPawn(const std::string& utf8) const
{
	return config_.utf8 ? utf8 : Text::utf8ToLatin1(utf8);
}

std::string AudioCastComponent::fromPawn(const std::string& text) const
{
	return config_.utf8 ? text : Text::latin1ToUtf8(text);
}

int AudioCastComponent::createRadio(AMX* owner, const std::string& name)
{
	startServices();
	const int radioId = nextRadioId_++;
	const std::string stationName = Text::trim(name).empty() ? "AudioCast #" + std::to_string(radioId) : Text::trim(name);
	auto station = std::make_shared<Station>(radioId, stationName, events_, config_.bitrateKbps, config_.burstMs);
	radios_[radioId] = Radio { station, owner };
	server_.addStation(station);
	return radioId;
}

bool AudioCastComponent::destroyRadio(int radioId)
{
	const auto it = radios_.find(radioId);
	if (it == radios_.end()) return false;

	for (auto listener = listeners_.begin(); listener != listeners_.end();)
	{
		if (listener->second.radioId == radioId)
		{
			server_.removeTicket(listener->second.token);
			listener = listeners_.erase(listener);
		}
		else
		{
			++listener;
		}
	}
	server_.removeStation(radioId);
	radios_.erase(it);
	return true;
}

std::shared_ptr<Station> AudioCastComponent::radio(int radioId) const
{
	const auto it = radios_.find(radioId);
	return it == radios_.end() ? nullptr : it->second.station;
}

int AudioCastComponent::requestTrack(int radioId, const std::string& query, bool interrupt)
{
	const auto station = radio(radioId);
	if (!station || Text::trim(query).empty()) return 0;

	const int requestId = nextRequestId_++;
	if (nextRequestId_ <= 0) nextRequestId_ = 1;
	station->enqueue(requestId, query, interrupt);
	loader_.submit(LoadRequest { LoadRequest::Kind::Track, radioId, requestId, query, 0 });
	return requestId;
}

bool AudioCastComponent::attachPlayer(int playerId, int radioId, std::string& url)
{
	if (playerId < 0 || playerId >= MAX_PLAYERS || !radio(radioId)) return false;

	// A new token per attachment: the previous stream of this player (or of
	// a former player with the same ID) stops working.
	detachPlayer(playerId);
	Listener listener;
	listener.radioId = radioId;
	listener.token = Text::randomToken(24);
	server_.addTicket(listener.token, StreamServer::Ticket { playerId, radioId, playerVolume(playerId) });
	url = "http://" + publicHost_ + ":" + std::to_string(publicPort_) + "/radio/" + std::to_string(radioId) + "/" + listener.token + ".mp3";
	listeners_[playerId] = std::move(listener);
	return true;
}

bool AudioCastComponent::detachPlayer(int playerId)
{
	const auto it = listeners_.find(playerId);
	if (it == listeners_.end()) return false;
	server_.removeTicket(it->second.token);
	listeners_.erase(it);
	return true;
}

int AudioCastComponent::playerRadio(int playerId) const
{
	const auto it = listeners_.find(playerId);
	return it == listeners_.end() ? 0 : it->second.radioId;
}

bool AudioCastComponent::playerStreaming(int playerId) const
{
	const auto it = listeners_.find(playerId);
	return it != listeners_.end() && server_.ticketConnected(it->second.token);
}

int AudioCastComponent::listenerCount(int radioId) const
{
	return server_.listenerCount(radioId);
}

bool AudioCastComponent::setRadioVolume(int radioId, int volume)
{
	const auto it = radios_.find(radioId);
	if (it == radios_.end()) return false;
	it->second.volume = std::clamp(volume, 0, 100);
	server_.setRadioVolume(radioId, it->second.volume);
	return true;
}

int AudioCastComponent::radioVolume(int radioId) const
{
	const auto it = radios_.find(radioId);
	return it == radios_.end() ? 0 : it->second.volume;
}

bool AudioCastComponent::setPlayerVolume(int playerId, int volume)
{
	if (playerId < 0 || playerId >= MAX_PLAYERS) return false;
	volume = std::clamp(volume, 0, 100);
	if (volume == 100) playerVolumes_.erase(playerId);
	else playerVolumes_[playerId] = volume;

	const auto listener = listeners_.find(playerId);
	if (listener != listeners_.end()) server_.setTicketVolume(listener->second.token, volume);
	return true;
}

int AudioCastComponent::playerVolume(int playerId) const
{
	const auto it = playerVolumes_.find(playerId);
	return it == playerVolumes_.end() ? 100 : it->second;
}

std::string AudioCastComponent::publicUrl(int radioId) const
{
	if (!config_.allowPublic || !radio(radioId)) return {};
	return "http://" + publicHost_ + ":" + std::to_string(publicPort_) + "/radio/" + std::to_string(radioId) + ".mp3";
}

int AudioCastComponent::startSearch(AMX* owner, const std::string& query, int maxResults)
{
	startServices();
	if (Text::trim(query).empty()) return 0;

	// Old searches nobody freed: keep the most recent ones only.
	constexpr std::size_t MAX_SEARCHES = 256;
	while (searches_.size() >= MAX_SEARCHES) searches_.erase(searches_.begin());

	const int searchId = nextSearchId_++;
	if (nextSearchId_ <= 0) nextSearchId_ = 1;
	searches_[searchId].owner = owner;
	LoadRequest request;
	request.kind = LoadRequest::Kind::Search;
	request.requestId = searchId;
	request.query = query;
	request.maxResults = maxResults;
	loader_.submit(std::move(request));
	return searchId;
}

const SearchEntry* AudioCastComponent::searchEntry(int searchId, int index) const
{
	const auto search = searches_.find(searchId);
	if (search == searches_.end() || index < 0 || index >= static_cast<int>(search->second.entries.size())) return nullptr;
	return &search->second.entries[static_cast<std::size_t>(index)];
}

int AudioCastComponent::searchCount(int searchId) const
{
	const auto search = searches_.find(searchId);
	if (search == searches_.end()) return -1;
	return search->second.done ? static_cast<int>(search->second.entries.size()) : -1;
}

bool AudioCastComponent::freeSearch(int searchId)
{
	return searches_.erase(searchId) != 0;
}
