/*
 *  AudioCast for open.mp and SA-MP
 *  Copyright (c) 2026 devbluen
 */

#pragma once

#include "config.hpp"
#include "track-events.hpp"
#include "loader.hpp"
#include "samp-pawn.hpp"
#include "station.hpp"
#include "stream-server.hpp"
#include <Server/Components/Pawn/pawn.hpp>
#include <sdk.hpp>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>

struct IAudioCastComponent : public IComponent
{
	PROVIDE_UID(0x4A75C1D3A0B7F296);
};

// One binary, two hosts: loaded from components/ it is an open.mp component,
// loaded from plugins/ it is a classic SA-MP plugin (see samp-main.cpp).
class AudioCastComponent final
	: public IAudioCastComponent
	, public CoreEventHandler
	, public PawnEventHandler
	, public PlayerConnectEventHandler
{
private:
	struct Radio
	{
		std::shared_ptr<Station> station;
		AMX* owner = nullptr;
	};

	struct Listener
	{
		int radioId = 0;
		std::string token;
	};

	ICore* core_ = nullptr;
	IPawnComponent* pawn_ = nullptr;
	SampPawnComponent sampPawn_;
	bool sampMode_ = false;

	AudioCastConfig config_;
	bool started_ = false;
	std::string publicHost_;
	int publicPort_ = 0;

	EventQueue events_;
	TrackLoader loader_;
	StreamServer server_ { events_ };

	std::map<int, Radio> radios_;
	std::unordered_map<int, Listener> listeners_;

	struct Search
	{
		AMX* owner = nullptr;
		bool done = false;
		std::vector<SearchEntry> entries;
		std::string error;
	};
	// Kept until Radio_FreeSearch, the owner script unloads, or too many pile up.
	std::map<int, Search> searches_;
	int nextSearchId_ = 1;
	int nextRadioId_ = 1;
	int nextRequestId_ = 1;

	static AudioCastComponent* instance_;

	std::string configValue(const std::string& key) const;
	void startServices();
	void stopServices();
	void destroyRadiosOwnedBy(AMX* amx);
	void tick();
	void deliverLoadResults();
	void deliverEvents();

	template <typename... Args>
	void callPublic(const char* name, Args... args);

public:
	// IComponent
	StringView componentName() const override { return "AudioCast"; }
	SemanticVersion componentVersion() const override;
	void onLoad(ICore* core) override;
	void onInit(IComponentList* components) override;
	void onReady() override;
	void onFree(IComponent* component) override;
	void provideConfiguration(ILogger& logger, IEarlyConfig& config, bool defaults) override;
	void free() override;
	void reset() override;

	void onTick(Microseconds elapsed, TimePoint now) override;
	void onAmxLoad(IPawnScript& script) override;
	void onAmxUnload(IPawnScript& script) override;
	void onPlayerDisconnect(IPlayer& player, PeerDisconnectReason reason) override;

	// SA-MP plugin entry points.
	void sampLoad();
	void sampUnload();
	void sampAmxLoad(AMX* amx);
	void sampAmxUnload(AMX* amx);
	void sampTick();

	// Used by the natives (server thread).
	IPawnScript* script(AMX* amx);
	const AudioCastConfig& config() const { return config_; }
	std::string toPawn(const std::string& utf8) const;
	std::string fromPawn(const std::string& text) const;

	int createRadio(AMX* owner, const std::string& name);
	bool destroyRadio(int radioId);
	std::shared_ptr<Station> radio(int radioId) const;
	int requestTrack(int radioId, const std::string& query, bool interrupt);
	int startSearch(AMX* owner, const std::string& query, int maxResults);
	const SearchEntry* searchEntry(int searchId, int index) const;
	int searchCount(int searchId) const;
	bool freeSearch(int searchId);
	bool attachPlayer(int playerId, int radioId, std::string& url);
	bool detachPlayer(int playerId);
	int playerRadio(int playerId) const;
	bool playerStreaming(int playerId) const;
	int listenerCount(int radioId) const;
	std::string publicUrl(int radioId) const;

	static AudioCastComponent* getInstance();
};
