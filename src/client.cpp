#include "client.h"
#include "glaze/glaze.hpp"

Client::Client(connection_hdl hdl, PacketSender &sender, conn_type type)
    : type{type}, hdl{hdl}, sender{sender}, frame_num{0}, mute{false} {}

void PacketSender::send_binary_packet(connection_hdl hdl, const void *data,
                                      size_t size) {
    send_binary_packet(hdl, {{data, size}});
}

void PacketSender::send_text_packet(connection_hdl hdl,
                                    const std::string &data) {
    send_text_packet(hdl, {data});
}

/* clang-format off */
struct window_cmd {
    int l;
    int r;
    std::optional<double> m;
    std::optional<int> level;
};

template <> 
struct glz::meta<window_cmd>
{
    using T = window_cmd;
    static constexpr auto value = object(
        "l", &T::l,
        "r", &T::r,
        "m", &T::m,
        "level", &T::level
    );
};

struct demodulation_cmd {
    std::string demodulation;
};

template <>
struct glz::meta<demodulation_cmd>
{
    using T = demodulation_cmd;
    static constexpr auto value = object(
        "demodulation", &T::demodulation
    );
};

struct userid_cmd {
    std::string userid;
};

template <>
struct glz::meta<userid_cmd>
{
    using T = userid_cmd;
    static constexpr auto value = object(
        "userid", &T::userid
    );
};

struct mute_cmd {
    bool mute;
};

template <>
struct glz::meta<mute_cmd>
{
    using T = mute_cmd;
    static constexpr auto value = object(
        "mute", &T::mute
    );
};

struct chat_cmd {
    std::string message;
    std::string username;
    std::optional<std::string> user_id;
    std::optional<std::string> reply_to_id;
    std::optional<std::string> reply_to_username;
};

template <>
struct glz::meta<chat_cmd>
{
    using T = chat_cmd;
    static constexpr auto value = object(
        "message", &T::message,
        "username", &T::username,
        "user_id", &T::user_id,
        "reply_to_id", &T::reply_to_id,
        "reply_to_username", &T::reply_to_username
    );
};

struct agc_cmd {
    std::string speed;
    std::optional<float> attack;
    std::optional<float> release;
};

template <>
struct glz::meta<agc_cmd>
{
    using T = agc_cmd;
    static constexpr auto value = object(
        "speed", &T::speed,
        "attack", &T::attack,
        "release", &T::release
    );
};

struct buffer_cmd {
    std::string size;
};

template <>
struct glz::meta<buffer_cmd>
{
    using T = buffer_cmd;
    static constexpr auto value = object(
        "size", &T::size
    );
};

using msg_variant = std::variant<window_cmd, demodulation_cmd, userid_cmd, mute_cmd, chat_cmd, agc_cmd, buffer_cmd>;

template <>
struct glz::meta<msg_variant>
{
    static constexpr std::string_view tag = "cmd";
    static constexpr auto ids = std::array{
        "window",
        "demodulation",
        "userid",
        "mute",
        "chat",
        "agc",
        "buffer"
    };
};

template<class... Ts>
struct overloaded : Ts... { using Ts::operator()...; };
/* clang-format on */

void Client::on_message(std::string &msg) {
    msg_variant msg_parsed;
    auto ec = glz::read_json(msg_parsed, msg);
    if (ec) {
        return;
    }

    std::visit(
        overloaded{
            [&](window_cmd &cmd) {
                on_window_message(cmd.l, cmd.m, cmd.r, cmd.level);
            },
            [&](demodulation_cmd &cmd) {
                on_demodulation_message(cmd.demodulation);
            },
            [&](userid_cmd &cmd) {
                on_userid_message(cmd.userid);
            },
            [&](chat_cmd &cmd) {
                std::string user_id = cmd.user_id.value_or("legacy_" + std::to_string(reinterpret_cast<uintptr_t>(hdl.lock().get())));
                std::string reply_to_id = cmd.reply_to_id.value_or("");
                std::string reply_to_username = cmd.reply_to_username.value_or("");
                on_chat_message(hdl, user_id, cmd.username, cmd.message, reply_to_id, reply_to_username);
            },
            [&](mute_cmd &cmd) {
                on_mute(cmd.mute);
            },
            [&](agc_cmd &cmd) {
                on_agc_message(cmd.speed, cmd.attack, cmd.release);
            },
            [&](buffer_cmd &cmd) {
                on_buffer_message(cmd.size);
            }
        },
        msg_parsed);
}

void Client::on_window_message(int, std::optional<double> &, int,
                               std::optional<int> &) {}
void Client::on_demodulation_message(std::string &) {}
void Client::on_chat_message(connection_hdl, std::string &, std::string &, std::string &, std::string &, std::string &) {}
void Client::on_agc_message(std::string &, std::optional<float> &, std::optional<float> &) {}
void Client::on_buffer_message(std::string &) {}
void Client::on_userid_message(std::string &userid) {
    // Used for correlating between signal and waterfall sockets
    if (userid.length() >= 32) {
        user_id = userid.substr(0, 32);
    } else {
        user_id = userid;
    }
}
void Client::on_mute(bool mute) { this->mute = mute; }