#include "GUI/Net/Session.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <random>
#include <thread>

namespace {
    // no I, L, O, 0 or 1
    constexpr std::string_view Alphabet = "ABCDEFGHJKMNPQRSTUVWXYZ23456789";

    // anything bigger than this didn't come from this app
    constexpr std::size_t MaxLine = 256;
    constexpr std::size_t MaxBuffered = 1024;
    constexpr std::size_t MaxQueuedOutput = 8192;
    constexpr std::size_t ReadBudget = 4096; // per frame
    constexpr int LineBudget = 32;

    constexpr std::size_t MaxPending = 8;
    constexpr float PendingTimeout = 5.f;

    constexpr float PingInterval = 4.f;
    constexpr float SilenceLimit = 12.f;
    constexpr float ResumeWindow = 60.f;
    constexpr float ResumeHandshake = 5.f;

    constexpr float RelayDelay = 1.5f; // prefer local play till this timer
    constexpr float LanOnlyJoinTime = 6.f;
    constexpr float RelayJoinTime = 60.f;
    constexpr float RelayRetryDelay = 3.f;
    constexpr int RelayPollSeconds = 15;
    constexpr int RelayHttpTimeout = 25;
    constexpr float RelayDeadAfter = 20.f;
    constexpr std::size_t MaxRelayLines = 256;

    constexpr unsigned MaxNameLength = 20;

    std::string RandomText(std::size_t length) {
        std::random_device device;
        std::seed_seq seed{device(), device(), device(), static_cast<unsigned>(std::chrono::steady_clock::now().time_since_epoch().count())};
        std::mt19937_64 generator(seed);
        std::uniform_int_distribution<std::size_t> pick(0, Alphabet.size() - 1);

        std::string text;
        for (std::size_t i = 0; i < length; ++i) text += Alphabet[pick(generator)];

        return text;
    }

    bool IsToken(const std::string& text) {
        return text.size() == 16 && std::all_of(text.begin(), text.end(), [](char c) { return Alphabet.find(c) != std::string_view::npos; });
    }

    bool IsLocalAddress(sf::IpAddress address) {
        const std::uint32_t v = address.toInteger();
        const unsigned a = v >> 24, b = (v >> 16) & 0xFF;

        return a == 10 || a == 127 || (a == 172 && b >= 16 && b <= 31) || (a == 192 && b == 168) || (a == 169 && b == 254);
    }

    std::string CleanName(const std::string& name) {
        std::string clean;

        for (const char c : name) {
            if (c >= 32 && c <= 126 && clean.size() < MaxNameLength) clean += c;
        }

        const std::size_t first = clean.find_first_not_of(' ');
        const std::size_t last = clean.find_last_not_of(' ');

        return first == std::string::npos ? "Player" : clean.substr(first, last - first + 1);
    }

    bool IsFenText(const std::string& text) {
        return !text.empty() && text.size() <= 120 && std::all_of(text.begin(), text.end(), [](unsigned char c) {
            return std::isalnum(c) || c == '/' || c == ' ' || c == '-';
        });
    }

    bool IsMoveToken(const std::string& text) {
        const auto file = [](char c) { return c >= 'a' && c <= 'h'; };
        const auto rank = [](char c) { return c >= '1' && c <= '8'; };

        if (text.size() != 4 && text.size() != 5) return false;
        if (!file(text[0]) || !rank(text[1]) || !file(text[2]) || !rank(text[3])) return false;

        return text.size() == 4 || std::string_view("qrbn").find(text[4]) != std::string_view::npos;
    }
}

#pragma region Codes

std::string Net::GenerateCode() {
    return RandomText(CodeLength);
}

std::string Net::NormalizeCode(const std::string& text) {
    std::string code;

    for (const char raw : text) {
        if (raw == ' ' || raw == '-') continue;

        const char c = static_cast<char>(std::toupper(static_cast<unsigned char>(raw)));

        if (Alphabet.find(c) == std::string_view::npos || code.size() >= CodeLength) return {};

        code += c;
    }

    return code.size() == CodeLength ? code : std::string();
}

std::string Net::FormatCode(const std::string& code) {
    return code.size() == CodeLength ? code.substr(0, 3) + "-" + code.substr(3) : code;
}

#pragma region LineSocket

Net::LineSocket::LineSocket(std::unique_ptr<sf::TcpSocket> socket) : m_Socket(std::move(socket)) {
    if (m_Socket) m_Socket->setBlocking(false);
}

Net::LineSocket::LineSocket(std::shared_ptr<RelayLink> link) : m_Link(std::move(link)) {}

void Net::LineSocket::Send(const std::string& line) {
    if (m_Dead || !Valid() || line.size() > MaxLine) return;

    m_Out += line;
    m_Out += '\n';

    Flush();
}

void Net::LineSocket::Flush() {
    if (m_Link) {
        if (!m_Out.empty()) m_Link->Send(m_Out);

        m_Out.clear();
        return;
    }

    while (!m_Dead && m_Socket && !m_Out.empty()) {
        std::size_t sent = 0;
        const sf::Socket::Status status = m_Socket->send(m_Out.data(), m_Out.size(), sent);

        if (status == sf::Socket::Status::Done) m_Out.clear();
        else if (status == sf::Socket::Status::Partial) m_Out.erase(0, sent);
        else if (status == sf::Socket::Status::NotReady) break;
        else m_Dead = true;
    }

    if (m_Out.size() > MaxQueuedOutput) m_Dead = true;
}

void Net::LineSocket::Pump() {
    if (m_Dead || !Valid()) return;

    Flush();

    if (m_Link) {
        m_Link->Take(m_In, MaxBuffered);

        if (m_Link->GetPhase() == RelayLink::Phase::Dead) m_Dead = true;
        return;
    }

    char buffer[512];
    std::size_t budget = ReadBudget;

    while (!m_Dead && budget > 0 && m_In.size() < MaxBuffered) {
        const std::size_t room = std::min({sizeof(buffer), budget, MaxBuffered - m_In.size()});
        std::size_t received = 0;

        const sf::Socket::Status status = m_Socket->receive(buffer, room, received);

        if (status == sf::Socket::Status::Done) {
            m_In.append(buffer, received);
            budget -= std::min(budget, received);
        } else if (status == sf::Socket::Status::NotReady) {
            break;
        } else {
            m_Dead = true;
        }
    }
}

std::optional<std::string> Net::LineSocket::NextLine() {
    const std::size_t end = m_In.find('\n');

    if (end == std::string::npos) {
        if (m_In.size() > MaxLine) {
            m_In.clear();
            m_Dead = true;
        }

        return std::nullopt;
    }

    if (end > MaxLine) {
        m_In.clear();
        m_Dead = true;

        return std::nullopt;
    }

    std::string line = m_In.substr(0, end);
    m_In.erase(0, end + 1);

    if (!line.empty() && line.back() == '\r') line.pop_back();

    return line;
}

#pragma region Connector

struct Net::Connector::State {
    std::atomic<bool> Done{false};
    std::unique_ptr<sf::TcpSocket> Socket;
};

void Net::Connector::Start(sf::IpAddress address, unsigned short port, float timeoutSeconds) {
    auto state = std::make_shared<State>();
    m_State = state;

    std::thread([state, address, port, timeoutSeconds]() {
        auto socket = std::make_unique<sf::TcpSocket>();

        if (socket->connect(address, port, sf::seconds(timeoutSeconds)) == sf::Socket::Status::Done) {
            state->Socket = std::move(socket);
        }

        state->Done.store(true, std::memory_order_release);
    }).detach();
}

std::optional<std::unique_ptr<sf::TcpSocket>> Net::Connector::Poll() {
    if (!m_State || !m_State->Done.load(std::memory_order_acquire)) return std::nullopt;

    std::unique_ptr<sf::TcpSocket> socket = std::move(m_State->Socket);
    m_State.reset();

    return socket;
}

#pragma region RelayLink

struct Net::RelayLink::State {
    std::mutex Mutex;
    std::condition_variable Wake;
    std::deque<std::string> In, Out;

    std::atomic<Phase> Current{Phase::Connecting};
    std::atomic<bool> Stop{false}, Reached{false}, Failing{false};
    std::atomic<int> LastStatus{0};

    std::string Host, Code, Owner, Key;
    unsigned short Port{80};
    bool IsHost{false};

    struct Reply {
        int Status{0};
        std::string Body;
    };

    static bool FromRelay(int status, const std::string& body) {
        switch (status) {
        case 200:
        case 204: return true;
        case 404: return body.empty() || body == "NOTFOUND";
        case 409: return body.empty() || body == "TAKEN";
        default: return false;
        }
    }

    struct Client {
        sf::Http Http;
        bool Resolved{false};
    };

    Reply Ask(Client& client, const std::string& path, const std::string& body, int timeout) {
        if (!client.Resolved) client.Resolved = client.Http.setHost(Host, Port);

        if (!client.Resolved) {
            LastStatus = 0;
            Failing = true;
            return {};
        }

        sf::Http::Request request(path, sf::Http::Request::Method::Post, body);
        request.setField("Content-Type", "text/plain");
        request.setField("User-Agent", "Chess++");
        request.setField("Connection", "close");

        const sf::Http::Response response = client.Http.sendRequest(request, sf::seconds(static_cast<float>(timeout)));
        const int status = static_cast<int>(response.getStatus());

        const bool reachable = FromRelay(status, response.getBody());

        LastStatus = status;
        Failing = !reachable;
        if (reachable) Reached = true;

        return {reachable ? status : 0, response.getBody()};
    }

    bool Sleep(float seconds) {
        std::unique_lock lock(Mutex);

        return !Wake.wait_for(lock, std::chrono::duration<float>(seconds), [this] { return Stop.load(); });
    }

    void Finish(Phase phase) {
        Current = phase;
        Wake.notify_all();
    }

    bool Register(Client& client) {
        const Reply reply = IsHost ? Ask(client, "/host", Code + " " + Owner, RelayHttpTimeout) : Ask(client, "/join", Code, RelayHttpTimeout);

        if (reply.Status != 200 || reply.Body.size() != 19 || reply.Body.compare(0, 3, "OK ") != 0) {
            Sleep(RelayRetryDelay);
            return false;
        }

        Key = reply.Body.substr(3);
        Current = IsHost ? Phase::Waiting : Phase::Paired;

        return true;
    }

    void Poll() {
        Client client;

        bool registered = false;
        float failedFor = 0.f;

        while (!Stop) {
            if (!registered) {
                registered = Register(client);
                continue;
            }

            const Reply reply = Ask(client, "/poll", Code + " " + Key + " " + std::to_string(RelayPollSeconds), RelayHttpTimeout);

            if (Stop) return;

            if (reply.Status == 0) {
                failedFor += 1.f;

                if (Current == Phase::Paired && failedFor > RelayDeadAfter) return Finish(Phase::Dead);

                Sleep(1.f);
            } else if (reply.Status == 200) {
                failedFor = 0.f;

                std::lock_guard lock(Mutex);
                std::size_t start = 0;

                while (start <= reply.Body.size()) {
                    std::size_t end = reply.Body.find('\n', start);
                    if (end == std::string::npos) end = reply.Body.size();

                    const std::string line = reply.Body.substr(start, end - start);
                    start = end + 1;

                    if (line.empty()) continue;

                    if (IsHost && Current == Phase::Waiting && line == "PAIRED") Current = Phase::Paired;
                    else if (Current == Phase::Paired) In.push_back(line);

                    if (line.size() > MaxLine || In.size() > MaxRelayLines) return Finish(Phase::Dead);
                }
            } else if (reply.Status == 204) {
                failedFor = 0.f;
            } else if (IsHost && Current != Phase::Paired) {
                registered = false;
                Current = Phase::Connecting;
            } else {
                return Finish(Phase::Dead);
            }
        }
    }

    void Push() {
        Client client;

        float failedFor = 0.f;

        while (true) {
            std::string body;

            {
                std::unique_lock lock(Mutex);
                const auto pending = [this] { return !Out.empty() && Current == Phase::Paired; };
                Wake.wait(lock, [&] { return Stop.load() || pending(); });

                if (!pending()) return;

                body = Code + " " + Key;
                for (std::size_t i = 0; i < 16 && i < Out.size(); ++i) body += "\n" + Out[i];
            }

            const Reply reply = Ask(client, "/send", body, RelayHttpTimeout);

            if (reply.Status == 200) {
                std::lock_guard lock(Mutex);

                std::size_t sent = std::count(body.begin(), body.end(), '\n');
                Out.erase(Out.begin(), Out.begin() + static_cast<std::ptrdiff_t>(std::min(sent, Out.size())));
                failedFor = 0.f;
            } else if (Stop) {
                return;
            } else if (reply.Status == 0 && (failedFor += 1.f) <= RelayDeadAfter) {
                Sleep(1.f);
            } else {
                return Finish(Phase::Dead);
            }
        }
    }
};

Net::RelayLink::RelayLink(const std::string& url, Mode mode, const std::string& code, const std::string& owner) : m_State(std::make_shared<State>()) {
    State& state = *m_State;

    std::string host = url;

    while (!host.empty() && host.back() == '/') host.pop_back();

    const bool secure = host.rfind("https://", 0) == 0;

    if (!secure && host.rfind("http://", 0) != 0) host = "http://" + host;

    state.Port = secure ? 443 : 80;

    const std::size_t schemeEnd = host.find("://") + 3;
    const std::size_t colon = host.find(':', schemeEnd);

    if (colon != std::string::npos) {
        unsigned parsed = 0;
        const auto [last, error] = std::from_chars(host.data() + colon + 1, host.data() + host.size(), parsed);

        if (error == std::errc() && last == host.data() + host.size() && parsed > 0 && parsed < 65536) state.Port = static_cast<unsigned short>(parsed);

        host.erase(colon);
    }

    state.Host = host;
    state.Code = code;
    state.Owner = owner;
    state.IsHost = mode == Mode::Host;

    std::thread([s = m_State] { s->Poll(); }).detach();
    std::thread([s = m_State] { s->Push(); }).detach();
}

Net::RelayLink::~RelayLink() {
    m_State->Stop = true;
    m_State->Wake.notify_all();
}

Net::RelayLink::Phase Net::RelayLink::GetPhase() const noexcept {
    return m_State->Current;
}

bool Net::RelayLink::Reached() const noexcept {
    return m_State->Reached;
}

bool Net::RelayLink::Failing() const noexcept {
    return m_State->Failing;
}

int Net::RelayLink::LastStatus() const noexcept {
    return m_State->LastStatus;
}

void Net::RelayLink::Send(const std::string& lines) {
    std::lock_guard lock(m_State->Mutex);

    std::size_t start = 0;

    while (start < lines.size()) {
        std::size_t end = lines.find('\n', start);
        if (end == std::string::npos) end = lines.size();

        if (m_State->Out.size() >= MaxRelayLines) {
            m_State->Current = Phase::Dead;
            return;
        }

        m_State->Out.push_back(lines.substr(start, end - start));
        start = end + 1;
    }

    m_State->Wake.notify_all();
}

void Net::RelayLink::Take(std::string& into, std::size_t maxBytes) {
    std::lock_guard lock(m_State->Mutex);

    while (!m_State->In.empty() && into.size() + m_State->In.front().size() + 1 <= maxBytes) {
        into += m_State->In.front();
        into += '\n';
        m_State->In.pop_front();
    }
}

#pragma region Session lifetime

Net::Session::~Session() {
    Close();
}

void Net::Session::closeSockets() {
    m_Listener.close();
    m_Udp.unbind();
    m_UdpBound = false;

    m_Pending.clear();

    m_Relay.reset();

    m_LanConnector.Cancel();
    m_Lan = LineSocket();
    m_LanHost.reset();

    m_Peer = LineSocket();
}

void Net::Session::reset() {
    closeSockets();

    m_State = State::Idle;
    m_Code.clear();
    m_Error.clear();
    m_PeerName.clear();
    m_Events.clear();
    m_Elapsed = 0.f;

    m_ViaRelay = false;
    m_IsHost = false;
    m_Token.clear();
    m_Resumable = m_Resuming = m_AwaitResume = false;
    m_ResumeLeft = m_ResumeHandshake = 0.f;
    m_SincePing = m_SinceReceive = 0.f;

    m_RelayEnabled = false;
    m_RelayOwner.clear();
    m_RelayTimer = 0.f;
    m_RelayReached = false;

    m_FindTimer = m_LanAge = m_LanRetry = 0.f;
    m_FindTargets.clear();
}

void Net::Session::Close() {
    if (m_State == State::Connected) m_Peer.Send("BYE");

    reset();
}

void Net::Session::fail(std::string reason) {
    closeSockets();

    m_State = State::Closed;
    m_Error = reason;

    queue(EventType::Closed, std::move(reason));
}

void Net::Session::queue(EventType type, std::string text, bool flag) {
    m_Events.push_back({type, std::move(text), flag});
}

std::optional<Net::Event> Net::Session::Poll() {
    if (m_Events.empty()) return std::nullopt;

    Event event = std::move(m_Events.front());
    m_Events.pop_front();

    return event;
}

void Net::Session::adopt(LineSocket&& socket, bool viaRelay) {
    LineSocket peer = std::move(socket);

    closeSockets();

    m_Peer = std::move(peer);
    m_ViaRelay = viaRelay;
    m_State = State::Connected;
    m_SincePing = m_SinceReceive = 0.f;

    if (m_Resuming) {
        m_AwaitResume = true;
        m_ResumeHandshake = ResumeHandshake;

        if (!m_IsHost) m_Peer.Send("RESUME " + m_Token);
    } else {
        m_Peer.Send("HELLO " + std::to_string(ProtocolVersion) + " " + (m_IsHost ? m_Token : std::string("-")) + " " + CleanName(m_Config.Name));

        queue(EventType::Connected);
    }
}

void Net::Session::startRelay() {
    m_Relay.reset();
    m_RelayEnabled = !m_Config.Relay.empty();
    m_RelayReached = false;
    m_RelayTimer = 0.f;
}

bool Net::Session::openHosting() {
    if (m_Listener.listen(sf::Socket::AnyPort) != sf::Socket::Status::Done) return false;

    m_Listener.setBlocking(false);

    m_UdpBound = m_Udp.bind(DiscoveryPort) == sf::Socket::Status::Done;
    m_Udp.setBlocking(false);

    startRelay();

    return true;
}

void Net::Session::openJoining() {
    m_Elapsed = 0.f;
    m_FindTimer = m_LanAge = m_LanRetry = 0.f;

    m_Udp.setBlocking(false);

    m_FindTargets = {sf::IpAddress::LocalHost, sf::IpAddress::Broadcast};

    if (const std::optional<sf::IpAddress> local = sf::IpAddress::getLocalAddress(); local && IsLocalAddress(*local)) {
        m_FindTargets.emplace_back(local->toInteger() | 0xFFu);
    }

    startRelay();
}

void Net::Session::Host(const std::string& code, Config config) {
    Close();

    m_Config = std::move(config);
    m_Code = code;
    m_IsHost = true;
    m_Token = RandomText(16);
    m_RelayOwner = RandomText(16);

    if (!openHosting()) {
        fail("Couldn't open a network port.");
        return;
    }

    m_State = State::Hosting;
}

void Net::Session::Join(const std::string& code, Config config) {
    Close();

    m_Config = std::move(config);
    m_Code = code;
    m_IsHost = false;
    m_State = State::Joining;

    openJoining();
}

void Net::Session::lostPeer(const std::string& reason) {
    if (!m_Resumable || m_Token.empty()) {
        fail(reason);
        return;
    }

    closeSockets();

    if (!m_Resuming) {
        m_Resuming = true;
        m_ResumeLeft = ResumeWindow;

        queue(EventType::Reconnecting);
    }

    m_State = State::Reconnecting;
    m_AwaitResume = false;

    if (m_IsHost) {
        if (!openHosting()) fail("Couldn't open a network port.");
    } else {
        openJoining();
    }
}

void Net::Session::Send(EventType type, const std::string& text, bool flag, bool option) {
    if (!Connected()) return;

    switch (type) {
    case EventType::Start: if (IsFenText(text)) m_Peer.Send(std::string("START ") + (flag ? "w " : "b ") + (option ? "1 " : "0 ") + text); break;
    case EventType::Move: if (IsMoveToken(text)) m_Peer.Send("MOVE " + text); break;
    case EventType::Resign: m_Peer.Send("RESIGN"); break;
    case EventType::DrawOffer: m_Peer.Send("DRAW"); break;
    case EventType::DrawAccept: m_Peer.Send("DRAWYES"); break;
    case EventType::DrawDecline: m_Peer.Send("DRAWNO"); break;
    case EventType::RematchOffer: m_Peer.Send("REMATCH"); break;
    case EventType::RematchAccept: m_Peer.Send("REMATCHYES"); break;
    case EventType::RematchDecline: m_Peer.Send("REMATCHNO"); break;
    case EventType::Sync: if (text.size() <= 5 && std::all_of(text.begin(), text.end(), [](unsigned char c) { return std::isdigit(c); })) m_Peer.Send("SYNC " + text); break;
    default: break;
    }
}

#pragma region Updating

void Net::Session::Update(float dt) {
    switch (m_State) {
    case State::Hosting: updateHosting(dt); break;
    case State::Joining: updateJoining(dt); break;
    case State::Connected: updateConnected(dt); break;
    case State::Reconnecting: updateReconnecting(dt); break;
    default: break;
    }
}

#pragma region Hosting

void Net::Session::updateHosting(float dt) {
    m_Elapsed += dt;

    for (int i = 0; i < 16 && m_UdpBound; ++i) {
        char buffer[64];
        std::size_t size = 0;
        std::optional<sf::IpAddress> from;
        unsigned short port = 0;

        const sf::Socket::Status status = m_Udp.receive(buffer, sizeof(buffer), size, from, port);

        if (status == sf::Socket::Status::NotReady) break;
        if (status != sf::Socket::Status::Done || !from) continue;

        if (std::string(buffer, size) == "CHESS1 FIND " + m_Code) {
            const std::string reply = "CHESS1 HERE " + m_Code + " " + std::to_string(m_Listener.getLocalPort());
            (void)m_Udp.send(reply.data(), reply.size(), *from, port);
        }
    }

    for (int i = 0; i < 4; ++i) {
        auto socket = std::make_unique<sf::TcpSocket>();

        if (m_Listener.accept(*socket) != sf::Socket::Status::Done) break;
        if (m_Pending.size() >= MaxPending) m_Pending.erase(m_Pending.begin());

        m_Pending.push_back({LineSocket(std::move(socket)), 0.f});
    }

    for (std::size_t i = 0; i < m_Pending.size();) {
        Pending& pending = m_Pending[i];

        pending.Socket.Pump();
        pending.Age += dt;

        if (const std::optional<std::string> line = pending.Socket.NextLine()) {
            if (*line == "JOIN " + m_Code) {
                pending.Socket.Send("PAIRED");

                LineSocket peer = std::move(pending.Socket);
                adopt(std::move(peer), false);
                return;
            }

            pending.Socket.Send("NOTFOUND");
            m_Pending.erase(m_Pending.begin() + static_cast<std::ptrdiff_t>(i));
        } else if (pending.Socket.Dead() || pending.Age > PendingTimeout) {
            m_Pending.erase(m_Pending.begin() + static_cast<std::ptrdiff_t>(i));
        } else {
            ++i;
        }
    }

    if (m_RelayEnabled) hostRelay(dt);
}

void Net::Session::hostRelay(float dt) {
    if (!m_Relay) {
        if ((m_RelayTimer -= dt) <= 0.f) m_Relay = std::make_shared<RelayLink>(m_Config.Relay, RelayLink::Mode::Host, m_Code, m_RelayOwner);
        return;
    }

    m_RelayReached |= m_Relay->Reached();

    if (m_Relay->GetPhase() == RelayLink::Phase::Paired) {
        adopt(LineSocket(m_Relay), true);
    } else if (m_Relay->GetPhase() == RelayLink::Phase::Dead) {
        m_Relay.reset();
        m_RelayTimer = RelayRetryDelay;
    }
}

#pragma region Joining

void Net::Session::updateJoining(float dt) {
    m_Elapsed += dt;

    joinLan(dt);

    if (m_State == State::Connected) return;

    const bool lanBusy = m_LanConnector.Busy() || m_Lan.Valid() || m_LanHost.has_value();

    if (m_RelayEnabled && lanBusy) {
        m_Relay.reset();
    } else if (m_RelayEnabled && m_Elapsed >= RelayDelay) {
        joinRelay(dt);
    }

    if (m_State == State::Connected) return;

    if (m_State == State::Joining && m_Elapsed >= (m_RelayEnabled ? RelayJoinTime : LanOnlyJoinTime)) {
        if (!m_RelayEnabled) fail("No game with that code was found on your network.");
        else if (!m_RelayReached) fail("No game found on your network, and the online server couldn't be reached.");
        else fail("No game with that code was found. Check the code and try again.");
    }
}

void Net::Session::joinLan(float dt) {
    if (m_Lan.Valid()) {
        m_Lan.Pump();
        m_LanAge += dt;

        if (const std::optional<std::string> line = m_Lan.NextLine()) {
            if (*line == "PAIRED") {
                adopt(std::move(m_Lan), false);
                return;
            }

            m_Lan = LineSocket();
            m_LanHost.reset();
            m_LanRetry = 1.f;
        } else if (m_Lan.Dead() || m_LanAge > PendingTimeout) {
            m_Lan = LineSocket();
            m_LanHost.reset();
            m_LanRetry = 1.f;
        }

        return;
    }

    if (m_LanConnector.Busy()) {
        if (auto result = m_LanConnector.Poll()) {
            if (*result) {
                m_Lan = LineSocket(std::move(*result));
                m_Lan.Send("JOIN " + m_Code);
                m_LanAge = 0.f;
            } else {
                m_LanHost.reset();
                m_LanRetry = 1.f;
            }
        }

        return;
    }

    m_LanRetry -= dt;

    for (int i = 0; i < 16; ++i) {
        char buffer[64];
        std::size_t size = 0;
        std::optional<sf::IpAddress> from;
        unsigned short port = 0;

        const sf::Socket::Status status = m_Udp.receive(buffer, sizeof(buffer), size, from, port);

        if (status == sf::Socket::Status::NotReady) break;
        if (status != sf::Socket::Status::Done || !from || !IsLocalAddress(*from)) continue;

        const std::string reply(buffer, size);
        const std::string prefix = "CHESS1 HERE " + m_Code + " ";

        if (reply.size() <= prefix.size() || reply.compare(0, prefix.size(), prefix) != 0) continue;

        unsigned parsed = 0;
        const char* begin = reply.data() + prefix.size();
        const char* end = reply.data() + reply.size();
        const auto [last, error] = std::from_chars(begin, end, parsed);

        if (error != std::errc() || last != end || parsed == 0 || parsed > 65535) continue;

        m_LanHost = *from;
        m_LanPort = static_cast<unsigned short>(parsed);
        m_LanConnector.Start(*m_LanHost, m_LanPort, 3.f);

        return;
    }

    m_FindTimer -= dt;

    if (m_FindTimer <= 0.f && m_LanRetry <= 0.f) {
        const std::string query = "CHESS1 FIND " + m_Code;

        for (const sf::IpAddress& target : m_FindTargets) (void)m_Udp.send(query.data(), query.size(), target, DiscoveryPort);

        m_FindTimer = 0.5f;
    }
}

void Net::Session::joinRelay(float dt) {
    if (!m_Relay) {
        if ((m_RelayTimer -= dt) <= 0.f) m_Relay = std::make_shared<RelayLink>(m_Config.Relay, RelayLink::Mode::Join, m_Code, std::string());
        return;
    }

    m_RelayReached |= m_Relay->Reached();

    if (m_Relay->GetPhase() == RelayLink::Phase::Paired) {
        adopt(LineSocket(m_Relay), true);
    } else if (m_Relay->GetPhase() == RelayLink::Phase::Dead) {
        m_Relay.reset();
        m_RelayTimer = RelayRetryDelay;
    }
}

#pragma region Connected

void Net::Session::updateConnected(float dt) {
    m_Peer.Pump();

    m_SinceReceive += dt;
    m_SincePing += dt;

    if (m_Resuming && (m_ResumeLeft -= dt) <= 0.f) {
        fail("Your opponent disconnected.");
        return;
    }

    if (m_AwaitResume && (m_ResumeHandshake -= dt) <= 0.f) {
        lostPeer("Your opponent disconnected.");
        return;
    }

    for (int budget = LineBudget;; --budget) {
        const std::optional<std::string> line = budget > 0 ? m_Peer.NextLine() : std::nullopt;

        if (!line) {
            if (budget > 0 && m_Peer.Dead()) lostPeer("Your opponent disconnected.");
            break;
        }

        m_SinceReceive = 0.f;
        handleLine(*line);

        if (m_State != State::Connected) return;
    }

    if (m_State != State::Connected) return;

    if (m_SinceReceive > SilenceLimit) {
        lostPeer("Lost the connection to your opponent.");
    } else if (m_SincePing >= PingInterval && !m_AwaitResume) {
        m_Peer.Send("PING");
        m_SincePing = 0.f;
    }
}

void Net::Session::updateReconnecting(float dt) {
    if ((m_ResumeLeft -= dt) <= 0.f) {
        fail("Your opponent disconnected.");
        return;
    }

    if (m_IsHost) updateHosting(dt);
    else updateJoining(dt);
}

void Net::Session::handleLine(const std::string& line) {
    const std::size_t space = line.find(' ');
    const std::string command = line.substr(0, space);
    const std::string argument = space == std::string::npos ? std::string() : line.substr(space + 1);

    if (m_AwaitResume) {
        const bool proven = m_IsHost ? command == "RESUME" && argument == m_Token : command == "RESUMED";

        if (!proven) {
            lostPeer("Your opponent disconnected.");
            return;
        }

        if (m_IsHost) m_Peer.Send("RESUMED");

        m_AwaitResume = false;
        m_Resuming = false;
        m_ResumeLeft = 0.f;

        queue(EventType::Resumed);
        return;
    }

    if (command == "HELLO") {
        const std::size_t first = argument.find(' ');
        const std::size_t second = first == std::string::npos ? first : argument.find(' ', first + 1);
        int version = 0;

        const std::string number = argument.substr(0, first);
        (void)std::from_chars(number.data(), number.data() + number.size(), version);

        if (version != ProtocolVersion) {
            m_Peer.Send("BYE");
            fail("Your opponent is running a different version of the game.");
            return;
        }

        if (second != std::string::npos) {
            const std::string token = argument.substr(first + 1, second - first - 1);

            if (!m_IsHost && IsToken(token)) m_Token = token;

            m_PeerName = CleanName(argument.substr(second + 1));
        }
    } else if (command == "START") {
        if (argument.size() > 4 && argument[1] == ' ' && (argument[0] == 'w' || argument[0] == 'b') && argument[2] >= '0' && argument[2] <= '1' && argument[3] == ' ' && IsFenText(argument.substr(4))) {
            queue(EventType::Start, argument.substr(4), argument[0] == 'w');
            m_Events.back().Option = argument[2] == '1';
        }
    } else if (command == "MOVE") {
        if (IsMoveToken(argument)) queue(EventType::Move, argument);
    } else if (command == "SYNC") {
        if (!argument.empty() && argument.size() <= 5 && std::all_of(argument.begin(), argument.end(), [](unsigned char c) { return std::isdigit(c); })) {
            queue(EventType::Sync, argument);
        }
    } else if (command == "RESIGN") {
        queue(EventType::Resign);
    } else if (command == "DRAW") {
        queue(EventType::DrawOffer);
    } else if (command == "DRAWYES") {
        queue(EventType::DrawAccept);
    } else if (command == "DRAWNO") {
        queue(EventType::DrawDecline);
    } else if (command == "REMATCH") {
        queue(EventType::RematchOffer);
    } else if (command == "REMATCHYES") {
        queue(EventType::RematchAccept);
    } else if (command == "REMATCHNO") {
        queue(EventType::RematchDecline);
    } else if (command == "BYE") {
        fail("Your opponent left the game.");
    }
}

#pragma region Text for the UI

std::string Net::Session::Status() const {
    switch (m_State) {
    case State::Hosting:
        return "Waiting for your opponent...";

    case State::Joining:
        if (m_LanHost || m_Lan.Valid()) return "Found a game on your network, connecting...";
        if (m_RelayEnabled && m_Elapsed >= RelayDelay) return "Looking for game " + FormatCode(m_Code) + " online...";
        return "Looking for game " + FormatCode(m_Code) + "...";

    case State::Connected:
        return "Connected";

    case State::Reconnecting:
        return "Connection lost, reconnecting...";

    case State::Closed:
        return m_Error;

    default:
        return {};
    }
}

std::string Net::Session::RelayInfo() const {
    if (m_State != State::Hosting && m_State != State::Joining) return {};

    if (!m_RelayEnabled) return "Local network only";
    if (!m_Relay) return m_State == State::Hosting ? "Online server: starting..." : std::string();
    if (m_Relay->Failing()) {
        if (const int status = m_Relay->LastStatus(); status >= 300 && status < 1000) return "Online server: unexpected reply (HTTP " + std::to_string(status) + "), trying again...";

        return "Online server: not reachable, trying again...";
    }
    if (!m_Relay->Reached()) return "Online server: connecting, it may take a moment to wake up...";

    return "Online server: connected";
}
