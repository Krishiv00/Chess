#pragma once

#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "SFML/Network.hpp"

namespace Net {
    constexpr unsigned short DiscoveryPort = 47821;
    constexpr int ProtocolVersion = 3;
    inline constexpr const char* RelayUrl = "https://chess-relay-5fjx.onrender.com/";
    constexpr std::size_t CodeLength = 6;

    [[nodiscard]]
    std::string GenerateCode();

    [[nodiscard]]
    std::string NormalizeCode(const std::string& text);

    [[nodiscard]]
    std::string FormatCode(const std::string& code);

    enum class EventType : uint8_t {
        Connected,
        Closed,
        Start,
        Move,
        Resign,
        DrawOffer,
        DrawAccept,
        DrawDecline,
        RematchOffer,
        RematchAccept,
        RematchDecline,
        Reconnecting,
        Resumed,
        Sync,
    };

    struct Event {
        EventType Type;
        std::string Text;
        bool Flag{false};
        bool Option{true};
    };

    class RelayLink final {
    private:
        struct State;
        std::shared_ptr<State> m_State;

    public:
        enum class Mode : uint8_t { Host, Join };
        enum class Phase : uint8_t { Connecting, Waiting, Paired, Dead };

        RelayLink(const std::string& url, Mode mode, const std::string& code, const std::string& owner);
        ~RelayLink();

        RelayLink(const RelayLink&) = delete;
        RelayLink& operator=(const RelayLink&) = delete;

        [[nodiscard]]
        Phase GetPhase() const noexcept;

        [[nodiscard]]
        bool Reached() const noexcept;

        [[nodiscard]]
        bool Failing() const noexcept;

        [[nodiscard]]
        int LastStatus() const noexcept;

        void Send(const std::string& lines);

        void Take(std::string& into, std::size_t maxBytes);
    };

    class LineSocket final {
    private:
        std::unique_ptr<sf::TcpSocket> m_Socket;
        std::shared_ptr<RelayLink> m_Link;
        std::string m_In;
        std::string m_Out;
        bool m_Dead{false};

    public:
        LineSocket() = default;
        explicit LineSocket(std::unique_ptr<sf::TcpSocket> socket);
        explicit LineSocket(std::shared_ptr<RelayLink> link);

        void Send(const std::string& line);

        void Pump();

        [[nodiscard]]
        std::optional<std::string> NextLine();

        [[nodiscard]]
        inline bool Dead() const noexcept {
            return m_Dead;
        }

        [[nodiscard]]
        inline bool Valid() const noexcept {
            return m_Socket != nullptr || m_Link != nullptr;
        }

        void Flush();
    };

    class Connector final {
    private:
        struct State;
        std::shared_ptr<State> m_State;

    public:
        void Start(sf::IpAddress address, unsigned short port, float timeoutSeconds);

        [[nodiscard]]
        bool Busy() const noexcept {
            return m_State != nullptr;
        }

        [[nodiscard]]
        std::optional<std::unique_ptr<sf::TcpSocket>> Poll();

        void Cancel() noexcept {
            m_State.reset();
        }
    };

    class Session final {
    public:
        enum class State : uint8_t {
            Idle, Hosting, Joining, Connected, Reconnecting, Closed
        };

        struct Config {
            std::string Name;
            std::string Relay; // empty for local network only
        };

    private:
        struct Pending {
            LineSocket Socket;
            float Age{0.f};
        };

        void reset();
        void closeSockets();
        void fail(std::string reason);
        void adopt(LineSocket&& socket, bool viaRelay);
        bool openHosting();
        void openJoining();
        void lostPeer(const std::string& reason);
        void startRelay();
        void handleLine(const std::string& line);
        void queue(EventType type, std::string text = {}, bool flag = false);

        void updateHosting(float dt);
        void updateJoining(float dt);
        void updateConnected(float dt);
        void updateReconnecting(float dt);

        void hostRelay(float dt);
        void joinRelay(float dt);
        void joinLan(float dt);

        State m_State{State::Idle};
        Config m_Config;
        std::string m_Code;
        std::string m_Error;
        std::string m_PeerName;
        std::deque<Event> m_Events;
        float m_Elapsed{0.f};

        // Connected
        LineSocket m_Peer;
        float m_SincePing{0.f};
        float m_SinceReceive{0.f};
        bool m_ViaRelay{false};
        bool m_IsHost{false};

        // Getting a dropped connection back
        std::string m_Token;
        bool m_Resumable{false};
        bool m_Resuming{false};
        bool m_AwaitResume{false};
        float m_ResumeLeft{0.f};
        float m_ResumeHandshake{0.f};

        // Hosting
        sf::TcpListener m_Listener;
        sf::UdpSocket m_Udp;
        bool m_UdpBound{false};
        std::vector<Pending> m_Pending;

        // Relay (both roles)
        std::shared_ptr<RelayLink> m_Relay;
        std::string m_RelayOwner;
        bool m_RelayEnabled{false};
        bool m_RelayReached{false};
        float m_RelayTimer{0.f};

        // Joining on the local network
        Connector m_LanConnector;
        LineSocket m_Lan;
        std::vector<sf::IpAddress> m_FindTargets;
        std::optional<sf::IpAddress> m_LanHost;
        unsigned short m_LanPort{0};
        float m_FindTimer{0.f};
        float m_LanAge{0.f};
        float m_LanRetry{0.f};

    public:
        Session() = default;
        ~Session();

        Session(const Session&) = delete;
        Session& operator=(const Session&) = delete;

        void Host(const std::string& code, Config config);
        void Join(const std::string& code, Config config);

        void Close();

        void Update(float dt);

        [[nodiscard]]
        std::optional<Event> Poll();

        void Send(EventType type, const std::string& text = {}, bool flag = false, bool option = true);

        [[nodiscard]]
        inline State GetState() const noexcept {
            return m_State;
        }

        [[nodiscard]]
        inline bool Connected() const noexcept {
            return m_State == State::Connected && !m_AwaitResume;
        }

        [[nodiscard]]
        inline bool Reconnecting() const noexcept {
            return m_Resuming && m_State != State::Closed;
        }

        [[nodiscard]]
        inline int ResumeSecondsLeft() const noexcept {
            return m_ResumeLeft > 0.f ? static_cast<int>(m_ResumeLeft) + 1 : 0;
        }

        inline void SetResumable(bool value) noexcept {
            m_Resumable = value;
        }

        [[nodiscard]]
        inline const std::string& Code() const noexcept {
            return m_Code;
        }

        [[nodiscard]]
        inline const std::string& Error() const noexcept {
            return m_Error;
        }

        [[nodiscard]]
        inline const std::string& PeerName() const noexcept {
            return m_PeerName;
        }

        [[nodiscard]]
        inline bool ViaRelay() const noexcept {
            return m_ViaRelay;
        }

        [[nodiscard]]
        inline bool Unstable() const noexcept {
            return m_State == State::Connected && m_SinceReceive > 10.f;
        }

        [[nodiscard]]
        std::string Status() const;

        [[nodiscard]]
        std::string RelayInfo() const;
    };
}
