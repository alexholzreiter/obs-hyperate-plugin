#pragma once

#include <atomic>
#include <chrono>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

#ifdef HYPERATE_HAVE_LIBWEBSOCKETS
struct lws;
struct lws_context;
struct lws_vhost;
#endif

namespace hyperate {

enum class ConnectionStatus {
	Disconnected,
	Connecting,
	Connected,
	Error,
};

struct HyperateClientConfig {
	std::string host = "app.hyperate.io";
	std::string path = "/ws";
	int port = 443;
	bool use_tls = true;
	std::string channel_id;
	std::string api_key;
	double smoothing_alpha = 0.25;
};

// One long-lived libwebsockets context and service thread are created per client
// instance and kept alive for its whole lifetime. Connect/disconnect only open or
// close the client connection (wsi) inside that context. Recreating and destroying
// the context per connection tears down global OpenSSL state (OPENSSL_cleanup is a
// one-way door), which broke every reconnect on Windows.
class HyperateClient {
public:
	HyperateClient();
	~HyperateClient();

	HyperateClient(const HyperateClient &) = delete;
	HyperateClient &operator=(const HyperateClient &) = delete;

	void start(HyperateClientConfig config);
	void stop();

	ConnectionStatus status() const;
	std::string status_message() const;

#ifdef HYPERATE_HAVE_LIBWEBSOCKETS
	void on_lws_established(::lws *wsi);
	void on_lws_receive(const char *message, size_t len);
	int on_lws_writable();
	void on_lws_connection_error();
	void on_lws_closed();
#endif

private:
	void run();
	void set_status(ConnectionStatus status, std::string message);
	void handle_text_message(const std::string &message);
	void wake_service_thread();

#ifdef HYPERATE_HAVE_LIBWEBSOCKETS
	void open_connection();
	bool connect_via(::lws_vhost *vhost, bool via_proxy);
	void request_close();
	void send_join_frame();
	void send_leave_frame();
	void send_heartbeat_frame();
	void send_text_frame(const std::string &payload);
#endif

	std::thread thread_;
	std::atomic<bool> shutdown_{false};

	mutable std::mutex status_mutex_;
	ConnectionStatus status_ = ConnectionStatus::Disconnected;
	std::string status_message_ = "Disconnected";

	// Commands from the UI thread to the service thread.
	std::mutex command_mutex_;
	bool connect_pending_ = false;
	bool disconnect_pending_ = false;
	HyperateClientConfig pending_config_;

	// Only touched on the service thread.
	HyperateClientConfig config_;

#ifdef HYPERATE_HAVE_LIBWEBSOCKETS
	::lws_context *lws_context_ = nullptr; // published under command_mutex_ for wake_service_thread()
	::lws *wsi_ = nullptr;
	// Two vhosts share the one context: "direct" never uses a proxy, "proxy" tunnels
	// through a detected HTTP proxy. A connection tries direct first and only falls
	// back to the proxy vhost if that fails, so proxy users and direct users both work
	// with no extra settings.
	::lws_vhost *direct_vhost_ = nullptr;
	::lws_vhost *proxy_vhost_ = nullptr;
	std::string proxy_;               // "[user:pass@]host:port" or empty if none detected
	bool via_proxy_now_ = false;      // is the in-flight attempt the proxy one
	bool proxy_retry_pending_ = false;// direct failed; start the proxy attempt in the loop
	bool closing_ = false;
	std::deque<std::string> pending_messages_;
	std::chrono::steady_clock::time_point next_heartbeat_{};
	std::chrono::steady_clock::time_point handshake_deadline_{};
	bool handshake_timed_out_ = false;
#endif
};

const char *connection_status_name(ConnectionStatus status);

} // namespace hyperate
