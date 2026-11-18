/*
 *  AudioCast for open.mp and SA-MP
 *  Copyright (c) 2026 devbluen
 */

#include "stream-server.hpp"
#include "log.hpp"
#include "mp3.hpp"
#include "text.hpp"
#include "version.hpp"
#include <algorithm>
#include <cctype>
#include <deque>
#include <sstream>
#include <vector>

using tcp = asio::ip::tcp;

namespace
{
constexpr auto PUMP_INTERVAL = std::chrono::milliseconds(20);
constexpr auto REQUEST_TIMEOUT = std::chrono::seconds(10);
constexpr std::size_t MAX_REQUEST_SIZE = 8192;
constexpr std::size_t ICY_METADATA_INTERVAL = 16000;

std::string lower(std::string value)
{
	std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return value;
}

std::string headerSafe(const std::string& text, int codepage)
{
	std::string safe = Text::utf8ToCodepage(text, codepage);
	safe.erase(std::remove_if(safe.begin(), safe.end(), [](char c) { return c == '\r' || c == '\n'; }), safe.end());
	if (safe.size() > 200) safe.resize(200);
	return safe;
}

// The audio at a volume (MP3 frames with lowered global gain).
std::string attenuated(std::string audio, int steps)
{
	Mp3::attenuate(reinterpret_cast<std::uint8_t*>(audio.data()), audio.size(), steps);
	return audio;
}

std::string plainResponse(const char* status)
{
	return std::string("HTTP/1.0 ") + status + "\r\nContent-Type: text/plain\r\nConnection: close\r\nCache-Control: no-cache\r\n\r\n" + status + "\n";
}
}

class StreamSession : public std::enable_shared_from_this<StreamSession>
{
private:
	StreamServer& server_;
	tcp::socket socket_;
	asio::streambuf request_;
	asio::steady_timer timeout_;
	std::deque<std::string> outbox_;
	std::size_t queuedBytes_ = 0;
	std::size_t maxQueuedBytes_ = 0;
	bool writing_ = false;
	bool closeAfterWrite_ = false;
	bool closed_ = false;
	bool icy_ = false;
	std::size_t untilMetadata_ = ICY_METADATA_INTERVAL;
	std::string sentTitle_;

	void onRequest(const std::error_code& error)
	{
		if (closed_) return;
		if (error)
		{
			close();
			return;
		}

		std::istream stream(&request_);
		std::string requestLine;
		std::getline(stream, requestLine);
		bool wantsMetadata = false;
		std::string header;
		while (std::getline(stream, header) && header != "\r" && !header.empty())
		{
			const std::string lowered = lower(header);
			if (lowered.rfind("icy-metadata:", 0) == 0 && lowered.find('1') != std::string::npos) wantsMetadata = true;
		}

		// "GET /radio/<id>/<token>.mp3 HTTP/1.1"
		std::istringstream line(requestLine);
		std::string method;
		std::string target;
		line >> method >> target;
		const bool isHead = method == "HEAD";
		if ((method != "GET" && !isHead) || target.compare(0, 7, "/radio/") != 0)
		{
			reply(plainResponse("404 Not Found"));
			return;
		}

		std::string path = target.substr(7);
		if (const std::size_t query = path.find_first_of("?#"); query != std::string::npos) path.resize(query);
		if (path.size() > 4 && lower(path.substr(path.size() - 4)) == ".mp3") path.resize(path.size() - 4);
		std::string idText = path;
		std::string requestToken;
		if (const std::size_t slash = path.find('/'); slash != std::string::npos)
		{
			idText = path.substr(0, slash);
			requestToken = path.substr(slash + 1);
		}

		if (idText.empty() || idText.size() > 9 || !std::all_of(idText.begin(), idText.end(), [](unsigned char c) { return std::isdigit(c); }))
		{
			reply(plainResponse("404 Not Found"));
			return;
		}
		server_.openStream(shared_from_this(), std::stoi(idText), requestToken, isHead, wantsMetadata);
	}

	void write()
	{
		if (closed_ || outbox_.empty())
		{
			writing_ = false;
			if (closeAfterWrite_) close();
			return;
		}
		writing_ = true;
		auto self = shared_from_this();
		asio::async_write(socket_, asio::buffer(outbox_.front()), [self](const std::error_code& error, std::size_t)
		{
			if (self->closed_) return;
			if (error)
			{
				self->close();
				return;
			}
			self->queuedBytes_ -= self->outbox_.front().size();
			self->outbox_.pop_front();
			self->write();
		});
	}

	std::string metadataBlock(const std::string& title)
	{
		std::string text = Text::utf8ToCodepage(title, server_.config_.metadataCodepage);
		if (text == sentTitle_) return std::string(1, '\0');
		sentTitle_ = text;
		std::replace(text.begin(), text.end(), '\'', '`');
		text = "StreamTitle='" + text + "';";
		if (text.size() > 255 * 16) text.resize(255 * 16);
		const std::size_t blocks = (text.size() + 15) / 16;
		text.resize(blocks * 16, '\0');
		return static_cast<char>(blocks) + text;
	}

public:
	int radioId = 0;
	int playerId = -1;
	std::string token;
	bool attached = false;

	StreamSession(StreamServer& server, tcp::socket socket, std::size_t maxQueuedBytes)
		: server_(server)
		, socket_(std::move(socket))
		, request_(MAX_REQUEST_SIZE)
		, timeout_(socket_.get_executor())
		, maxQueuedBytes_(maxQueuedBytes)
	{
	}

	void start()
	{
		std::error_code ignored;
		socket_.set_option(tcp::no_delay(true), ignored);

		auto self = shared_from_this();
		timeout_.expires_after(REQUEST_TIMEOUT);
		timeout_.async_wait([self](const std::error_code& error)
		{
			if (!error && !self->attached && !self->closed_) self->close();
		});
		asio::async_read_until(socket_, request_, "\r\n\r\n", [self](const std::error_code& error, std::size_t)
		{
			self->onRequest(error);
		});
	}

	void enableMetadata() { icy_ = true; }

	// Sends a complete response and closes the connection.
	void reply(std::string response)
	{
		push(std::move(response));
		closeAfterWrite_ = true;
		if (!writing_) write();
	}

	void push(std::string data)
	{
		if (closed_ || data.empty()) return;
		// A client that cannot keep up (or stopped reading) is dropped instead
		// of buffering audio forever.
		if (queuedBytes_ + data.size() > maxQueuedBytes_)
		{
			close();
			return;
		}
		queuedBytes_ += data.size();
		outbox_.push_back(std::move(data));
		if (!writing_) write();
	}

	void sendAudio(const std::string& audio, const std::string& title)
	{
		if (closed_ || audio.empty()) return;
		if (!icy_)
		{
			push(audio);
			return;
		}

		// Shoutcast metadata: a title block after every ICY_METADATA_INTERVAL
		// bytes of audio.
		std::string chunk;
		chunk.reserve(audio.size() + 64);
		std::size_t position = 0;
		while (position < audio.size())
		{
			const std::size_t count = std::min(untilMetadata_, audio.size() - position);
			chunk.append(audio, position, count);
			position += count;
			untilMetadata_ -= count;
			if (untilMetadata_ == 0)
			{
				chunk += metadataBlock(title);
				untilMetadata_ = ICY_METADATA_INTERVAL;
			}
		}
		push(std::move(chunk));
	}

	void close()
	{
		if (closed_) return;
		closed_ = true;
		auto self = shared_from_this();
		std::error_code ignored;
		timeout_.cancel();
		socket_.shutdown(tcp::socket::shutdown_both, ignored);
		socket_.close(ignored);
		outbox_.clear();
		queuedBytes_ = 0;
		server_.sessionClosed(*this);
	}
};

StreamServer::StreamServer(EventQueue& events)
	: events_(events)
{
}

StreamServer::~StreamServer()
{
	stop();
}

bool StreamServer::start(const AudioCastConfig& config, std::string& error)
{
	if (running_) return true;
	config_ = config;
	io_ = std::make_unique<asio::io_context>(1);
	pumpTimer_ = std::make_unique<asio::steady_timer>(*io_);

	// The radios keep their clock even when the port cannot be opened, so
	// the network thread always runs.
	bool listening = false;
	try
	{
		const tcp::endpoint endpoint(asio::ip::make_address(config_.bindAddress), static_cast<unsigned short>(config_.port));
		acceptor_ = std::make_unique<tcp::acceptor>(*io_);
		acceptor_->open(endpoint.protocol());
		acceptor_->set_option(tcp::acceptor::reuse_address(true));
		acceptor_->bind(endpoint);
		acceptor_->listen();
		boundPort_ = acceptor_->local_endpoint().port();
		listening = true;
	}
	catch (const std::exception& exception)
	{
		error = "could not listen on " + config_.bindAddress + ":" + std::to_string(config_.port) + " - " + exception.what();
		acceptor_.reset();
	}

	if (acceptor_) accept();
	schedulePump();
	running_ = true;
	thread_ = std::thread([this]
	{
		for (;;)
		{
			try
			{
				io_->run();
				return;
			}
			catch (const std::exception& exception)
			{
				AudioCastLogWarning(std::string("network thread error: ") + exception.what());
			}
		}
	});
	return listening;
}

void StreamServer::stop()
{
	if (!running_) return;
	asio::post(*io_, [this]
	{
		std::error_code ignored;
		if (acceptor_) acceptor_->close(ignored);
		pumpTimer_->cancel();
		for (auto& entry : slots_)
		{
			const auto sessions = entry.second.sessions;
			for (const auto& session : sessions) session->close();
		}
		slots_.clear();
		io_->stop();
	});
	if (thread_.joinable()) thread_.join();
	slots_.clear();
	pumpTimer_.reset();
	acceptor_.reset();
	io_.reset();
	running_ = false;
	boundPort_ = 0;

	std::lock_guard<std::mutex> lock(sharedMutex_);
	tickets_.clear();
	listenerCounts_.clear();
	radioVolumes_.clear();
	connectedTickets_.clear();
	sessionCount_ = 0;
}

void StreamServer::accept()
{
	acceptor_->async_accept([this](const std::error_code& error, tcp::socket socket)
	{
		if (!acceptor_ || !acceptor_->is_open()) return;
		if (!error)
		{
			// Room for 30 seconds of audio plus the burst.
			const std::size_t maxQueued = static_cast<std::size_t>(config_.bitrateKbps) * 125u *
				(30u + static_cast<std::size_t>(config_.burstMs) / 1000u) + 65536u;
			std::make_shared<StreamSession>(*this, std::move(socket), maxQueued)->start();
		}
		accept();
	});
}

void StreamServer::schedulePump()
{
	pumpTimer_->expires_after(PUMP_INTERVAL);
	pumpTimer_->async_wait([this](const std::error_code& error)
	{
		if (error) return;
		pump();
		schedulePump();
	});
}

void StreamServer::pump()
{
	const auto now = Station::Clock::now();
	std::string audio;
	for (auto& entry : slots_)
	{
		audio.clear();
		entry.second.station->produce(now, audio);
		if (audio.empty() || entry.second.sessions.empty()) continue;

		const std::string title = entry.second.station->streamTitle();
		const std::vector<std::shared_ptr<StreamSession>> sessions(entry.second.sessions.begin(), entry.second.sessions.end());
		std::vector<int> steps;
		{
			std::lock_guard<std::mutex> lock(sharedMutex_);
			for (const auto& session : sessions) steps.push_back(volumeStepsLocked(entry.first, session->token));
		}
		// One copy per volume in use; listeners at full volume share the original.
		std::map<int, std::string> versions;
		for (std::size_t index = 0; index < sessions.size(); ++index)
		{
			if (steps[index] == 0)
			{
				sessions[index]->sendAudio(audio, title);
				continue;
			}
			auto version = versions.find(steps[index]);
			if (version == versions.end()) version = versions.emplace(steps[index], attenuated(audio, steps[index])).first;
			sessions[index]->sendAudio(version->second, title);
		}
	}
}

void StreamServer::openStream(const std::shared_ptr<StreamSession>& session, int radioId, const std::string& token, bool isHead,
	bool wantsMetadata)
{
	int playerId = -1;
	if (token.empty())
	{
		if (!config_.allowPublic)
		{
			session->reply(plainResponse("403 Forbidden"));
			return;
		}
	}
	else
	{
		std::lock_guard<std::mutex> lock(sharedMutex_);
		const auto ticket = tickets_.find(token);
		if (ticket == tickets_.end() || ticket->second.radioId != radioId)
		{
			session->reply(plainResponse("403 Forbidden"));
			return;
		}
		playerId = ticket->second.playerId;
	}
	int steps = 0;
	{
		std::lock_guard<std::mutex> lock(sharedMutex_);
		steps = volumeStepsLocked(radioId, token);
	}

	const auto slot = slots_.find(radioId);
	if (slot == slots_.end())
	{
		session->reply(plainResponse("404 Not Found"));
		return;
	}

	std::string headers = "HTTP/1.0 200 OK\r\n"
		"Content-Type: audio/mpeg\r\n"
		"Cache-Control: no-cache, no-store\r\n"
		"Pragma: no-cache\r\n"
		"Connection: close\r\n"
		"Server: AudioCast/" AUDIOCAST_VERSION "\r\n"
		"icy-name: " + headerSafe(slot->second.station->name(), config_.metadataCodepage) + "\r\n"
		"icy-br: " + std::to_string(config_.bitrateKbps) + "\r\n"
		"icy-pub: 0\r\n";
	if (wantsMetadata) headers += "icy-metaint: " + std::to_string(ICY_METADATA_INTERVAL) + "\r\n";
	headers += "\r\n";

	if (isHead)
	{
		session->reply(std::move(headers));
		return;
	}

	if (sessionCount_ >= static_cast<std::size_t>(config_.maxListeners))
	{
		session->reply(plainResponse("503 Service Unavailable"));
		return;
	}

	// The same player opening the stream again (a new PlayAudioStream)
	// replaces the old connection.
	if (!token.empty())
	{
		std::vector<std::shared_ptr<StreamSession>> previous;
		for (auto& entry : slots_)
		{
			for (const auto& other : entry.second.sessions)
			{
				if (other->token == token) previous.push_back(other);
			}
		}
		for (const auto& other : previous) other->close();
	}

	session->attached = true;
	session->radioId = radioId;
	session->playerId = playerId;
	session->token = token;
	slot->second.sessions.insert(session);
	++sessionCount_;
	{
		std::lock_guard<std::mutex> lock(sharedMutex_);
		++listenerCounts_[radioId];
		if (!token.empty()) ++connectedTickets_[token];
	}

	AudioCastEvent event;
	event.type = AudioCastEvent::Type::ListenerConnect;
	event.radioId = radioId;
	event.playerId = playerId;
	events_.push(std::move(event));

	if (wantsMetadata) session->enableMetadata();
	session->push(std::move(headers));
	// Every listener starts with the same amount of recent audio, so all of
	// them play the same position of the song.
	session->sendAudio(attenuated(slot->second.station->burst(), steps), slot->second.station->streamTitle());
}

int StreamServer::volumeStepsLocked(int radioId, const std::string& token) const
{
	double percent = 100.0;
	const auto radio = radioVolumes_.find(radioId);
	if (radio != radioVolumes_.end()) percent = radio->second;
	if (!token.empty())
	{
		const auto ticket = tickets_.find(token);
		if (ticket != tickets_.end()) percent = percent * ticket->second.volume / 100.0;
	}
	return Mp3::volumeToSteps(percent);
}

void StreamServer::sessionClosed(StreamSession& session)
{
	if (!session.attached) return;
	session.attached = false;

	const auto slot = slots_.find(session.radioId);
	if (slot != slots_.end())
	{
		for (auto it = slot->second.sessions.begin(); it != slot->second.sessions.end(); ++it)
		{
			if (it->get() == &session)
			{
				slot->second.sessions.erase(it);
				break;
			}
		}
	}
	if (sessionCount_ > 0) --sessionCount_;
	{
		std::lock_guard<std::mutex> lock(sharedMutex_);
		if (--listenerCounts_[session.radioId] <= 0) listenerCounts_.erase(session.radioId);
		if (!session.token.empty())
		{
			const auto connected = connectedTickets_.find(session.token);
			if (connected != connectedTickets_.end() && --connected->second <= 0) connectedTickets_.erase(connected);
		}
	}

	AudioCastEvent event;
	event.type = AudioCastEvent::Type::ListenerDisconnect;
	event.radioId = session.radioId;
	event.playerId = session.playerId;
	events_.push(std::move(event));
}

void StreamServer::addStation(std::shared_ptr<Station> station)
{
	if (!running_ || !station) return;
	asio::post(*io_, [this, station]
	{
		slots_[station->id()].station = station;
	});
}

void StreamServer::removeStation(int radioId)
{
	{
		std::lock_guard<std::mutex> lock(sharedMutex_);
		radioVolumes_.erase(radioId);
	}
	if (!running_) return;
	asio::post(*io_, [this, radioId]
	{
		const auto slot = slots_.find(radioId);
		if (slot == slots_.end()) return;
		const auto sessions = slot->second.sessions;
		for (const auto& session : sessions) session->close();
		slots_.erase(radioId);
	});
}

void StreamServer::addTicket(const std::string& token, Ticket ticket)
{
	std::lock_guard<std::mutex> lock(sharedMutex_);
	tickets_[token] = ticket;
}

void StreamServer::removeTicket(const std::string& token)
{
	{
		std::lock_guard<std::mutex> lock(sharedMutex_);
		tickets_.erase(token);
	}
	if (!running_) return;
	asio::post(*io_, [this, token]
	{
		std::vector<std::shared_ptr<StreamSession>> sessions;
		for (auto& entry : slots_)
		{
			for (const auto& session : entry.second.sessions)
			{
				if (session->token == token) sessions.push_back(session);
			}
		}
		for (const auto& session : sessions) session->close();
	});
}

void StreamServer::setRadioVolume(int radioId, int percent)
{
	std::lock_guard<std::mutex> lock(sharedMutex_);
	if (percent >= 100) radioVolumes_.erase(radioId);
	else radioVolumes_[radioId] = std::max(percent, 0);
}

void StreamServer::setTicketVolume(const std::string& token, int percent)
{
	std::lock_guard<std::mutex> lock(sharedMutex_);
	const auto ticket = tickets_.find(token);
	if (ticket != tickets_.end()) ticket->second.volume = std::clamp(percent, 0, 100);
}

int StreamServer::listenerCount(int radioId) const
{
	std::lock_guard<std::mutex> lock(sharedMutex_);
	const auto it = listenerCounts_.find(radioId);
	return it == listenerCounts_.end() ? 0 : it->second;
}

bool StreamServer::ticketConnected(const std::string& token) const
{
	std::lock_guard<std::mutex> lock(sharedMutex_);
	return connectedTickets_.count(token) != 0;
}
